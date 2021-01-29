#include "../watchful.h"

#ifndef MACOS

watchful_backend_t watchful_fse = {
    .name = "fse",
    .setup = NULL,
    .teardown = NULL,
};

#else

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

    for (size_t i = 0; i < numEvents; i++) {
        int event_type = 0;
        if (eventFlags[i] & kFSEventStreamEventFlagItemCreated)
            event_type = event_type | WFLAG_CREATED;
        if (eventFlags[i] & kFSEventStreamEventFlagItemRemoved)
            event_type = event_type | WFLAG_DELETED;
        if (eventFlags[i] & kFSEventStreamEventFlagItemRenamed)
            event_type = event_type | WFLAG_MOVED;
        if ((eventFlags[i] & kFSEventStreamEventFlagItemModified) ||
            (eventFlags[i] & kFSEventStreamEventFlagItemXattrMod) ||
            (eventFlags[i] & kFSEventStreamEventFlagItemInodeMetaMod))
            event_type = event_type | WFLAG_MODIFIED;

        if (WATCHFUL_DEBUG) {
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

        if (!event_type || !(stream->wm->events & event_type)) continue;

        if (watchful_is_excluded(paths[i], stream->wm->excludes)) continue;

        watchful_event_t *event = (watchful_event_t *)malloc(sizeof(watchful_event_t));

        event->type = event_type;
        event->path = watchful_clone_string(paths[i]);

        janet_thread_send(stream->parent, janet_wrap_pointer(event), 10);
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

    CFAbsoluteTime latency = stream->delay; /* Latency in seconds */
    /* CFAbsoluteTime latency = 0; */

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
