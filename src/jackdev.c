/*
 *  Created on: Dec 8, 2013
 *      Author: jonas
 */

#include <jack/jack.h>
#include <jack/midiport.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "jackdev.h"

struct JackContextStruct {
	jack_client_t* client;
	jack_port_t* audioin;
	jack_port_t* audioout;
	jack_port_t* midiin;

	int samplerate;

	ProcessFn processFn;
	EventFn eventFn;
	void* cbCtx;
};

static void handleMidi(JackContext* ctx, const jack_midi_event_t* event)
{
	const jack_midi_data_t* data = event->buffer;
	if (event->size == 0) {
		return;
	}
	else if ((data[0] & 0xf0) == 0x90) {
		// NOTE ON event
		if (data[2] > 0) {
			Event e = { .type = EVENT_SET_LEVEL,
					.loop = data[1],
					.level = data[2] / 128.0f };
			ctx->eventFn(ctx->cbCtx, &e);
		} else {
			Event e = { .type = EVENT_SET_LEVEL,
					.loop = data[1],
					.level = 0 };
			ctx->eventFn(ctx->cbCtx, &e);
		}
	}
	else if ((data[0] & 0xf0) == 0x80) {
		// NOTE OFF event
		Event e = { .type = EVENT_SET_LEVEL,
				.loop = data[1],
				.level = 0 };
		ctx->eventFn(ctx->cbCtx, &e);
	}
	else if ((data[0] & 0xf0) == 0xb0) {
		// CONTROLLER event
		if (data[1] == 64) {
			// Sustain switch
			if (data[2] > 0) {
				Event e = { .type = EVENT_START_RECORDING };
				ctx->eventFn(ctx->cbCtx, &e);
			}
			else {
				Event e = { .type = EVENT_STOP_RECORDING };
				ctx->eventFn(ctx->cbCtx, &e);
			}
		}
	}

	//printf("MIDI: %02x %02x %02x %02x\n", data[0], data[1], data[2], data[3]);
}

static int process(jack_nframes_t nframes, void* arg)
{
	JackContext* ctx = (JackContext*)arg;

	/* Process MIDI input */
	void* midibuf = jack_port_get_buffer(ctx->midiin, nframes);
	jack_nframes_t event_count = jack_midi_get_event_count(midibuf);
	for (jack_nframes_t i = 0; i < event_count; i++) {
		jack_midi_event_t event;
		jack_midi_event_get(&event, midibuf, i);
		handleMidi(ctx, &event);
	}

	Sample* inbuf = jack_port_get_buffer(ctx->audioin, nframes);
	Sample* outbuf = jack_port_get_buffer(ctx->audioout, nframes);

	ctx->processFn(ctx->cbCtx, outbuf, inbuf, nframes);

	return 0;
}

JackContext* jackInit(ProcessFn createSamplesFn, EventFn eventFn, void* cbCtx)
{
	JackContext* ctx = malloc(sizeof(JackContext));
	memset(ctx, 0, sizeof(JackContext));

	ctx->processFn = createSamplesFn;
	ctx->eventFn = eventFn;
	ctx->cbCtx = cbCtx;

	ctx->client = jack_client_open("loopblender", JackNullOption, 0);
	if (!ctx->client) {
		fprintf(stderr, "Cannot connect to JACK\n");
		return 0;
	}

	ctx->samplerate = jack_get_sample_rate(ctx->client);

	fprintf(stderr, "Connected to JACK: rate %d Hz, buffer size %d frames, sample size %d bytes\n",
			ctx->samplerate,
			jack_get_buffer_size(ctx->client),
			(int)sizeof(Sample));

	ctx->audioin = jack_port_register(ctx->client, "in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	ctx->audioout = jack_port_register(ctx->client, "out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

	ctx->midiin = jack_port_register(ctx->client, "MIDI-IN", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	jack_set_process_callback(ctx->client, process, ctx);

	if (jack_activate(ctx->client)) {
		fprintf(stderr, "Cannot start jackiness\n");
		return false;
	}

	return ctx;
}

int jackGetSampleRate(JackContext* ctx)
{
	return ctx->samplerate;
}

bool jackConnectMidiInput(JackContext* ctx, const char* port)
{
	int res = jack_connect(ctx->client, port, jack_port_name(ctx->midiin));
	return res == 0;
}

bool jackConnectAudioOutput(JackContext* ctx, const char* port)
{
	int res = jack_connect(ctx->client, jack_port_name(ctx->audioout), port);
	return res == 0;
}

bool jackConnectAudioInput(JackContext* ctx, const char* port)
{
	int res = jack_connect(ctx->client, port, jack_port_name(ctx->audioin));
	return res == 0;
}

bool jackLoop(JackContext* ctx)
{
	while (true) sleep(1);
	jack_client_close(ctx->client);

	return true;
}
