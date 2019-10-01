/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <gtimer.h>
#include <gimxcommon/include/gerror.h>
#include <gimxcommon/include/glist.h>
#include <gimxlog/include/glog.h>
#include <gimxtime/include/gtime.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

GLOG_INST(GLOG_NAME)

struct gtimer {
  int fd;
  void * user;
  GPOLL_READ_CALLBACK fp_read;
  GPOLL_CLOSE_CALLBACK fp_close;
  GTIMER_REMOVE_SOURCE fp_remove;
  GLIST_LINK(struct gtimer);
};

static GLIST_INST(struct gtimer, timers);
GLIST_DESTRUCTOR(timers, gtimer_close)

static int close_callback(void * user) {

  struct gtimer * timer = (struct gtimer *) user;

  return timer->fp_close(timer->user);
}

static int read_callback(void * user) {

  struct gtimer * timer = (struct gtimer *) user;

  uint64_t nexp;
  ssize_t res;

  res = read(timer->fd, &nexp, sizeof(nexp));
  if (res != sizeof(nexp)) {
    PRINT_ERROR_ERRNO("read");
    return -1;
  }
  if (nexp > 1) {
      gtime now = gtime_gettime();
      if (GLOG_LEVEL(GLOG_NAME,ERROR)) {
        fprintf (stderr, "%lu.%06lu timer fired %" PRIu64 " times...\n", GTIME_SECPART(now), GTIME_USECPART(now), nexp);
      }
  }

  return timer->fp_read(timer->user);
}

struct gtimer * gtimer_start(void * user, unsigned int usec, const GTIMER_CALLBACKS * callbacks) {

  __time_t sec = usec / 1000000;
  __time_t nsec = (usec - sec * 1000000) * 1000;
  struct timespec period = { .tv_sec = sec, .tv_nsec = nsec };
  struct itimerspec new_value = { .it_interval = period, .it_value = period, };

  if (callbacks->fp_register == NULL)
  {
    PRINT_ERROR_OTHER("fp_register is NULL");
    return NULL;
  }

  if (callbacks->fp_remove == NULL)
  {
    PRINT_ERROR_OTHER("fp_remove is NULL");
    return NULL;
  }

  int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
  if (tfd < 0) {
    PRINT_ERROR_ERRNO("timerfd_create");
    return NULL;
  }

  int ret = timerfd_settime(tfd, 0, &new_value, NULL);
  if (ret) {
    PRINT_ERROR_ERRNO("timerfd_settime");
    close(tfd);
    return NULL;
  }

  struct gtimer * timer = calloc(1, sizeof(*timer));
  if (timer == NULL) {
    PRINT_ERROR_ALLOC_FAILED("calloc");
    return NULL;
  }

  GPOLL_CALLBACKS gpoll_callbacks = {
          .fp_read = read_callback,
          .fp_write = NULL,
          .fp_close = close_callback,
  };
  ret = callbacks->fp_register(tfd, timer, &gpoll_callbacks);
  if (ret < 0) {
    close(tfd);
    free(timer);
    return NULL;
  }

  timer->fd = tfd;
  timer->user = user;
  timer->fp_read = callbacks->fp_read;
  timer->fp_close = callbacks->fp_close;
  timer->fp_remove = callbacks->fp_remove;

  GLIST_ADD(timers, timer)

  return timer;
}

int gtimer_close(struct gtimer * timer) {

  timer->fp_remove(timer->fd);
  close(timer->fd);

  GLIST_REMOVE(timers, timer)

  free(timer);

  return 1;
}
