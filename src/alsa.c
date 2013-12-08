/*
 *  Created on: Dec 8, 2013
 *      Author: Jonas Norling
 */

#include <alsa/asoundlib.h>
#include <stdbool.h>
#include <poll.h>
#include <stdint.h>

#include "alsa.h"

struct AlsaContextStruct {
	snd_seq_t* seq;
	snd_pcm_t* pcm;
	int inputPort;
	createSamplesFn createSamples;
	setLevelFn setLevel;
	void* cbCtx;
};

static bool openSequencer(AlsaContext* ctx)
{
	int res;

	res = snd_seq_open(&ctx->seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK);
	if (res < 0) {
		return false;
	}

	res = snd_seq_set_client_name(ctx->seq, "Loop blender");
	if (res < 0) {
		return false;
	}

	ctx->inputPort = snd_seq_create_simple_port(ctx->seq, "MIDI IN",
			SND_SEQ_PORT_CAP_WRITE |
			SND_SEQ_PORT_CAP_SUBS_WRITE,
			SND_SEQ_PORT_TYPE_SYNTHESIZER);

	if (ctx->inputPort < 0) {
		return false;
	}

	return true;
}

static bool openPcm(AlsaContext* ctx)
{
	int res;

	res = snd_pcm_open(&ctx->pcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
	if (res < 0) {
		return false;
	}

	res = snd_pcm_set_params(ctx->pcm,
			SND_PCM_FORMAT_S16_LE,
			SND_PCM_ACCESS_RW_INTERLEAVED,
			1,
			48000,
			1,
			10000);
	if (res < 0) {
		fprintf(stderr, "Failed to set params: %s\n", snd_strerror(res));
		return false;
	}

	return true;
}

static void handleEvent(AlsaContext* ctx, snd_seq_event_t* event)
{
	switch (event->type) {
	case SND_SEQ_EVENT_NOTEON:
		fprintf(stderr, "Note on %d v%d on %d\n",
				event->data.note.note,
				event->data.note.velocity,
				event->data.note.channel);
		if (event->data.note.velocity == 0) {
			ctx->setLevel(ctx->cbCtx, event->data.note.note, 0);
		}
		else {
			ctx->setLevel(ctx->cbCtx, event->data.note.note, event->data.note.velocity / 128.0f);
		}
		break;
	case SND_SEQ_EVENT_NOTEOFF:
		fprintf(stderr, "Note off %d v%d on %d\n",
				event->data.note.note,
				event->data.note.velocity,
				event->data.note.channel);
		ctx->setLevel(ctx->cbCtx, event->data.note.note, 0);
		break;
	case SND_SEQ_EVENT_CONTROLLER:
		fprintf(stderr, "Controller %d = %d on %d",
				event->data.control.param,
				event->data.control.value,
				event->data.control.channel);
		break;
	default:
		//fprintf(stderr, "Event type %d\n", event->type);
		break;
	}
}

AlsaContext* alsaInit(createSamplesFn createSamples, setLevelFn setLevel, void* cbCtx)
{
	AlsaContext* ctx = malloc(sizeof(AlsaContext));
	memset(ctx, 0, sizeof(AlsaContext));

	ctx->createSamples = createSamples;
	ctx->setLevel = setLevel;
	ctx->cbCtx = cbCtx;

	bool res = false;
	res = openSequencer(ctx);
	res &= openPcm(ctx);

	if (res) {
		return ctx;
	}
	else {
		return 0;
	}
}

bool alsaConnectMidiInput(AlsaContext* ctx, const char* port)
{
	snd_seq_addr_t addr;
	int res = snd_seq_parse_address(ctx->seq, &addr, port);
	res |= snd_seq_connect_from(ctx->seq, ctx->inputPort, addr.client, addr.port);
	return res == 0;
}

bool alsaLoop(AlsaContext* ctx)
{
	int npfd = 0;
	npfd += snd_seq_poll_descriptors_count(ctx->seq, POLLIN);
	npfd += snd_pcm_poll_descriptors_count(ctx->pcm);
	struct pollfd pfd[npfd];
	snd_seq_poll_descriptors(ctx->seq, pfd, npfd, POLLIN);
	snd_pcm_poll_descriptors(ctx->pcm, pfd, npfd);

	fprintf(stderr, "Running...\n");

	const int bufferlen = 64;
	int consumed = bufferlen;
	Sample buffer[bufferlen];

	while (true) {
		if (poll(pfd, npfd, -1) > 0) {

			// Read PCM events. Feed samples to ALSA using the write API. This implementation is
			// probably fairly inefficient -- we should mmap the buffer instead.
			unsigned short revents = 0;
			snd_pcm_poll_descriptors_revents(ctx->pcm, pfd, npfd, &revents);
			if (revents & POLLERR) {
				fprintf(stderr, "PCM poll error\n");
				exit(1);
			}
			if (revents & POLLOUT) {
				if (snd_pcm_state(ctx->pcm) == SND_PCM_STATE_XRUN ||
						snd_pcm_state(ctx->pcm) == SND_PCM_STATE_SUSPENDED) {
					fprintf(stderr, "xrun\n");
					exit(1);
				}

				if (consumed >= bufferlen) {
					consumed = 0;
					ctx->createSamples(ctx->cbCtx, buffer, bufferlen);
				}
				int wrote = snd_pcm_writei(ctx->pcm, &buffer[consumed], bufferlen - consumed);

				if (wrote > 0) {
					if (wrote != bufferlen) {
						fprintf(stderr, "?");
					}
					consumed += wrote;
				}
				else if (wrote < 0) {
					fprintf(stderr, "Error %s\n", snd_strerror(wrote));
					exit(1);
				}
			}

			// Read MIDI events
			snd_seq_event_t* event = 0;
			int remaining = 17;
			while (remaining > 0) {
				remaining = snd_seq_event_input(ctx->seq, &event);
				if (event != 0) {
					handleEvent(ctx, event);
				}
				snd_seq_free_event(event);
			}
		}
	}

	return true;
}
