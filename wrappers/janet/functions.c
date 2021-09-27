#include "wrapper.h"

static int watchful_event_callback(WatchfulEvent *event, void *info) {
    /* 1. Get pipe. */
    JanetStream *pipe = (JanetStream *)info;
    int handle = (int)(pipe->handle);
    ssize_t written_bytes = 0;
    /* 2. Wrap pointer. */
    uint8_t sentinel = 1;
    written_bytes = write(handle, &sentinel, 1);
    debug_print("Wrote %d bytes\n", written_bytes);
    written_bytes = write(handle, &event, sizeof(WatchfulEvent *));
    debug_print("Wrote %d bytes\n", written_bytes);

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

    if (!watchful_path_is_dir(path)) janet_panic("path is not a directory");

    /* WatchfulMonitor *temp = watchful_monitor_create(NULL, path, 0, NULL, WATCHFUL_EVENT_ALL, watchful_event_callback, NULL); */
    /* if (NULL == temp) janet_panic("cannot create monitor"); */

    WatchfulMonitor *wm = janet_abstract(&watchful_monitor_type, sizeof(WatchfulMonitor));
    int error = watchful_monitor_init(wm, NULL, path, 0, NULL, WATCHFUL_EVENT_ALL, watchful_event_callback, (void *)pipe);
    if (error) janet_panic("cannot iniitalise monitor");

    return janet_wrap_abstract(wm);
}

JANET_FN(cfun_read_event,
        "(_watchful/read-event pipe)",
        "Native function for reading an event from a pipe") {
    janet_fixarity(argc, 1);

    debug_print("Trying to read an event...\n");
    JanetStream *pipe = janet_getabstract(argv, 0, &janet_stream_type);
    int handle = (int)(pipe->handle);

    size_t ptr_len = sizeof(WatchfulEvent *);
    WatchfulEvent *event;
    ssize_t written_bytes = 0;
    written_bytes = read(handle, &event, ptr_len);
    if (written_bytes != ptr_len) janet_panic("insufficient number of bytes in pipe");

    JanetString event_path = janet_cstring(event->path);

    return janet_wrap_string(event_path);
}

JANET_FN(cfun_start,
        "(_watchful/start monitor)",
        "Native function for starting a watch") {
    janet_fixarity(argc, 1);

    WatchfulMonitor *wm = janet_getabstract(argv, 0, &watchful_monitor_type);
    int error = 0;

    error = watchful_monitor_start(wm);
    if (error) janet_panic("failed to start monitor cleanly");

    return janet_wrap_nil();
}

JANET_FN(cfun_stop,
        "(_watchful/stop monitor)",
        "Native function for stopping a watch") {
    janet_fixarity(argc, 1);

    WatchfulMonitor *wm = janet_getabstract(argv, 0, &watchful_monitor_type);
    int error = 0;

    error = watchful_monitor_stop(wm);
    if (error) janet_panic("failed to stop monitor cleanly");

    return janet_wrap_nil();
}

JANET_MODULE_ENTRY(JanetTable *env) {
    janet_cfuns_ext(env, "_watchful", (JanetRegExt[]) {
        JANET_REG("monitor", cfun_monitor),
        JANET_REG("read-event", cfun_read_event),
        JANET_REG("start", cfun_start),
        JANET_REG("stop", cfun_stop),
        JANET_REG_END
    });
}
