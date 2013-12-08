/*
 * alsa.h
 *
 *  Created on: Dec 8, 2013
 *      Author: jonas
 */

#ifndef ALSA_H_
#define ALSA_H_

#include <stdbool.h>

struct AlsaContextStruct;
typedef struct AlsaContextStruct AlsaContext;

AlsaContext* alsaInit(void);
bool alsaLoop(AlsaContext* ctx);

bool alsaConnectMidiInput(AlsaContext* ctx, const char* port);

#endif /* ALSA_H_ */
