#include "../watchful.h"

#ifndef LINUX

watchful_backend_t watchful_inotify = {
    .name = "inotify",
    .setup = NULL,
    .teardown = NULL,
};

#else

static char *path_for_wd(watchful_stream_t *stream, int wd) {
    for(size_t i = 0; i < stream->watch_num; i++) {
        if (wd == stream->watches[i]->wd)
            return (char *)stream->watches[i]->path;
    }

    return NULL;
}

static int handle_event(watchful_stream_t *stream) {
    debug_print("Event handler called\n");
    char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *notify_event;

    if (stream->delay) {
        int seconds = (int)stream->delay;
        int nanoseconds = (int)((stream->delay - seconds) * 100000000);
        struct timespec duration = { .tv_sec = seconds, .tv_nsec = nanoseconds, };
        nanosleep(&duration, NULL);
    }

    int size = read(stream->fd, buf, sizeof(buf));
    if (size <= 0) return 1;

    for (char *ptr = buf; ptr < buf + size; ptr += sizeof(struct inotify_event) + notify_event->len) {
        int event_type = 0;
        notify_event = (const struct inotify_event *)ptr;

        if (notify_event->mask & IN_CREATE) {
            event_type = WFLAG_CREATED;
        } else if (notify_event->mask & IN_DELETE) {
            event_type = WFLAG_DELETED;
        } else if (notify_event->mask & IN_MOVE) {
            event_type = WFLAG_MOVED;
        } else if ((notify_event->mask & IN_MODIFY) ||
                   (notify_event->mask & IN_ATTRIB)) {
            event_type = WFLAG_MODIFIED;
        }

        if (!event_type) continue;
        /* if (!event_type || !(stream->wm->events & event_type)) continue; */

        char *path_to_watch = path_for_wd(stream, notify_event->wd);
        if (path_to_watch == NULL) continue;

        char *path = (!notify_event->len) ?
            watchful_clone_string(path_to_watch) :
            watchful_extend_path(path_to_watch,
                                 (char *)notify_event->name,
                                 ((notify_event->mask & IN_ISDIR) ? 1 : 0));
        if (path == NULL) continue;

        if (watchful_is_excluded(path, stream->wm->excludes)) {
            free(path);
            continue;
        }

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

            if (notify_event->len)
                debug_print("%s", notify_event->name);
            else
                debug_print("%s", stream->wm->path);

            if (notify_event->mask & IN_ISDIR)
                debug_print(" [directory]\n");
            else
                debug_print(" [file]\n");
        }

        watchful_event_t *event = (watchful_event_t *)malloc(sizeof(watchful_event_t));

        event->type = event_type;
        event->path = path;

        janet_thread_send(stream->parent, janet_wrap_pointer(event), 10);
    }

    return 0;
}

static void *loop_runner(void *arg) {
    int error = 0;
    watchful_stream_t *stream = arg;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    error = pthread_sigmask(SIG_BLOCK, &mask, NULL);
    if (error) return NULL;

    int sfd = signalfd(-1, &mask, 0);
    if (sfd == -1) return NULL;

    debug_print("Entering the run loop...\n");
    while (1) {
        int nfds = ((stream->fd < sfd) ? sfd : stream->fd) + 1;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(stream->fd, &readfds);
        FD_SET(sfd, &readfds);

        select(nfds, &readfds, NULL, NULL, NULL);
        if (FD_ISSET(sfd, &readfds)) break;

        debug_print("Select complete\n");

        error = handle_event(stream);
        if (error) return NULL;

        debug_print("Read complete\n");
    }
    debug_print("Leaving the run loop...\n");

    close(sfd);

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

static int add_watches(watchful_stream_t *stream, char *path, DIR *dir) {
    debug_print("The number of watches is %ld\n", stream->watch_num);
    if (stream->watch_num == 0) {
        stream->watches = (watchful_watch_t **)malloc(sizeof(watchful_watch_t *));
    } else {
        watchful_watch_t **new_watches = (watchful_watch_t **)realloc(stream->watches, sizeof(*stream->watches) * (stream->watch_num + 1));
        if (new_watches == NULL) return 1;
        stream->watches = new_watches;
    }
    watchful_watch_t *watch = (watchful_watch_t *)malloc(sizeof(watchful_watch_t));
    stream->watches[stream->watch_num++] = watch;

    int events = IN_ATTRIB | IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVE;
    if (!(stream->wm->events & WFLAG_CREATED))
        events = events ^ IN_CREATE;
    if (!(stream->wm->events & WFLAG_DELETED))
        events = events ^ IN_DELETE;
    if (!(stream->wm->events & WFLAG_MOVED))
        events = events ^ IN_MOVE;
    if (!(stream->wm->events & WFLAG_MODIFIED)) {
        events = events ^ IN_ATTRIB;
        events = events ^ IN_MODIFY;
    }

    debug_print("Adding watch to %s...\n", path);
    watch->wd = inotify_add_watch(stream->fd, path, events);
    if (watch->wd == -1) return 1;
    debug_print("Watch added\n");

    watch->path = (const uint8_t *)watchful_clone_string(path);

    if (dir == NULL) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

        char *child_path = watchful_extend_path(path, entry->d_name, 1);

        DIR *dir = opendir(child_path);
        if (dir == NULL) {
            free(child_path);
            continue;
        }

        debug_print("Adding more watches...\n");
        int error = add_watches(stream, child_path, dir);
        free(child_path);
        closedir(dir);
        if (error) return 1;
    }

    return 0;
}

static int setup(watchful_stream_t *stream) {
    debug_print("Setting up...\n");
    int error = 0;

    stream->fd = inotify_init();
    if (stream->fd == -1) return 1;

    stream->watch_num = 0;

    char *path = watchful_clone_string((char *)stream->wm->path);
    DIR *dir = opendir(path);
    error = add_watches(stream, path, dir);
    free(path);
    if (dir) closedir(dir);
    if (error) return 1;

    error = start_loop(stream);
    if (error) return 1;

    return 0;
}

static int teardown(watchful_stream_t *stream) {
    debug_print("Tearing down...\n");
    int error = 0;

    if (stream->thread) {
        pthread_kill(stream->thread, SIGUSR1);
        pthread_join(stream->thread, NULL);
    }

    for (size_t i = 0; i < stream->watch_num; i++) {
        inotify_rm_watch(stream->fd, stream->watches[i]->wd);
        free((char *)stream->watches[i]->path);
        free(stream->watches[i]);
    }

    free(stream->watches);
    stream->watches = NULL;

    error = close(stream->fd);
    if (error) return 1;

    return 0;
}

watchful_backend_t watchful_inotify = {
    .name = "inotify",
    .setup = setup,
    .teardown = teardown,
};

#endif
