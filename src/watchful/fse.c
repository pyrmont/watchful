#include "../watchful.h"

#ifndef MACOS

watchful_backend_t watchful_fse = {
    .name = "fse",
    .setup = NULL,
    .watch = NULL,
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
	JanetFunction *cb = (JanetFunction *)clientCallBackInfo;
	Janet argv[] = {};
	Janet out = janet_wrap_nil();
	JanetFiber *f = NULL;
	JanetSignal sig = janet_pcall(cb, 0, argv, &out, &f);
	if (sig != JANET_SIGNAL_OK && sig != JANET_SIGNAL_YIELD)
		janet_printf("top level signal(%d): %v\n", sig, out);

    /* char **paths = eventPaths; */

    /* printf("Callback called\n"); */
    /* for (size_t i=0; i < numEvents; i++) { */
    /*     /1* int count; *1/ */
    /*     /1* flags are unsigned long, IDs are uint64_t *1/ */
    /*     printf("Change %llu in %s, flags %u\n", eventIds[i], paths[i], eventFlags[i]); */
    /* } */
}

static int setup(watchful_monitor_t *wm) {
    printf("Setting up...\n");

    FSEventStreamContext stream_context;
    memset(&stream_context, 0, sizeof(stream_context));
    stream_context.info = wm->cb;

    CFStringRef path = CFStringCreateWithCString(NULL, (const char *)wm->path, kCFStringEncodingUTF8);
	CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **)&path, 1, NULL);

	CFAbsoluteTime latency = 1.0; /* Latency in seconds */

    FSEventStreamRef stream = FSEventStreamCreate(
		NULL,
        &callback,
        &stream_context,
        pathsToWatch,
        kFSEventStreamEventIdSinceNow, /* Or a previous event ID */
        latency,
        kFSEventStreamCreateFlagNone /* Flags explained in reference */
    );

    wm->fse_stream = stream;

    return 0;
}

static void *loop_runner(void *arg) {
    watchful_monitor_t *wm = arg;

    wm->fse_loop = CFRunLoopGetCurrent();

    FSEventStreamScheduleWithRunLoop(
        wm->fse_stream,
        wm->fse_loop,
        kCFRunLoopDefaultMode
    );

    FSEventStreamStart(wm->fse_stream);

    janet_init();

    printf("Entering the run loop...\n");
    CFRunLoopRun();
    printf("Leaving the run loop...\n");

    janet_deinit();

    wm->fse_loop = NULL;
    return NULL;
}

static int start_loop(watchful_monitor_t *wm) {
    int error = 0;

    pthread_attr_t attr;
    error = pthread_attr_init(&attr);
    if (error) return 1;

    error = pthread_create(&wm->thread, &attr, loop_runner, wm);
    if (error) return 1;

    pthread_attr_destroy(&attr);
    return 0;
}

static int watch(watchful_monitor_t *wm) {
    printf("Watching...\n");

    start_loop(wm);

    return 0;
}

static int teardown(watchful_monitor_t *wm) {
    printf("Tearing down...\n");

    FSEventStreamStop(wm->fse_stream);
    FSEventStreamInvalidate(wm->fse_stream);
    FSEventStreamRelease(wm->fse_stream);

    CFRunLoopStop(wm->fse_loop);

    wm->fse_stream = NULL;

    return 0;
}

watchful_backend_t watchful_fse = {
    .name = "fse",
    .setup = setup,
    .watch = watch,
    .teardown = teardown,
};

#endif
