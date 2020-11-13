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

static Janet cfun_create(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);

    const uint8_t *choice = janet_getkeyword(argv, 0);
    watchful_backend_t *backend;
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

    const uint8_t *path = janet_getstring(argv, 1);

    watchful_monitor_t *wm = (watchful_monitor_t *)janet_abstract(&watchful_monitor_type, sizeof(watchful_monitor_t));
    wm->backend = backend;
    wm->path = path;

    return janet_wrap_abstract(wm);
}

static Janet cfun_destroy(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);

    return janet_wrap_nil();
}

static Janet cfun_watch(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);

    JanetTuple settings = janet_gettuple(argv, 0);
    JanetFunction *cb = janet_getfunction(argv, 1);

    watchful_monitor_t *wm = (watchful_monitor_t *)janet_getabstract(settings, 0, &watchful_monitor_type);
    watchful_stream_t *stream = (watchful_stream_t *)malloc(sizeof(watchful_stream_t));
    stream->wm = wm;

    JanetCFunction cfun = janet_unwrap_cfunction(janet_resolve_core("thread/current"));
    stream->parent = (JanetThread *)janet_unwrap_abstract(cfun(0, NULL));

    wm->backend->setup(stream);
    printf("Setup complete\n");

    Janet out;
    while (true) {
        int error = janet_thread_receive(&out, 5.0);
        if (error) break;
        janet_pcall(cb, 0, NULL, &out, NULL);
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
     "(watchful/create cb path &opt excludes)\n\n"
     "Create a monitor for `path`, excluding `excludes`"},
    {"watch", cfun_watch,
     "(watchful/watch [monitor & options] cb)\n\n"
     "Watch `monitor` and execute the function `cb` on changes"
    },
    {"destroy", cfun_destroy,
     "(watchful/destroy monitor)\n\n"
     "Destroy the monitor"},
    {NULL, NULL, NULL}
};

void watchful_register_watcher(JanetTable *env) {
    janet_cfuns(env, "watchful", cfuns);
}

JANET_MODULE_ENTRY(JanetTable *env) {
    watchful_register_watcher(env);
}
