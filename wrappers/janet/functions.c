#include "wrapper.h"

static int event_callback(WatchfulEvent *event, void *info) {
    /* 1. Get pipe. */
    JanetTuple pipe = (JanetTuple)info;
    JanetStream *input = janet_unwrap_abstract(pipe[1]);

    /* 2. Get handle. */
    int handle = (int)(input->handle);

    /* 3. Send sentinel value. */
    ssize_t written_bytes = 0;
    uint8_t sentinel = 1;
    written_bytes = write(handle, &sentinel, 1);

    /* 4. Send pointer. */
    written_bytes = write(handle, &event, sizeof(WatchfulEvent *));

    return 0;
}

static Janet event_wrap_type(int type) {
    switch (type) {
        case WATCHFUL_EVENT_CREATED:
            return janet_ckeywordv("created");
        case WATCHFUL_EVENT_DELETED:
            return janet_ckeywordv("deleted");
        case WATCHFUL_EVENT_MOVED:
            return janet_ckeywordv("moved");
        case WATCHFUL_EVENT_MODIFIED:
            return janet_ckeywordv("modified");
        default:
            return janet_wrap_nil();
    }
}

/* Exposed Functions */

JANET_FN(cfun_monitor,
        "(_watchful/monitor path pipe &opt excluded-paths)",
        "Native function for creating a monitor") {
    janet_fixarity(argc, 2);

    const char *path = janet_getcstring(argv, 0);
    if (NULL == path) janet_panic("cannot get path");

    size_t excl_paths_len = 0;
    const char **excl_paths = NULL;
    if (!janet_checktype(argv[1], JANET_NIL)) {
        JanetTuple tuple = janet_gettuple(argv, 1);
        excl_paths_len = janet_tuple_length(tuple);
        excl_paths = malloc(sizeof(const char *) * excl_paths_len);
        for (size_t i = 0; i < excl_paths_len; i++) {
            /* won't work if path contains embedded zeros */
            excl_paths[i] = (const char *)janet_unwrap_string(tuple[i]);
        }
    }

    if (!watchful_path_is_dir(path)) janet_panic("path is not a directory");

    WatchfulMonitor *wm = janet_abstract(&watchful_monitor_type, sizeof(WatchfulMonitor));
    int error = watchful_monitor_init(wm, NULL, path, excl_paths_len, excl_paths, WATCHFUL_EVENT_ALL, event_callback, NULL);
    if (error) janet_panic("cannot initialise monitor");

    return janet_wrap_abstract(wm);
}

JANET_FN(cfun_get_pipe,
        "(_watchful/get-pipe monitor)",
        "Native function for getting the output pipe") {
    janet_fixarity(argc, 1);

    WatchfulMonitor *wm = janet_getabstract(argv, 0, &watchful_monitor_type);

    JanetTuple pipe = (JanetTuple)(wm->callback_info);
    if (NULL == pipe) janet_panic("no pipe");
    JanetStream *output = janet_unwrap_abstract(pipe[0]);

    return janet_wrap_abstract(output);
}

JANET_FN(cfun_is_watching,
        "(_watchful/watching? monitor)",
        "Native function for checking if monitor is running") {
    janet_fixarity(argc, 1);

    WatchfulMonitor *wm = janet_getabstract(argv, 0, &watchful_monitor_type);

    return janet_wrap_boolean((int)wm->is_watching);
}

JANET_FN(cfun_read_event,
        "(_watchful/read-event pipe)",
        "Native function for reading an event from a pipe") {
    janet_fixarity(argc, 1);

    JanetStream *pipe = janet_getabstract(argv, 0, &janet_stream_type);
    int handle = (int)(pipe->handle);

    size_t ptr_len = sizeof(WatchfulEvent *);
    WatchfulEvent *event;
    ssize_t written_bytes = 0;
    written_bytes = read(handle, &event, ptr_len);
    if (written_bytes != ptr_len) janet_panic("insufficient number of bytes in pipe");

    JanetString event_path = janet_cstring(event->path);

    JanetKV *st = janet_struct_begin(2);
    janet_struct_put(st, janet_ckeywordv("path"), janet_cstringv(event->path));
    janet_struct_put(st, janet_ckeywordv("type"), event_wrap_type(event->type));
    Janet result = janet_wrap_struct(st);

    if (NULL != event->path) free(event->path);
    if (NULL != event) free(event);

    return result;
}

JANET_FN(cfun_start,
        "(_watchful/start monitor pipe)",
        "Native function for starting a watch") {
    janet_fixarity(argc, 2);

    WatchfulMonitor *wm = janet_getabstract(argv, 0, &watchful_monitor_type);

    JanetTuple pipe = janet_gettuple(argv, 1);
    if (NULL == pipe) janet_panic("cannot get pipe");
    wm->callback_info = (void *)pipe;

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
        JANET_REG("get-pipe", cfun_get_pipe),
        JANET_REG("read-event", cfun_read_event),
        JANET_REG("start", cfun_start),
        JANET_REG("stop", cfun_stop),
        JANET_REG("watching?", cfun_is_watching),
        JANET_REG_END
    });
}
