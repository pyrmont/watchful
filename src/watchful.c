#include "watchful.h"

/* Deinitialising */

static int watchful_monitor_gc(void *p, size_t size) {
    (void) size;
    (void) p;
    return 0;
}

/* Marking */

static int watchful_monitor_mark(void *p, size_t size) {
    (void) size;
    (void) p;
    return 0;
}

/* Type Definition */

static const JanetAbstractType watchful_monitor_type = {
    "watchful/monitor",
    watchful_monitor_gc,
    watchful_monitor_mark,
    JANET_ATEND_GCMARK
};

/* C Functions */

static int watchful_option_count(JanetTuple head) {
    size_t head_size = janet_tuple_length(head);

    if (head_size > 1 && !janet_cstrcmp(janet_getkeyword(head, 1), "count"))
        return janet_getinteger(head, 2);

    if (head_size > 3 && !janet_cstrcmp(janet_getkeyword(head, 3), "count"))
        return janet_getinteger(head, 4);

    return INFINITY;
}

static double watchful_option_elapse(JanetTuple head) {
    size_t head_size = janet_tuple_length(head);

    if (head_size > 1 && !janet_cstrcmp(janet_getkeyword(head, 1), "elapse"))
        return janet_getnumber(head, 2);

    if (head_size > 3 && !janet_cstrcmp(janet_getkeyword(head, 3), "elapse"))
        return janet_getnumber(head, 4);

    return INFINITY;
}

static Janet cfun_create(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);

    /* Need to know backend first */
    watchful_backend_t *backend;
    if (argc == 3) {
        const uint8_t *choice = janet_getkeyword(argv, 2);
        if (!janet_cstrcmp(choice, "fse")) {
            backend = &watchful_fse;
        } else if (!janet_cstrcmp(choice, "inotify")) {
            backend = &watchful_inotify;
        } else {
            janet_panicf("backend :%s not found", choice);
        }

        if (backend->setup == NULL || backend->teardown == NULL) {
            janet_panicf("backend :%s is not supported on this platform", choice);
        }
    } else {
        backend = &watchful_default_backend;
    }

    const uint8_t *path = janet_getstring(argv, 0);

    watchful_monitor_t *wm = (watchful_monitor_t *)janet_abstract(&watchful_monitor_type, sizeof(watchful_monitor_t));
    wm->backend = backend;
    wm->path = path;

    return janet_wrap_abstract(wm);
}

static Janet cfun_watch(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);

    JanetTuple head = janet_gettuple(argv, 0);
    JanetFunction *cb = janet_getfunction(argv, 1);

    size_t head_size = janet_tuple_length(head);

    if (head_size == 0) {
        janet_panicf("missing path monitor");
    } else if (head_size % 2 == 0) {
        janet_panicf("missing option value");
    }

    watchful_monitor_t *wm = (watchful_monitor_t *)janet_getabstract(head, 0, &watchful_monitor_type);
    watchful_stream_t *stream = (watchful_stream_t *)malloc(sizeof(watchful_stream_t));
    stream->wm = wm;

    int count = watchful_option_count(head);
    double elapse = watchful_option_elapse(head);

    JanetCFunction cfun = janet_unwrap_cfunction(janet_resolve_core("thread/current"));
    stream->parent = (JanetThread *)janet_unwrap_abstract(cfun(0, NULL));

    wm->backend->setup(stream);
    printf("Setup complete\n");

    int counted = 0;
    double elapsed = 0.0;
    time_t start = time(0);
    while (counted < count && elapsed < elapse) {
        double timeout = (elapse == INFINITY) ? INFINITY : elapse;
        Janet out;
        int timed_out = janet_thread_receive(&out, timeout);
        if (!timed_out) {
            watchful_event_t *event = (watchful_event_t *)janet_unwrap_pointer(out);
            Janet tup[2] = {janet_cstringv(event->path), janet_wrap_integer(event->type)};
            JanetTuple args = janet_tuple_n(tup, 2);
            janet_pcall(cb, 2, (Janet *)args, &out, NULL);
            /* TODO: Catch the output and ensure memory is all freed */
            free(event);
        }
        counted++;
        time_t now = time(0);
        elapsed = (double)now - (double)start;
    }

    wm->backend->teardown(stream);
    printf("Teardown complete\n");

    stream->wm = NULL;
    stream->parent = NULL;
    free(stream);

    return janet_wrap_integer(0);
}

static const JanetReg cfuns[] = {
    {"create", cfun_create,
     "(watchful/create path &opt excludes backend)\n\n"
     "Create a monitor for `path`\n\n"
     "The monitor can optionally be created with `excludes`, an array or tuple "
     "of strings that are paths that the monitor will exclude, and `backend`, "
     "a keyword representing the API that the monitor will use (`:fse`, "
     "`:inotify`). If a backend is selected that is not supported, the function "
     "will panic."
    },
    {"watch", cfun_watch,
     "(watchful/watch [monitor & options] cb)\n\n"
     "Watch `monitor` and execute the function `cb` on changes\n\n"
     "The watch can optionally include `:count <integer>` and/or `:elapse "
     "<double>`. The integer after `:count` is the number of changes that "
     "should be monitored before the watch terminates. The double after "
     "`:elapse` is the number of seconds to wait until the watch terminates."
    },
    {NULL, NULL, NULL}
};

void watchful_register_watcher(JanetTable *env) {
    janet_cfuns(env, "watchful", cfuns);
}

JANET_MODULE_ENTRY(JanetTable *env) {
    watchful_register_watcher(env);
}
