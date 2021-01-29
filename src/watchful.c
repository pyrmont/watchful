#include "watchful.h"

/* Utility Functions */

char *watchful_clone_string(char *src) {
    int src_len = strlen(src);
    char *dest = (char *)malloc(sizeof(char) * (src_len + 1));
    if (dest == NULL) return NULL;
    memcpy(dest, src, src_len);
    dest[src_len] = '\0';
    return dest;
}

char *watchful_extend_path(char *path, char *name, int is_dir) {
    int path_len = strlen(path);
    int name_len = strlen(name);
    int new_path_len = path_len + name_len + (is_dir ? 1 : 0);
    char *new_path = (char *)malloc(sizeof(char) * (new_path_len + 1));
    if (new_path == NULL) return NULL;
    memcpy(new_path, path, path_len);
    memcpy(new_path + path_len, name, name_len);
    if (is_dir) new_path[new_path_len - 1] = '/';
    new_path[new_path_len] = '\0';
    return new_path;
}

int watchful_is_excluded(char *path, watchful_excludes_t *excludes) {
    if (excludes->len == 0) return 0;
    for (size_t i = 0; i < (size_t)excludes->len; i++) {
        const char *exclude = (const char *)excludes->paths[i];
        if (wildmatch(exclude, path, WM_WILDSTAR) == WM_MATCH) return 1;
    }
    return 0;
}

/* Deinitialising */

static int watchful_monitor_gc(void *p, size_t size) {
    (void) size;
    watchful_monitor_t *monitor = (watchful_monitor_t *)p;
    if (monitor->path != NULL) {
        free((uint8_t *)monitor->path);
        monitor->path = NULL;
    }
    if (monitor->excludes != NULL) {
        for (size_t i = 0; i < monitor->excludes->len; i++) {
            free((char *)monitor->excludes->paths[i]);
        }
        free(monitor->excludes);
        monitor->excludes = NULL;
    }
    return 0;
}

/* Marking */

static int watchful_monitor_mark(void *p, size_t size) {
    (void) size;
    watchful_monitor_t *monitor = (watchful_monitor_t *)p;

    Janet wrapped_path = janet_wrap_string(monitor->path);
    janet_mark(wrapped_path);

    return 0;
}

/* Type Definition */

static const JanetAbstractType watchful_monitor_type = {
    "watchful/monitor",
    watchful_monitor_gc,
    watchful_monitor_mark,
    JANET_ATEND_GCMARK
};

/* Helper Functions */

static int watchful_check_path(const uint8_t *path, int *is_dir) {
#ifdef JANET_WINDOWS
    struct _stat st;
    int error = _stat((char *)path, &st);
    if (error) return 1;
    if (st.st_mode & _S_IFDIR) *is_dir = 1;
#else
    struct stat st;
    int error = stat((char *)path, &st);
    if (error) return 1;
    if (S_ISDIR(st.st_mode)) *is_dir = 1;
#endif
    return 0;
}

static JanetThread *watchful_current_thread() {
    JanetCFunction cfun = janet_unwrap_cfunction(janet_resolve_core("thread/current"));
    return (JanetThread *)janet_unwrap_abstract(cfun(0, NULL));
}

static double watchful_option_number(JanetTuple head, char *name, double dflt) {
    size_t head_size = (head == NULL) ? 0 : janet_tuple_length(head);

    for (size_t i = 0; i < head_size; i += 2) {
        if (!janet_cstrcmp(janet_getkeyword(head, i), name))
            return janet_getnumber(head, i + 1);
    }

    return dflt;
}

static int watchful_set_events(watchful_monitor_t *wm, JanetView exclude_events) {
    wm->events = WFLAG_ALL;
    if (exclude_events.len == 0) return 0;

    for (size_t i = 0; i < (size_t)exclude_events.len; i++) {
        const uint8_t *choice = janet_getkeyword(exclude_events.items, i);
        if (!janet_cstrcmp(choice, "created")) {
            wm->events = wm->events ^ WFLAG_CREATED;
        } else if (!janet_cstrcmp(choice, "deleted")) {
            wm->events = wm->events ^ WFLAG_DELETED;
        } else if (!janet_cstrcmp(choice, "moved")) {
            wm->events = wm->events ^ WFLAG_MOVED;
        } else if (!janet_cstrcmp(choice, "modified")) {
            wm->events = wm->events ^ WFLAG_MODIFIED;
        } else {
            return 1;
        }
    }

    return 0;
}

static int watchful_set_excludes(watchful_monitor_t *wm, JanetView exclude_paths) {
    wm->excludes = (watchful_excludes_t *)malloc(sizeof(watchful_excludes_t));
    if (wm->excludes == NULL) return 1;
    wm->excludes->paths = NULL;
    wm->excludes->len = 0;

    if (exclude_paths.len == 0) return 0;

    for (size_t i = 0; i < (size_t)exclude_paths.len; i++) {
        if (i == 0) {
            wm->excludes->paths = (char **)malloc(sizeof(char *));
            if (wm->excludes->paths == NULL) return 1;
        } else {
            char **new_paths = (char **)realloc(wm->excludes->paths, sizeof(char *) * (i + 1));
            if (new_paths == NULL) return 1;
            wm->excludes->paths = new_paths;
        }
        char *exclude_path = (char *)janet_getstring(exclude_paths.items, i);
        if (exclude_path[0] == '/') {
            wm->excludes->paths[i] = watchful_clone_string(exclude_path);
        } else {
            wm->excludes->paths[i] = watchful_extend_path((char *)wm->path, exclude_path, 0);
        }
        if (wm->excludes->paths[i] == NULL) return 1;
        wm->excludes->len = i + 1;
    }

    return 0;
}

static int watchful_set_path(watchful_monitor_t *wm, const uint8_t *path, size_t max_len, int is_dir) {
    size_t path_len = strlen((char *)path);

    if (path_len > max_len) return 1;

    char *buf = (char *)malloc(sizeof(char) * max_len);
    size_t buf_len = 0;

    if (path[0] == '/') {
        memcpy(buf, path, path_len);
        if (path[path_len - 1] == '/') path_len--;
    } else {
#ifdef JANET_WINDOWS
        _getcwd(buf, max_len);
#else
        getcwd(buf, max_len);
#endif
        size_t cwd_len = strlen(buf);
        buf[cwd_len] = '/';
        buf_len = cwd_len + 1;
        memcpy(buf + buf_len, path, path_len);
    }

    if (buf_len + path_len + 2 > max_len) {
        free(buf);
        return 1;
    }

    if (is_dir) {
        buf[buf_len + path_len] = '/';
        buf[buf_len + path_len + 1] = '\0';
    } else {
        buf[buf_len + path_len] = '\0';
    }
    debug_print("The path being watched is %s\n", buf);

    wm->path = (const uint8_t *)buf;
    return 0;
}

static const Janet *watchful_tuple_event_types(int types) {
    Janet values[4];
    int i = 0;

    if (types & WFLAG_CREATED)
        values[i++] = janet_ckeywordv("created");
    if (types & WFLAG_DELETED)
        values[i++] = janet_ckeywordv("deleted");
    if (types & WFLAG_MOVED)
        values[i++] = janet_ckeywordv("moved");
    if (types & WFLAG_MODIFIED)
        values[i++] = janet_ckeywordv("modified");

    const Janet *result = (i == 0) ? janet_tuple_n(NULL, i) :
                                     janet_tuple_n(values, i);
    janet_tuple_flag(result) |= JANET_TUPLE_FLAG_BRACKETCTOR;
    return result;
}

/* Exposed Functions */

static Janet cfun_create(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 4);

    const uint8_t *path = janet_getstring(argv, 0);

    JanetView exclude_paths;
    if (argc >= 2) {
        exclude_paths = janet_getindexed(argv, 1);
    } else {
        exclude_paths.items = NULL;
        exclude_paths.len = 0;
    }

    JanetView exclude_events;
    if (argc >= 3) {
        exclude_events = janet_getindexed(argv, 2);
    } else {
        exclude_events.items = NULL;
        exclude_events.len = 0;
    }

    watchful_backend_t *backend = NULL;
    if (argc == 4) {
        const uint8_t *choice = janet_getkeyword(argv, 3);
        if (!janet_cstrcmp(choice, "fse")) {
            backend = &watchful_fse;
        } else if (!janet_cstrcmp(choice, "inotify")) {
            backend = &watchful_inotify;
        } else {
            janet_panicf("backend :%s not found", choice);
        }
    } else {
        backend = &watchful_default_backend;
    }

    if (backend->setup == NULL || backend->teardown == NULL) {
        janet_panicf("backend :%s is not supported on this platform", backend->name);
    }

    watchful_monitor_t *wm = (watchful_monitor_t *)janet_abstract(&watchful_monitor_type, sizeof(watchful_monitor_t));
    wm->backend = backend;
    wm->path = NULL;
    wm->excludes = NULL;
    wm->events = 0;

    int error = 0;
    int is_dir = 0;
    error = watchful_check_path(path, &is_dir);
    if (error) janet_panic("invalid path");

    error = watchful_set_path(wm, path, 1024, is_dir);
    if (error) janet_panic("path too long");

    error = watchful_set_excludes(wm, exclude_paths);
    if (error) janet_panic("cannot copy excluded paths");

    error = watchful_set_events(wm, exclude_events);
    if (error) janet_panic("invalid keywords in events to exclude");

    return janet_wrap_abstract(wm);
}

static Janet cfun_watch(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 5);

    watchful_monitor_t *wm = (watchful_monitor_t *)janet_getabstract(argv, 0, &watchful_monitor_type);
    JanetFunction *event_cb = janet_getfunction(argv, 1);
    JanetTuple head = (argc >= 3) ? janet_gettuple(argv, 2) : NULL;
    double delay = (argc >= 4) ? janet_getnumber(argv, 3) : 1.0;
    JanetFunction *ready_cb = (argc == 5) ? janet_getfunction(argv, 4) : NULL;

    if (argc >= 3 && janet_tuple_length(head) % 2 != 0) janet_panicf("missing option value");

    double count = watchful_option_number(head, "count", INFINITY);
    double elapse = watchful_option_number(head, "elapse", INFINITY);

    watchful_stream_t *stream = (watchful_stream_t *)malloc(sizeof(watchful_stream_t));
    stream->wm = wm;
    stream->parent = watchful_current_thread();
    stream->delay = delay;

    wm->backend->setup(stream);
    debug_print("Setup complete\n");

    if (ready_cb != NULL) {
        JanetTuple args = janet_tuple_n(NULL, 0);
        JanetFiber *ready_f = janet_fiber(ready_cb, 64, 0, args);
        ready_f->env = (janet_current_fiber())->env;
        Janet out;
        JanetSignal sig = janet_continue(ready_f, janet_wrap_nil(), &out);
        if (sig != JANET_SIGNAL_OK && sig != JANET_SIGNAL_YIELD) {
            janet_stacktrace(ready_f, out);
            janet_printf("top level signal(%d): %v\n", sig, out);
        }
        debug_print("Called ready callback\n");
    }

    double counted = 0.0;
    double elapsed = 0.0;
    double delayed = 0.0;
    time_t start = time(0);
    time_t last_run = 0;
    while (counted < count && elapsed < elapse) {
        Janet out;
        double timeout = (elapse == INFINITY) ? INFINITY : (elapse - elapsed);
        int timed_out = janet_thread_receive(&out, timeout);
        time_t now = time(0);
        delayed = (double)now - (double)last_run;
        if (!timed_out && delayed >= delay) {
            watchful_event_t *event = (watchful_event_t *)janet_unwrap_pointer(out);
            Janet const *event_types = watchful_tuple_event_types(event->type);
            Janet tup[2] = {janet_cstringv(event->path), janet_wrap_tuple(event_types)};
            JanetTuple args = janet_tuple_n(tup, 2);
            JanetFiber *event_f = janet_fiber(event_cb, 64, 2, args);
            event_f->env = (janet_current_fiber())->env;
            JanetSignal sig = janet_continue(event_f, janet_wrap_nil(), &out);
            if (sig != JANET_SIGNAL_OK && sig != JANET_SIGNAL_YIELD) {
                janet_stacktrace(event_f, out);
                janet_printf("top level signal(%d): %v\n", sig, out);
            }
            free(event->path);
            free(event);
            last_run = now;
        }
        counted++;
        elapsed = (double)now - (double)start;
    }

    wm->backend->teardown(stream);
    debug_print("Teardown complete\n");

    stream->wm = NULL;
    stream->parent = NULL;
    free(stream);
    debug_print("Freed stream\n");

    return janet_wrap_integer(0);
}

static const JanetReg cfuns[] = {
    {"create", cfun_create,
     "(watchful/create path &opt ignored-paths ignored-events backend)\n\n"
     "Create a monitor for `path`\n\n"
     "By default, Watchful will watch for creation, deletion, movement and "
     "modification events. In addition to the path, the user can optionally "
     "set:\n\n"
     "- `ignored-paths`: (array/tuple) events on paths that include one of these "
     "strings are ignored\n\n"
     "- `ignored-events`: (array/tuple) events that match one of these events "
     "(`:created`, `:deleted`, `:moved` and `:modified`) are ignored\n\n"
     "- `backend`: (keyword) a keyword denoting the backend to use, one of "
     "`:fse`, `:inotify`\n\n"
     "If a backend is selected that is not supported, the function will panic."
    },
    {"watch", cfun_watch,
     "(watchful/watch monitor on-event &opt conditions freq on-ready)\n\n"
     "Watch `monitor` and call the function `on-event` on file system events "
     "with optional termination conditions and an on-ready callback\n\n"
     "The `on-event` callback is a function that takes two arguments: `path` "
     "is the path of the file triggering the event and `event-types` is a "
     "tuple of the event types that occurred.\n\n"
     "Supported termination conditons are `:count <integer>` and "
     "`:elapse <double>`. The integer after `:count` is the number of events "
     "that will be monitored before the watch terminates. The double after "
     "`:elapse` is the number of seconds to wait until the watch terminates. "
     "If both conditions are provided, the watch will terminate when the "
     "earlier condition is met.\n\n"
     "The `on-ready` callback is a function that takes no arguments and is "
     "called after the watch begins."
    },
    {NULL, NULL, NULL}
};

void watchful_register_watcher(JanetTable *env) {
    janet_cfuns(env, "watchful", cfuns);
}

JANET_MODULE_ENTRY(JanetTable *env) {
    watchful_register_watcher(env);
}
