/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <gimxpoll/include/gpoll.h>
#include <gimxtimer/include/gtimer.h>
#include <gimxprio/include/gprio.h>
#include <gimxtime/include/gtime.h>
#include <gimxlog/include/glog.h>

#include <gimxcommon/test/common.h>
#include <gimxcommon/test/handlers.c>

static unsigned int samples = 0;
static int debug = 0;
static int trace = 0;
static int prio = 0;

static int slices[] = { 5, 10, 25, 50, 100 };

struct timer_test {
    gtime period;
    struct gtimer * timer;
    gtime next;
    gtime sum;
    unsigned int count;
    unsigned int slices[sizeof(slices) / sizeof(*slices) + 1];
};

#define ADD_TEST(PERIOD) { PERIOD * 1000LL, NULL, 0, 0, 0, {} },

static struct timer_test timers[] = {
    ADD_TEST(1000)
    ADD_TEST(2000)
    ADD_TEST(3000)
    ADD_TEST(4000)
    ADD_TEST(5000)
    ADD_TEST(6000)
    ADD_TEST(7000)
    ADD_TEST(8000)
    ADD_TEST(9000)
    ADD_TEST(10000)
};

static void usage() {
  fprintf(stderr, "Usage: ./gtimer_test [-d] [-n samples] [-p] [-t]\n");
  exit(EXIT_FAILURE);
}

/*
 * Reads command-line arguments.
 */
static int read_args(int argc, char* argv[]) {

  int opt;
  while ((opt = getopt(argc, argv, "dn:pt")) != -1) {
    switch (opt) {
    case 'd':
      debug = 1;
      break;
    case 'n':
      samples = atoi(optarg);
      break;
    case 'p':
      prio = 1;
      break;
    case 't':
      trace = 1;
      break;
    default: /* '?' */
      usage();
      break;
    }
  }
  return 0;
}

static int timer_close_callback(void * user __attribute__((unused))) {
  set_done();
  return 1;
}

static inline void process(struct timer_test * timer, long long int diff) {

  int percent = diff * 100 / timer->period;

  unsigned int i;
  for (i = 0; i < sizeof(slices) / sizeof(*slices); ++i) {
      if (percent <= slices[i]) {
          break;
      }
  }
  timer->slices[i]++;

  timer->sum += diff;
  ++timer->count;

  if (timer == (timers + sizeof(timers) / sizeof(*timers) - 1) && timer->count == samples) {
    set_done();
  }
}

static int timer_read_callback(void * user) {

  struct timer_test * timer = (struct timer_test *) user;

  gtime now = gtime_gettime();

  gtimediff diff = now - timer->next;

  // Tolerate early firing:
  // - the delay between the timer firing and the process scheduling may vary
  // - on Windows the timer period is rounded to the nearest multiple of the timer resolution.

  process(user, llabs(diff));

#ifdef WIN32
  timer->next = now + timer->period;
#else
  do {
    timer->next += timer->period;
  } while (timer->next <= now);
#endif

  return 1; // Returning a non-zero value makes gpoll return, allowing to check the 'done' variable.
}

int main(int argc, char* argv[]) {

  setup_handlers();

  read_args(argc, argv);

  printf("Press enter to continue.\n");
  fflush(stdout);

  getchar();

  if (debug) {
    glog_set_level("gimxtimer", E_GLOG_LEVEL_DEBUG);
    glog_set_level("gimxprio", E_GLOG_LEVEL_DEBUG);
  }
  if (trace) {
    glog_set_level("gimxtimer", E_GLOG_LEVEL_TRACE);
    glog_set_level("gimxprio", E_GLOG_LEVEL_TRACE);
  }

  if (prio && gprio_init() < 0) {
  	set_done();
  }

  unsigned int i;
  for (i = 0; i < sizeof(timers) / sizeof(*timers); ++i) {

    GTIMER_CALLBACKS timer_callbacks = {
            .fp_read = timer_read_callback,
            .fp_close = timer_close_callback,
            .fp_register = REGISTER_FUNCTION,
            .fp_remove = REMOVE_FUNCTION,
    };
    timers[i].timer = gtimer_start(timers + i, timers[i].period / 1000, &timer_callbacks);
    if (timers[i].timer == NULL) {
      set_done();
      break;
    }

    timers[i].next = gtime_gettime() + timers[i].period;
  }

  while(!is_done()) {
    gpoll();
  }

  for (i = 0; i < sizeof(timers) / sizeof(*timers); ++i) {
    gtimer_close(timers[i].timer);
  }

  if (prio)
  {
    gprio_clean();
  }

  fprintf(stderr, "Exiting\n");

  printf("timer\tperiod\tcount\tdiff");

  unsigned int j;
  for (j = 0; j < sizeof(slices) / sizeof(*slices); ++j) {
    if (j == 0) {
      printf("\t0-%d", slices[j]);
    } else {
      printf("\t%d-%d", slices[j - 1], slices[j]);
    }
  }
  printf("\t>%d\n", slices[j - 1]);

  for (i = 0; i < sizeof(timers) / sizeof(*timers); ++i) {
    if (timers[i].count) {
      printf("%d\t"GTIME_FS"us\t%u\t"GTIME_FS"/1K", i, timers[i].period / 1000, timers[i].count, timers[i].sum * 1000 / timers[i].count / timers[i].period);
      for (j = 0; j < sizeof(timers[i].slices) / sizeof(*timers[i].slices); ++j) {
        printf("\t%d", timers[i].slices[j]);
      }
      printf("\n");
    }
  }

  return 0;
}
