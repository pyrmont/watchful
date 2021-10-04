#include "../watchful.h"

#ifndef MACOS

WatchfulBackend watchful_fsevents = {
    .name = "fsevents",
    .setup = NULL,
    .teardown = NULL,
};

#else

bool start_waiting;
pthread_mutex_t start_mutex;
pthread_cond_t start_cond;

static int timecmp(WatchfulTime *t1, WatchfulTime *t2) {
    if (t1->tv_sec > t2->tv_sec)
        return 1;
    else if (t1->tv_sec < t2->tv_sec)
        return -1;
    else if (t1->tv_nsec > t2->tv_nsec)
        return 1;
    else if (t1->tv_nsec < t2->tv_nsec)
        return -1;
    else
        return 0;
}

static bool is_historical_event(WatchfulMonitor *wm, char *path, int flag, int event) {
    struct stat buf;

    /* TODO: Consider handling of other events */
    if (event == WATCHFUL_EVENT_CREATED) {
        if (flag & kFSEventStreamEventFlagItemRemoved) return true;
        if (flag & kFSEventStreamEventFlagItemRenamed) return true;

        stat(path, &buf);
        if (timecmp(wm->start_time, &buf.st_birthtimespec) >= 0) return true;

        CFStringRef key = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
        WatchfulTime *val = (WatchfulTime *)CFDictionaryGetValue(wm->watches, key);
        if (NULL != val && timecmp(val, &buf.st_birthtimespec) >= 0) return true;

        WatchfulTime *old_val = val;
        val = malloc(sizeof(WatchfulTime));
        val->tv_sec = buf.st_birthtimespec.tv_sec;
        val->tv_nsec = buf.st_birthtimespec.tv_nsec;
        CFDictionarySetValue(wm->watches, key, val);

        free(old_val);
        CFRelease(key);

        return false;
    } else if (event == WATCHFUL_EVENT_DELETED) {
        return false;
    } else if (event == WATCHFUL_EVENT_MOVED) {
        return false;
    } else if (event == WATCHFUL_EVENT_MODIFIED) {
        return false;
    } else { /* Unreachable */
        return false;
    }
}

static void handle_event(
    ConstFSEventStreamRef streamRef,
    void *clientCallBackInfo,
    size_t numEvents,
    void *eventPaths,
    const FSEventStreamEventFlags eventFlags[],
    const FSEventStreamEventId eventIds[])
{
    char **paths = eventPaths;
    WatchfulMonitor *wm = clientCallBackInfo;

    for (size_t i = 0; i < numEvents; i++) {
        int event_type = 0;

        if ((eventFlags[i] & kFSEventStreamEventFlagItemCreated) &&
            !is_historical_event(wm, paths[i], eventFlags[i], WATCHFUL_EVENT_CREATED))
            event_type = event_type | WATCHFUL_EVENT_CREATED;
        else if ((eventFlags[i] & kFSEventStreamEventFlagItemRemoved) &&
                 !is_historical_event(wm, paths[i], eventFlags[i], WATCHFUL_EVENT_DELETED))
            event_type = event_type | WATCHFUL_EVENT_DELETED;
        else if ((eventFlags[i] & kFSEventStreamEventFlagItemRenamed) &&
                 !is_historical_event(wm, paths[i], eventFlags[i], WATCHFUL_EVENT_MOVED))
            event_type = event_type | WATCHFUL_EVENT_MOVED;
        else if (((eventFlags[i] & kFSEventStreamEventFlagItemModified) ||
                  (eventFlags[i] & kFSEventStreamEventFlagItemXattrMod) ||
                  (eventFlags[i] & kFSEventStreamEventFlagItemInodeMetaMod)) &&
                 !is_historical_event(wm, paths[i], eventFlags[i], WATCHFUL_EVENT_MODIFIED))
            event_type = event_type | WATCHFUL_EVENT_MODIFIED;

        if (!(event_type && (wm->events & event_type))) continue;
        if (watchful_monitor_excludes_path(wm, paths[i])) continue;

        WatchfulEvent *event = malloc(sizeof(WatchfulEvent));

        event->type = event_type;
        event->path = watchful_path_create(paths[i], NULL, eventFlags[i] & kFSEventStreamEventFlagItemIsDir);

        wm->callback(event, wm->callback_info);

        event->type = 0;
        free(event->path);
        free(event);
    }

    return;
}

static int end_loop(WatchfulMonitor *wm) {
    int error = 0;

    FSEventStreamFlushSync(wm->ref);
    FSEventStreamStop(wm->ref);

    FSEventStreamUnscheduleFromRunLoop(
        wm->ref,
        wm->loop,
        kCFRunLoopDefaultMode
    );

    FSEventStreamInvalidate(wm->ref);
    FSEventStreamRelease(wm->ref);

    if (wm->thread) {
        CFRunLoopStop(wm->loop);
        error = pthread_join(wm->thread, NULL);
        if (error) return 1;
        CFRelease(wm->loop);
    }

    return 0;
}

static void *loop_runner(void *arg) {
    WatchfulMonitor *wm = arg;

    wm->loop = CFRunLoopGetCurrent();
    wm->loop = (CFRunLoopRef)CFRetain(wm->loop);

    FSEventStreamScheduleWithRunLoop(
        wm->ref,
        wm->loop,
        kCFRunLoopDefaultMode
    );

    bool started = FSEventStreamStart(wm->ref);
    if (!started) return NULL;

    pthread_mutex_lock(&start_mutex);
    start_waiting = false;
    pthread_cond_signal(&start_cond);
    pthread_mutex_unlock(&start_mutex);

    CFRunLoopRun();

    return NULL;
}

static int start_loop(WatchfulMonitor *wm) {
    int error = 0;

    wm->start_time = malloc(sizeof(WatchfulTime));
    if (NULL == wm->start_time) return 1;
    error = clock_gettime(CLOCK_REALTIME, wm->start_time);
    if (error) return 1;

    FSEventStreamContext stream_context;
    memset(&stream_context, 0, sizeof(stream_context));
    stream_context.info = wm;

    CFStringRef path = CFStringCreateWithCString(NULL, wm->path, kCFStringEncodingUTF8);
    CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **)&path, 1, NULL);

    FSEventStreamCreateFlags fsevents_events = 0;
    fsevents_events = fsevents_events | kFSEventStreamCreateFlagWatchRoot;
    fsevents_events = fsevents_events | kFSEventStreamCreateFlagFileEvents;

    wm->ref = FSEventStreamCreate(
        NULL,
        handle_event,
        &stream_context,
        pathsToWatch,
        kFSEventStreamEventIdSinceNow,
        (CFAbsoluteTime)wm->delay,
        fsevents_events
    );

    CFRelease(pathsToWatch);
    CFRelease(path);

    pthread_attr_t attr;
    error = pthread_attr_init(&attr);
    if (error) return 1;

    start_waiting = true;
    pthread_mutex_init(&start_mutex, NULL);
    pthread_cond_init(&start_cond, NULL);

    error = pthread_create(&wm->thread, &attr, loop_runner, wm);
    if (error) return 1;

    pthread_mutex_lock(&start_mutex);
    while (start_waiting) {
        pthread_cond_wait(&start_cond, &start_mutex);
    }
    pthread_mutex_unlock(&start_mutex);

    pthread_mutex_destroy(&start_mutex);
    pthread_cond_destroy(&start_cond);
    pthread_attr_destroy(&attr);
    return 0;
}

static int remove_watches(WatchfulMonitor *wm) {
    if (NULL == wm) return 1;
    if (NULL == wm->watches) return 1;

    CFIndex count = CFDictionaryGetCount(wm->watches);
    WatchfulTime **times = malloc(sizeof(WatchfulTime *) * count);
    if (NULL == times) return 1;

    CFDictionaryGetKeysAndValues(wm->watches, NULL, (const void **)times);

    for (CFIndex i = 0; i < count; i++) free(times[i]);
    free(times);
    CFRelease(wm->watches);
    wm->watches = NULL;

    return 0;
}

static int add_watches(WatchfulMonitor *wm) {
    wm->watches = CFDictionaryCreateMutable(
        NULL,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        NULL
    );
    if (NULL == wm->watches) return 1;

    return 0;
}

static int setup(WatchfulMonitor *wm) {
    int error = 0;

    error = add_watches(wm);
    if (error) return 1;

    error = start_loop(wm);
    if (error) return 1;

    return 0;
}

static int teardown(WatchfulMonitor *wm) {
    int error = 0;

    error = end_loop(wm);
    if (error) return 1;

    error = remove_watches(wm);
    if (error) return 1;

    return 0;
}

WatchfulBackend watchful_fsevents = {
    .name = "fsevents",
    .setup = setup,
    .teardown = teardown,
};

#endif
