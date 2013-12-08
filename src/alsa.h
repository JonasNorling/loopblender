/*
 * alsa.h
 *
 *  Created on: Dec 8, 2013
 *      Author: jonas
 */

#ifndef ALSA_H_
#define ALSA_H_

#include <stdbool.h>

typedef int16_t Sample;
typedef void (*createSamplesFn)(void* ctx, Sample* buffer, int count);
typedef void (*setLevelFn)(void* ctx, int loop, float level);

struct AlsaContextStruct;
typedef struct AlsaContextStruct AlsaContext;

AlsaContext* alsaInit(createSamplesFn createSamples, setLevelFn setLevel, void* cbCtx);
bool alsaLoop(AlsaContext* ctx);

bool alsaConnectMidiInput(AlsaContext* ctx, const char* port);

#endif /* ALSA_H_ */
