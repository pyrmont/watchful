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
    watchful_monitor_t *monitor = (watchful_monitor_t *)p;
    janet_mark(janet_wrap_function(monitor->cb));
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
    janet_arity(argc, 3, 4);

    const uint8_t *choice = janet_getkeyword(argv, 0);
    watchful_backend_t *backend;
    if (!janet_cstrcmp(choice, "fse")) {
        backend = &watchful_fse;
    } else if (!janet_cstrcmp(choice, "inotify")) {
        backend = &watchful_inotify;
    } else {
        janet_panicf("backend :%s not found", choice);
    }

    if (backend->setup == NULL ||
        backend->watch == NULL ||
        backend->teardown == NULL) {
        janet_panicf("backend :%s is not supported on this platform", choice);
    }

    JanetFunction *cb = janet_getfunction(argv, 1);
    const uint8_t *path = janet_getstring(argv, 2);

    watchful_monitor_t *wm = (watchful_monitor_t *)janet_abstract(&watchful_monitor_type, sizeof(watchful_monitor_t));
    wm->backend = backend;
    wm->cb = cb;
    wm->path = path;

    wm->backend->setup(wm);
    printf("Setup complete\n");

    return janet_wrap_abstract(wm);
}

static Janet cfun_destroy(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);

    watchful_monitor_t *wm = (watchful_monitor_t *)janet_getabstract(argv, 0, &watchful_monitor_type);

    wm->backend->teardown(wm);
    printf("Teardown complete\n");

    return janet_wrap_nil();
}

static Janet cfun_watch(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);

    watchful_monitor_t *wm = (watchful_monitor_t *)janet_getabstract(argv, 0, &watchful_monitor_type);

    wm->backend->watch(wm);

    return janet_wrap_integer(0);
}

static const JanetReg cfuns[] = {
    {"create", cfun_create,
     "(watchful/create cb path &opt excludes)\n\n"
     "Create a monitor"},
    {"watch", cfun_watch,
     "(watchful/watch monitor)\n\n"
     "Watch with the monitor"
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
