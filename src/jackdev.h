/*
 *  Created on: Dec 8, 2013
 *      Author: jonas
 */

#ifndef JACKDEV_H_
#define JACKDEV_H_

#include <jack/jack.h>

typedef struct {
	enum Type {
		EVENT_SET_LEVEL,
		EVENT_START_RECORDING,
		EVENT_STOP_RECORDING
	} type;
	int loop;
	float level;
} Event;

typedef jack_default_audio_sample_t Sample;
typedef void (*CreateSamplesFn)(void* ctx, Sample* buffer, int count);
typedef void (*EventFn)(void* ctx, const Event* event);

struct JackContextStruct;
typedef struct JackContextStruct JackContext;

bool jackLoop(JackContext* ctx);
JackContext* jackInit(CreateSamplesFn createSamplesFn, EventFn eventFn, void* cbCtx);
bool jackConnectMidiInput(JackContext* ctx, const char* port);

#endif /* JACKDEV_H_ */
