#include "wrapper.h"

static void ev_callback(JanetEVGenericMessage msg) {
    size_t pairs_len = 3;
    WatchfulEvent *event = msg.argp;
    JanetKV *st = janet_struct_begin(pairs_len);

    Janet event_type;
    switch (event->type) {
        case WATCHFUL_EVENT_MODIFIED:
            event_type = janet_ckeywordv("modified");
            break;
        case WATCHFUL_EVENT_CREATED:
            event_type = janet_ckeywordv("created");
            break;
        case WATCHFUL_EVENT_DELETED:
            event_type = janet_ckeywordv("deleted");
            break;
        case WATCHFUL_EVENT_RENAMED:
            event_type = janet_ckeywordv("renamed");
            break;
        default:
            event_type = janet_wrap_nil();
    }

    janet_struct_put(st, janet_ckeywordv("type"), event_type);
    janet_struct_put(st, janet_ckeywordv("path"), janet_cstringv(event->path));
    if (NULL != event->old_path) {
        janet_struct_put(st, janet_ckeywordv("old-path"), janet_cstringv(event->old_path));
    }

    free(event->path);
    free(event->old_path);
    free(event);

    JanetFunction *give = janet_unwrap_function(msg.argj);
    Janet args[1] = { janet_wrap_struct(janet_struct_end(st)) };
    Janet result;
    janet_pcall(give, 1, args, &result, NULL);

    return;
}

static WatchfulEvent *copy_event(const WatchfulEvent *src) {
    WatchfulEvent *event = malloc(sizeof(WatchfulEvent));
    if (NULL == event) return NULL;

    event->type = src->type;
    event->path = NULL;
    event->old_path = NULL;

    event->path = malloc(sizeof(char) * (strlen(src->path) + 1));
    if (NULL == event->path) goto error;
    strcpy(event->path, src->path);

    if (NULL == src->old_path) {
        event->old_path = NULL;
    } else {
        event->old_path = malloc(sizeof(char) * (strlen(src->old_path) + 1));
        if (NULL == event->old_path) goto error;
        strcpy(event->old_path, src->old_path);
    }

    return event;

error:
    free(event->path);
    free(event->old_path);
    free(event);

    return NULL;
}

static int monitor_callback(const WatchfulEvent *event, void *info) {
    CallbackInfo *callback_info = (CallbackInfo *)info;

    WatchfulEvent *copy = copy_event(event);
    if (NULL == copy) return 1;

    JanetEVGenericMessage msg = {0};
    msg.argp = (void *)copy;
    msg.argj = janet_wrap_function(callback_info->fn);

    janet_ev_post_event(callback_info->vm, ev_callback, msg);

    return 0;
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
            if (!janet_cstrcmp(excl_event, "modified"))
                events = events ^ WATCHFUL_EVENT_MODIFIED;
            else if (!janet_cstrcmp(excl_event, "created"))
                events = events ^ WATCHFUL_EVENT_CREATED;
            else if (!janet_cstrcmp(excl_event, "deleted"))
                events = events ^ WATCHFUL_EVENT_DELETED;
            else if (!janet_cstrcmp(excl_event, "renamed"))
                events = events ^ WATCHFUL_EVENT_RENAMED;
            else
                janet_panicf("%j is not an ignorable event", vals[i]);
        }
    }

    double delay = 0;

    WatchfulMonitor *wm = janet_abstract(&watchful_monitor_type, sizeof(WatchfulMonitor));
    int error = watchful_monitor_init(wm, backend, path, excl_paths_len, excl_paths, events, delay, monitor_callback, NULL);
    if (error) janet_panic("cannot initialise monitor");

    if (NULL != excl_paths) janet_sfree(excl_paths);

    return janet_wrap_abstract(wm);
}

JANET_FN(cfun_is_watching,
        "(_watchful/watching? monitor)",
        "Native function for checking if monitor is running") {
    janet_fixarity(argc, 1);

    WatchfulMonitor *wm = janet_getabstract(argv, 0, &watchful_monitor_type);

    return janet_wrap_boolean((int)wm->is_watching);
}

JANET_FN(cfun_start,
        "(_watchful/start monitor give-fn)",
        "Native function for starting a watch") {
    janet_fixarity(argc, 2);

    WatchfulMonitor *wm = janet_getabstract(argv, 0, &watchful_monitor_type);

    JanetFunction *give_fn = janet_unwrap_function(argv[1]);
    if (NULL == give_fn) janet_panic("cannot get give function");

    CallbackInfo *callback_info = malloc(sizeof(CallbackInfo));
    if (NULL == callback_info) janet_panic("cannot create callback info");
    callback_info->vm = janet_local_vm();
    callback_info->fn = give_fn;
    wm->callback_info = callback_info;

    int err = watchful_monitor_start(wm);
    if (err) janet_panic("failed to start monitor cleanly");

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
        JANET_REG("start", cfun_start),
        JANET_REG("stop", cfun_stop),
        JANET_REG("watching?", cfun_is_watching),
        JANET_REG_END
    });
}
