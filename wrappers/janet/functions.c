#include "wrapper.h"

static int watchful_event_callback(WatchfulEvent *event) {
    printf("You called?\n");
    /*
     * 1. Get pipe.
     * 2. Wrap pointer.
     * 3. Marshal pointer.
     * 4. Send pointer down pipe.
     */
    return 0;
}

/* Exposed Functions */

JANET_FN(cfun_monitor,
        "(_watchful/monitor path pipe)",
        "Native function for creating a monitor") {
    janet_fixarity(argc, 2);

    const char *path = janet_getcstring(argv, 0);
    if (NULL == path) janet_panic("cannot get path");

    JanetStream *pipe = janet_getabstract(argv, 1, &janet_stream_type);
    if (NULL == pipe) janet_panic("cannot get pipe");

    WatchfulMonitor *temp = watchful_monitor_create(NULL, path, 0, NULL, WATCHFUL_EVENT_ALL, watchful_event_callback);
    if (NULL == temp) janet_panic("cannot create monitor");

    WatchfulMonitor *wm = janet_abstract(&watchful_monitor_type, sizeof(WatchfulMonitor));
    wm->backend = temp->backend;
    wm->path = temp->path;
    wm->excludes = temp->excludes;
    wm->events = temp->events;
    wm->callback = temp->callback;
    wm->thread = temp->thread;
    wm->delay = temp->delay;

    free(temp);

    return janet_wrap_abstract(wm);
}

JANET_FN(cfun_make_event,
        "(_watchful/make-event bytes)",
        "Native function for creating an event from bytes") {
    janet_fixarity(argc, 1);

    return janet_wrap_nil();
}

JANET_FN(cfun_start,
        "(_watchful/start monitor)",
        "Native function for starting a watch") {
    janet_fixarity(argc, 1);

    WatchfulMonitor *wm = janet_getabstract(argv, 0, &watchful_monitor_type);
    int error = 0;

    error = watchful_monitor_start(wm);
    if (error) janet_panic("Failed to start monitor cleanly");

    return janet_wrap_nil();
}

JANET_FN(cfun_stop,
        "(_watchful/stop monitor)",
        "Native function for stopping a watch") {
    janet_fixarity(argc, 1);

    WatchfulMonitor *wm = janet_getabstract(argv, 0, &watchful_monitor_type);
    int error = 0;

    error = watchful_monitor_stop(wm);
    if (error) janet_panic("Failed to stop monitor cleanly");

    return janet_wrap_nil();
}

JANET_MODULE_ENTRY(JanetTable *env) {
    janet_cfuns_ext(env, "_watchful", (JanetRegExt[]) {
        JANET_REG("monitor", cfun_monitor),
        JANET_REG("make-event", cfun_make_event),
        JANET_REG("start", cfun_start),
        JANET_REG("stop", cfun_stop),
        JANET_REG_END
    });
}
