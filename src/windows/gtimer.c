/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <gtimer.h>
#include <gimxcommon/include/gerror.h>
#include <gimxcommon/include/glist.h>
#include <gimxlog/include/glog.h>
#include "timerres.h"

#include <windows.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>

GLOG_INST(GLOG_NAME)

struct gtimer {
    void * user;
    unsigned int period; // in base timer ticks
    unsigned int nexp; // number of base timer ticks since last event
    int (*fp_read)(void * user);
    int (*fp_close)(void * user);
    GLIST_LINK(struct gtimer);
};

static GLIST_INST(struct gtimer, timers);

static int timer_cb(unsigned int nexp) {

    int ret = 0;

    struct gtimer * timer;
    for (timer = GLIST_BEGIN(timers); timer != GLIST_END(timers); timer = timer->next) {
        timer->nexp += nexp;
        unsigned int divisor = timer->nexp / timer->period;
        if (divisor >= 1) {
            int status = timer->fp_read(timer->user);
            if (status < 0) {
                ret = -1;
            } else if (ret != -1 && status) {
                ret = 1;
            }
            timer->nexp = 0;
        }
    }

    return ret;
}

struct gtimer * gtimer_start(void * user, unsigned int usec, const GTIMER_CALLBACKS * callbacks) {

    if (usec == 0) {
        PRINT_ERROR_OTHER("timer period cannot be 0");
        return NULL;
    }

    if (callbacks->fp_read == 0) {
        PRINT_ERROR_OTHER("fp_read is null");
        return NULL;
    }

    if (callbacks->fp_register == 0) {
        PRINT_ERROR_OTHER("fp_register is null");
        return NULL;
    }

    if (callbacks->fp_remove == 0) {
        PRINT_ERROR_OTHER("fp_remove is null");
        return NULL;
    }

    GPOLL_INTERFACE poll_interface = {
      .fp_register = callbacks->fp_register,
      .fp_remove = callbacks->fp_remove,
    };
    unsigned int timer_resolution = timerres_begin(&poll_interface, timer_cb);
    if (timer_resolution == 0) {
        return NULL;
    }
    
    unsigned int requested = usec * 10;

    unsigned int lowest = timer_resolution * 9 / 10;
    if (requested < lowest) {
        if (GLOG_LEVEL(GLOG_NAME,ERROR)) {
            fprintf(stderr, "%s:%d %s: timer period should be higher than %dus\n", __FILE__, __LINE__, __func__, lowest / 10);
        }
        timerres_end();
        return NULL;
    }

    unsigned int lower = requested / timer_resolution;
    unsigned int remainder = requested % timer_resolution;
    unsigned int upper = lower + 1;
    unsigned int period = (timer_resolution - remainder > remainder) ? lower : upper;

    if (period * timer_resolution != usec * 10) {
        if (GLOG_LEVEL(GLOG_NAME,INFO)) {
            printf("rounding timer period %uus to %uus\n", usec, period * timer_resolution / 10);
        }
    }

    struct gtimer * timer = calloc(1, sizeof(*timer));
    if (timer == NULL) {
      PRINT_ERROR_ALLOC_FAILED("calloc");
      timerres_end();
      return NULL;
    }

    timer->user = user;
    timer->period = period;
    timer->nexp = 0;
    timer->fp_read = callbacks->fp_read;
    timer->fp_close = callbacks->fp_close;

    GLIST_ADD(timers, timer);

    return timer;
}

int gtimer_close(struct gtimer * timer) {

    GLIST_REMOVE(timers, timer);

    free(timer);

    timerres_end();

    return 1;
}
