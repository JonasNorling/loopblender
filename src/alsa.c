/*
 *  Created on: Dec 8, 2013
 *      Author: Jonas Norling
 */

#include <alsa/asoundlib.h>
#include <stdbool.h>
#include <poll.h>

#include "alsa.h"

struct AlsaContextStruct {
	snd_seq_t* seq;
	int inputPort;
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

static void handleEvent(AlsaContext* ctx, snd_seq_event_t* event)
{
	switch (event->type) {
	case SND_SEQ_EVENT_NOTEON:
		fprintf(stderr, "Note %d v%d on %d\n",
				event->data.note.note,
				event->data.note.velocity,
				event->data.note.channel);
		break;
	case SND_SEQ_EVENT_CONTROLLER:
		fprintf(stderr, "Controller %d = %d on %d",
				event->data.control.param,
				event->data.control.value,
				event->data.control.channel);
		break;
	default:
		fprintf(stderr, "Event type %d\n", event->type);
		break;
	}
}

AlsaContext* alsaInit()
{
	AlsaContext* ctx = malloc(sizeof(AlsaContext));
	memset(ctx, 0, sizeof(AlsaContext));

	bool res = false;
	res = openSequencer(ctx);

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
	int npfd = snd_seq_poll_descriptors_count(ctx->seq, POLLIN);
	struct pollfd pfd[npfd];
	snd_seq_poll_descriptors(ctx->seq, pfd, npfd, POLLIN);

	fprintf(stderr, "Running...\n");

	while (true) {
		if (poll(pfd, npfd, 1000) > 0) {
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
