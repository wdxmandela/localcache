#include "cache.h"
#include <unistd.h>

static time_t process_started;
static pthread_t time_reflush_tid;
static volatile int now;
static volatile int run_time_reflush_thread;

static void *time_update(void *arg) {
    while (run_time_reflush_thread) {
    	struct timeval timer;
    	gettimeofday(&timer, NULL);
    	now = (int) (timer.tv_sec - process_started);
    	sleep(1);
    }
    return NULL;
}

time_t time_started(void) {
    return process_started;
}

int time_now(void) {
    return now;
}

rstatus_t time_init(void) {
    process_started = time(NULL) - 2;
    run_time_reflush_thread = 1;
    int err = pthread_create(&time_reflush_tid, NULL, time_update, NULL);
    if (err != 0) return MC_ERROR;
    return MC_OK;
}

void time_deinit(void) {
	run_time_reflush_thread = 0;
	pthread_join(time_reflush_tid, NULL);
}

