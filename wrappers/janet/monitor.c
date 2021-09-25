#include "wrapper.h"

/* Deinitialising */

static int watchful_monitor_gc(void *p, size_t size) {
    /* Should teardown monitor here */
    (void) size;
    watchful_monitor_destroy((WatchfulMonitor *)p);
    return 0;
}

/* Marking */

static int watchful_monitor_mark(void *p, size_t size) {
    (void) size;
    WatchfulMonitor *monitor = (WatchfulMonitor *)p;
    return 0;
}

/* Type Definition */

const JanetAbstractType watchful_monitor_type = {
    "watchful/monitor",
    watchful_monitor_gc,
    watchful_monitor_mark,
    JANET_ATEND_GCMARK
};
