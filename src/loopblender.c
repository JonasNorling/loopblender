/*
 *  Created on: Dec 8, 2013
 *      Author: Jonas Norling
 *
 * This program plays and blends synchronized sound loops.
 */

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>

#include "jackdev.h"

/* *************************************************************
 * Types and constants
 * *************************************************************/

static const int MidiNoteA4 = 69;
static const int HzA4 = 440;

typedef struct {
	int samplerate; ///< Sample rate in Hz
	int looplen;    ///< Loop length in samples
	int loopcount;  ///< Number of loops
	int channels;   ///< Number of channels (1=mono, 2=stereo)
	Sample* buffer; ///< Sample buffer where all the loops are kept
	int buffersize; ///< Buffer size in bytes

	int samplept;   ///< Currently playing sample number

	float* loopLevels; ///< Current volume for each loop
} Context;

/* *************************************************************
 * Static functions
 * *************************************************************/

/**
 * Convert a MIDI note number to frequency
 */
static float noteToHz(int note)
{
	return exp2f((note - MidiNoteA4)/12.0) * HzA4;
}

/**
 * Get the offset of a sample in the loop buffer
 */
static inline int sampleOffset(Context* ctx, int loop, int sample, int channel)
{
	// Samples are stored interleaved in the buffer, so that samples for the
	// same time offset are adjacent.
	assert(sample < ctx->looplen);
	assert(channel < ctx->channels);
	assert(loop < ctx->loopcount);

	return sample * (ctx->loopcount * ctx->channels) + loop * ctx->channels + channel;
}

static bool allocateBuffer(Context* ctx)
{
	const int samples = ctx->loopcount * ctx->channels * ctx->looplen;
	ctx->buffersize = sizeof(Sample) * samples;
	printf("Allocating %.1f MiB sample memory\n",
			(1.0f * ctx->buffersize) / 1024 / 1024);
	ctx->buffer = malloc(ctx->buffersize);
	if (ctx->buffer != 0) {
		if (mlock(ctx->buffer, ctx->buffersize) != 0) {
			perror("Failed to lock buffer memory in RAM");
		}
		memset(ctx->buffer, 0, ctx->buffersize);
		return true;
	}
	return false;
}

static void fillWithTestData(Context* ctx)
{
	fprintf(stderr, "Generating test loops... ");

	for (int loop = 0; loop < ctx->loopcount; loop++) {

		// Fill the loops with sine waves. Those are not spliced properly,
		// so there will be clicking. Also, this is silly slow of course.

		const float hz = noteToHz(loop);
		const Sample amplitude = 0.3;
		for (int sample = 0; sample < ctx->looplen; sample++) {
			Sample value = amplitude * sinf(sample * 2*M_PI * hz / ctx->samplerate);
			ctx->buffer[sampleOffset(ctx, loop, sample, 0)] = value;
		}
	}

	fprintf(stderr, "done.\n");
}

static void createSamples(void* _ctx, Sample* buffer, int count)
{
	Context* ctx = (Context*)_ctx;

	int pt = ctx->samplept;

	for (int sample = 0; sample < count; sample++) {
		pt = (pt + 1) % ctx->looplen;

		// Accumulate samples multiplied by volume level
		Sample value = 0.0f;
		for (int loop = 0; loop < ctx->loopcount; loop++) {
			value += ctx->loopLevels[loop] * ctx->buffer[sampleOffset(ctx, loop, pt, 0)];
		}
		buffer[sample] = value;
	}

	ctx->samplept = pt;
}

static void event(void* _ctx, const Event* event)
{
	Context* ctx = (Context*)_ctx;
	switch (event->type) {
	case EVENT_SET_LEVEL:
		if (event->level < ctx->loopcount) {
			ctx->loopLevels[event->loop] = event->level;
			//fprintf(stderr, "Set level of %d to %.2f\n", event->loop, event->level);
		}
		break;
	case EVENT_START_RECORDING:
	case EVENT_STOP_RECORDING:
		break;
	}
}

/* *************************************************************
 * Interface functions
 * *************************************************************/

static void printHelp()
{
	printf("Usage:\n"
			"  -n, --loops=N      Number of loops [default: 100]\n"
			"  -l, --length=N     Loop length in samples [default: 48000]\n"
			"  -t, --testloops    Create test loops\n"
			"  -m, --mididev=PORT Connect MIDI input to this ALSA port (client:port)\n"
			"  -h, --help         Print help\n"
			);
}

int main(int argc, char* argv[])
{
	int length = 48000;
	int loops = 100;
	bool testloops = false;
	const char* mididev = 0;

	struct option longopts[] = {
			{ "loops", required_argument, 0, 'n' },
			{ "length", required_argument, 0, 'l' },
			{ "mididev", required_argument, 0, 'm' },
//			{ "loopdir", required_argument, 0, 'd' },
			{ "testloops", required_argument, 0, 't' },
			{ "help", no_argument, 0, 'h'},
			{ 0, 0, 0, 0}};
	int opt;
	while ((opt = getopt_long(argc, argv, "n:l:m:th", longopts, 0)) != -1) {
		switch (opt) {
		case 'n':
			loops = atoi(optarg);
			break;
		case 'l':
			length = atoi(optarg);
			break;
		case 't':
			testloops = true;
			break;
		case 'm':
			mididev = optarg;
			break;
		case 'h':
			printHelp();
			return 0;
			break;
		default:
			fprintf(stderr, "Unknown option\n");
			return 1;
		}
	}

	Context ctx;
	memset(&ctx, 0, sizeof(Context));
	ctx.loopcount = loops;
	ctx.channels = 1; // Mono
	ctx.looplen = length;
	ctx.loopLevels = calloc(ctx.loopcount, sizeof(float));

	if (!allocateBuffer(&ctx)) {
		fprintf(stderr, "Failed to allocate buffers\n");
		return 1;
	}

	JackContext* jackCtx = jackInit(createSamples, event, &ctx);
	if (jackCtx == 0) {
		fprintf(stderr, "JACK init failed\n");
		return 1;
	}

	ctx.samplerate = 48000; // FIXME: Get from JACK
	if (testloops) {
		fillWithTestData(&ctx);
	}

	if (mididev != 0 && mididev[0] != '\0') {
		if (!jackConnectMidiInput(jackCtx, mididev)) {
			fprintf(stderr, "Failed to connect MIDI input to %s\n", mididev);
		}
	}

	if (!jackLoop(jackCtx)) {
		fprintf(stderr, "JACK failed\n");
		return 1;
	}

	return 0;
}
