#include "wrapper.h"

static int event_callback(const WatchfulEvent *event, void *info) {
    /* 1. Ignore sigpipe. */
    signal(SIGPIPE, SIG_IGN);

    /* 2. Get pipe. */
    JanetTuple pipe = (JanetTuple)info;
    JanetStream *input = janet_unwrap_abstract(pipe[1]);

    /* 3. Get handle. */
    int handle = (int)(input->handle);

    ssize_t written_bytes = 0;

    /* 4. Send the path length. */
    size_t null_byte = 0;
    size_t len = strlen(event->path);
    size_t max = 255;
    size_t loops = len / max;
    size_t rem = len % max;
    for (size_t i = 0; i < loops; i++)  {
        written_bytes = write(handle, &max, 1);
        if (written_bytes != 1) goto error;
    }
    if (rem) {
        written_bytes = write(handle, &rem, 1);
        if (written_bytes != 1) goto error;
    }
    written_bytes = write(handle, &null_byte, 1);
    if (written_bytes != 1) goto error;

    /* 5. Send the event path. */
    written_bytes = write(handle, event->path, len);
    if (written_bytes != len) goto error;

    /* 6. Send the event type. */
    written_bytes = write(handle, &(event->type), 1);
    if (written_bytes != 1) goto error;

    /* 7. Reset signal. */
    signal(SIGPIPE, SIG_DFL);

    return 0;

error:
    janet_stream_close(input);
    return 1;
}

/* Exposed Functions */

JANET_FN(cfun_monitor,
        "(_watchful/monitor path opts)",
        "Native function for creating a monitor") {
    janet_fixarity(argc, 2);

    WatchfulBackend *backend = NULL;

    const char *path = janet_getcstring(argv, 0);
    if (NULL == path) janet_panic("cannot get path");
    if (!watchful_path_is_dir(path)) janet_panic("path is not a directory");

    JanetStruct empty = janet_struct_end(janet_struct_begin(0));
    JanetStruct opts = janet_optstruct(argv, argc, 1, empty);

    size_t excl_paths_len = 0;
    const char **excl_paths = NULL;
    Janet excluded_paths = janet_struct_get(opts, janet_ckeywordv("ignored-paths"));
    if (!janet_checktype(excluded_paths, JANET_NIL)) {
        if (!janet_checktypes(excluded_paths, JANET_TFLAG_INDEXED)) janet_panic("ignored-paths option must be array or tuple");
        const Janet *vals = NULL;
        janet_indexed_view(excluded_paths, &vals, (int32_t *)&excl_paths_len);
        excl_paths = janet_smalloc(sizeof(const char *) * excl_paths_len);
        for (size_t i = 0; i < excl_paths_len; i++) {
            excl_paths[i] = (const char *)janet_unwrap_string(vals[i]);
        }
    }

    int events = WATCHFUL_EVENT_ALL;
    Janet excluded_events = janet_struct_get(opts, janet_ckeywordv("ignored-events"));
    if (!janet_checktype(excluded_events, JANET_NIL)) {
        if (!janet_checktypes(excluded_events, JANET_TFLAG_INDEXED)) janet_panic("ignored-events option must be array or tuple");
        const Janet *vals = NULL;
        size_t excl_events_len = 0;
        janet_indexed_view(excluded_events, &vals, (int32_t *)&excl_events_len);
        for (size_t i = 0; i < excl_events_len; i++) {
            JanetString excl_event = janet_unwrap_keyword(vals[i]);
            if (!janet_cstrcmp(excl_event, "created"))
                events = events ^ WATCHFUL_EVENT_CREATED;
            else if (!janet_cstrcmp(excl_event, "deleted"))
                events = events ^ WATCHFUL_EVENT_DELETED;
            else if (!janet_cstrcmp(excl_event, "moved"))
                events = events ^ WATCHFUL_EVENT_MOVED;
            else if (!janet_cstrcmp(excl_event, "modified"))
                events = events ^ WATCHFUL_EVENT_MODIFIED;
            else
                janet_panicf("%j is not an ignorable event", vals[i]);
        }
    }

    double delay = 0;

    WatchfulMonitor *wm = janet_abstract(&watchful_monitor_type, sizeof(WatchfulMonitor));
    int error = watchful_monitor_init(wm, backend, path, excl_paths_len, excl_paths, events, delay, event_callback, NULL);
    if (error) janet_panic("cannot initialise monitor");

    if (NULL != excl_paths) janet_sfree(excl_paths);

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
        JANET_REG("start", cfun_start),
        JANET_REG("stop", cfun_stop),
        JANET_REG("watching?", cfun_is_watching),
        JANET_REG_END
    });
}
