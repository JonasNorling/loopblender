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

#include "alsa.h"

/* *************************************************************
 * Types and constants
 * *************************************************************/

static const Sample MaxSample = INT16_MAX;
static const Sample MinSample = INT16_MIN;

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

	uint8_t* loopLevels; ///< Current volume for each loop, 0..127
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
		const Sample amplitude = MaxSample;
		for (int sample = 0; sample < ctx->looplen; sample++) {
			Sample value = round(amplitude * sinf(sample * 2*M_PI * hz / ctx->samplerate));
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

		// Accumulate samples (16 bit) multiplied by volume level (7 bit)
		int32_t value = 0;
		for (int loop = 0; loop < ctx->loopcount; loop++) {
			value += ctx->loopLevels[loop] * ctx->buffer[sampleOffset(ctx, loop, pt, 0)];
		}
		buffer[sample] = value >> 8;
	}

	ctx->samplept = pt;
}

static void setLevel(void* _ctx, int loop, float level)
{
	Context* ctx = (Context*)_ctx;
	if (loop < ctx->loopcount) {
		ctx->loopLevels[loop] = level * 128;
	}
}

/* *************************************************************
 * Interface functions
 * *************************************************************/

static void printHelp()
{
	printf("Usage:\n"
			"  -n, --loops=N      Number of loops [default: 100]\n"
			"  -r, --samplerate=N Sample rate in Hz [default: 48000]\n"
			"  -l, --length=N     Loop length in samples [default: samplerate]\n"
			"  -t, --testloops    Create test loops\n"
			"  -m, --mididev=PORT Connect MIDI input to this ALSA port (client:port)\n"
			"  -h, --help         Print help\n"
			);
}

int main(int argc, char* argv[])
{
	int samplerate = 48000;
	int length = 1 * samplerate;
	int loops = 100;
	bool testloops = false;
	const char* mididev = 0;

	struct option longopts[] = {
			{ "loops", required_argument, 0, 'n' },
			{ "samplerate", required_argument, 0, 'r' },
			{ "length", required_argument, 0, 'l' },
			{ "mididev", required_argument, 0, 'm' },
//			{ "loopdir", required_argument, 0, 'd' },
			{ "testloops", required_argument, 0, 't' },
			{ "help", no_argument, 0, 'h'},
			{ 0, 0, 0, 0}};
	int opt;
	while ((opt = getopt_long(argc, argv, "n:r:l:m:th", longopts, 0)) != -1) {
		switch (opt) {
		case 'n':
			loops = atoi(optarg);
			break;
		case 'r':
			samplerate = atoi(optarg);
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
	ctx.samplerate = samplerate;
	ctx.loopLevels = calloc(ctx.loopcount, sizeof(uint8_t));

	if (!allocateBuffer(&ctx)) {
		fprintf(stderr, "Failed to allocate buffers\n");
		return 1;
	}

	if (testloops) {
		fillWithTestData(&ctx);
	}

	AlsaContext* alsaCtx = alsaInit(createSamples, setLevel, &ctx);
	if (alsaCtx == 0) {
		fprintf(stderr, "ALSA init failed\n");
		return 1;
	}

	if (mididev != 0 && mididev[0] != '\0') {
		if (!alsaConnectMidiInput(alsaCtx, mididev)) {
			fprintf(stderr, "Failed to connect MIDI input to %s\n", mididev);
		}
	}

	if (!alsaLoop(alsaCtx)) {
		fprintf(stderr, "ALSA failed\n");
		return 1;
	}

	return 0;
}
