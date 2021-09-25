#include "../watchful.h"

#ifndef MACOS

WatchfulBackend watchful_fse = {
    .name = "fse",
    .setup = NULL,
    .teardown = NULL,
};

#else

static int compare_paths(char *p1, char *p2) {
    size_t p1_len = strlen(p1);
    size_t p2_len = strlen(p2);
    size_t min_len = p1_len < p2_len ? p1_len : p2_len;
    for (size_t i = 0; i < min_len; i++) {
        if (p1[i] != p2[i]) return 1;
    }

    return p2_len > p1_len ? -1 : 0;
}

static int setEventType(FSEventStreamEventFlags flags) {
    int event_type = 0;
    if (flags & kFSEventStreamEventFlagItemCreated)
        event_type = event_type | WFLAG_CREATED;
    if (flags & kFSEventStreamEventFlagItemRemoved)
        event_type = event_type | WFLAG_DELETED;
    if (flags & kFSEventStreamEventFlagItemRenamed)
        event_type = event_type | WFLAG_MOVED;
    if ((flags & kFSEventStreamEventFlagItemModified) ||
        (flags & kFSEventStreamEventFlagItemXattrMod) ||
        (flags & kFSEventStreamEventFlagItemInodeMetaMod))
        event_type = event_type | WFLAG_MODIFIED;
    return event_type;
}

static void callback(
    ConstFSEventStreamRef streamRef,
    void *clientCallBackInfo,
    size_t numEvents,
    void *eventPaths,
    const FSEventStreamEventFlags eventFlags[],
    const FSEventStreamEventId eventIds[])
{
    debug_print("Callback called\n");
    char **paths = (char **)eventPaths;
    watchful_stream_t *stream = (watchful_stream_t *)clientCallBackInfo;
    int event_type = 0;
    size_t uniques[numEvents];
    size_t num_uniques = 0;

    for (size_t i = 0; i < numEvents; i++) {
        int is_unique = 1;
        for (size_t j = 0; j < num_uniques; j++) {
            switch (compare_paths(paths[uniques[j]], paths[i])) {
                case -1:
                    uniques[j] = i;
                    is_unique = 0;
                    break;
                case 0:
                    is_unique = 0;
                    break;
                case 1:
                    break;
            }
            if (!is_unique) break;
        }

        if (is_unique) uniques[num_uniques++] = i;

        if (WATCHFUL_DEBUG) {
            event_type = setEventType(eventFlags[i]);
            debug_print("Event: ");

            if (event_type & WFLAG_CREATED)
                debug_print("[Created] ");
            if (event_type & WFLAG_DELETED)
                debug_print("[Deleted] ");
            if (event_type & WFLAG_MOVED)
                debug_print("[Moved] ");
            if (event_type & WFLAG_MODIFIED)
                debug_print("[Modified] ");

            debug_print("%s", paths[i]);

            if (eventFlags[i] & kFSEventStreamEventFlagItemIsDir) {
                debug_print(" [directory]\n");
            } else if (eventFlags[i] & kFSEventStreamEventFlagItemIsFile) {
                debug_print(" [file]\n");
            }
        }
    }

    for (size_t i = 0; i < num_uniques; i++) {
        size_t selection = uniques[i];
        debug_print("Unique path: %s\n", paths[selection]);
        event_type = setEventType(eventFlags[selection]);

        if (!event_type || !(stream->wm->events & event_type)) return;
        if (watchful_is_excluded(paths[selection], stream->wm->excludes)) return;

        watchful_event_t *event = (watchful_event_t *)malloc(sizeof(watchful_event_t));

        event->type = event_type;
        event->path = watchful_clone_string(paths[selection]);

        /* janet_thread_send(stream->parent, janet_wrap_pointer(event), 10); */
    }
}

static void *loop_runner(void *arg) {
    watchful_stream_t *stream = arg;

    stream->loop = CFRunLoopGetCurrent();

    FSEventStreamScheduleWithRunLoop(
        stream->ref,
        stream->loop,
        kCFRunLoopDefaultMode
    );

    FSEventStreamStart(stream->ref);

    debug_print("Entering the run loop...\n");
    CFRunLoopRun();
    debug_print("Leaving the run loop...\n");

    stream->loop = NULL;
    return NULL;
}

static int start_loop(watchful_stream_t *stream) {
    int error = 0;

    pthread_attr_t attr;
    error = pthread_attr_init(&attr);
    if (error) return 1;

    error = pthread_create(&stream->thread, &attr, loop_runner, stream);
    if (error) return 1;

    pthread_attr_destroy(&attr);
    return 0;
}

static int setup(watchful_stream_t *stream) {
    debug_print("Setting up...\n");

    FSEventStreamContext stream_context;
    memset(&stream_context, 0, sizeof(stream_context));
    stream_context.info = stream;

    CFStringRef path = CFStringCreateWithCString(NULL, (const char *)stream->wm->path, kCFStringEncodingUTF8);
    CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **)&path, 1, NULL);

    /* CFAbsoluteTime latency = stream->delay; /1* Latency in seconds *1/ */
    CFAbsoluteTime latency = 0;

    stream->ref = FSEventStreamCreate(
        NULL,
        &callback,
        &stream_context,
        pathsToWatch,
        kFSEventStreamEventIdSinceNow, /* Or a previous event ID */
        latency,
        /* kFSEventStreamCreateFlagNone /1* Flags explained in reference *1/ */
        /* kFSEventStreamCreateFlagFileEvents */
        kFSEventStreamCreateFlagWatchRoot | kFSEventStreamCreateFlagFileEvents
    );

    int error = start_loop(stream);
    CFRelease(pathsToWatch);
    CFRelease(path);
    if (error) return 1;

    return 0;
}

static int teardown(watchful_stream_t *stream) {
    debug_print("Tearing down...\n");

    if (stream->thread) {
        CFRunLoopStop(stream->loop);
        pthread_join(stream->thread, NULL);
    }

    FSEventStreamFlushSync(stream->ref);
    FSEventStreamStop(stream->ref);
    FSEventStreamInvalidate(stream->ref);
    FSEventStreamRelease(stream->ref);

    return 0;
}

watchful_backend_t watchful_fse = {
    .name = "fse",
    .setup = setup,
    .teardown = teardown,
};

#endif
