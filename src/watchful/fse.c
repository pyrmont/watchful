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
    JanetThread *parent = (JanetThread *)clientCallBackInfo;
    janet_thread_send(parent, janet_wrap_integer(5), 10);

    /* char **paths = eventPaths; */

    /* for (size_t i=0; i < numEvents; i++) { */
    /*     /1* int count; *1/ */
    /*     /1* flags are unsigned long, IDs are uint64_t *1/ */
    /*     printf("Change %llu in %s, flags %u\n", eventIds[i], paths[i], eventFlags[i]); */
    /* } */
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

    printf("Entering the run loop...\n");
    CFRunLoopRun();
    printf("Leaving the run loop...\n");

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
    printf("Setting up...\n");

    FSEventStreamContext stream_context;
    memset(&stream_context, 0, sizeof(stream_context));
    stream_context.info = stream->parent;

    CFStringRef path = CFStringCreateWithCString(NULL, (const char *)stream->wm->path, kCFStringEncodingUTF8);
	CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **)&path, 1, NULL);

	CFAbsoluteTime latency = 1.0; /* Latency in seconds */

    stream->ref = FSEventStreamCreate(
		NULL,
        &callback,
        &stream_context,
        pathsToWatch,
        kFSEventStreamEventIdSinceNow, /* Or a previous event ID */
        latency,
        kFSEventStreamCreateFlagNone /* Flags explained in reference */
    );

    int error = start_loop(stream);
    if (error) return 1;

    return 0;
}

static int teardown(watchful_stream_t *stream) {
    printf("Tearing down...\n");

    FSEventStreamStop(stream->ref);
    FSEventStreamInvalidate(stream->ref);
    FSEventStreamRelease(stream->ref);

    CFRunLoopStop(stream->loop);

    return 0;
}

watchful_backend_t watchful_fse = {
    .name = "fse",
    .setup = setup,
    .teardown = teardown,
};

#endif
