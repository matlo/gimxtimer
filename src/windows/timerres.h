/*
 Copyright (c) 2019 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#ifndef TIMERRES_H_
#define TIMERRES_H_

#include <gimxpoll/include/gpoll.h>

typedef int (*TIMERRES_CALLBACK)(unsigned int);

unsigned int timerres_begin(const GPOLL_INTERFACE * poll_interface, TIMERRES_CALLBACK timer_cb);
void timerres_end();

#endif /* TIMERRES_H_ */
