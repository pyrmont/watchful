#include "../watchful.h"

#ifndef LINUX

WatchfulBackend watchful_inotify = {
    .name = "inotify",
    .setup = NULL,
    .teardown = NULL,
};

#else

bool start_waiting;
pthread_mutex_t start_mutex;
pthread_cond_t start_cond;

/* Forward declarations */
static int add_watch(WatchfulMonitor *wm, char *path);
static int remove_watch(WatchfulMonitor *wm, WatchfulWatch *watch);

static int translate_event(const struct inotify_event *event) {
    if (event->cookie) {
        return (event->mask & IN_MOVED_FROM)  ? WATCHFUL_EVENT_RENAMED :
               (event->mask & IN_MOVED_TO)    ? WATCHFUL_EVENT_RENAMED : 0;
    } else {
        return (event->mask & IN_CREATE)      ? WATCHFUL_EVENT_CREATED :
               (event->mask & IN_DELETE)      ? WATCHFUL_EVENT_DELETED :
               (event->mask & IN_DELETE_SELF) ? WATCHFUL_EVENT_DELETED :
               (event->mask & IN_MOVED_TO)    ? WATCHFUL_EVENT_CREATED :
               (event->mask & IN_MOVED_FROM)  ? WATCHFUL_EVENT_DELETED :
               (event->mask & IN_MODIFY)      ? WATCHFUL_EVENT_MODIFIED :
               (event->mask & IN_ATTRIB)      ? WATCHFUL_EVENT_MODIFIED : 0;
    }
}

static WatchfulWatch *watch_for_wd(WatchfulMonitor *wm, int wd) {
    for (size_t i = 0; i < wm->watches_len; i++) {
        if (wd == wm->watches[i]->wd) return wm->watches[i];
    }

    return NULL;
}

static int handle_event(WatchfulMonitor *wm) {
    char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *notify_event;

    /* if (stream->delay) { */
    /*     int seconds = (int)stream->delay; */
    /*     int nanoseconds = (int)((stream->delay - seconds) * 100000000); */
    /*     struct timespec duration = { .tv_sec = seconds, .tv_nsec = nanoseconds, }; */
    /*     nanosleep(&duration, NULL); */
    /* } */

    int size = read(wm->fd, buf, sizeof(buf));
    if (size <= 0) return 1;

    char *path = NULL;
    char *old_path = NULL;
    WatchfulEvent *event = NULL;

    for (char *ptr = buf; ptr < buf + size; ptr += sizeof(struct inotify_event) + notify_event->len) {
        notify_event = (const struct inotify_event *)ptr;

        /* 1. Get watch for watch descriptor. */
        WatchfulWatch *watch = watch_for_wd(wm, notify_event->wd);
        if (NULL == watch) continue;

        /* 2. Set event_type for this event. */
        int event_type = translate_event(notify_event);

        /* 3. If event is excluded, skip. */
        if (!(event_type && (wm->events & event_type))) continue;

        /* 4. Create absolute path for file. */
        path = (notify_event->len) ?
            watchful_path_create(notify_event->name, watch->path, (notify_event->mask & IN_ISDIR != 0)) :
            watchful_path_create(watch->path, NULL, true);
        if (path == NULL) goto error;

        /* 5. If renaming, save old_path. */
        if (event_type == WATCHFUL_EVENT_RENAMED && NULL == old_path) {
            old_path = path;
            path = NULL;
            continue;
        }

        /* 6. If file path is not excluded. */
        if (!watchful_monitor_excludes_path(wm, path)) {
            /* 7. Add or remove watches as appropriate. */
            char *copied_path = NULL;
            int err = 0;
            switch (event_type) {
                case WATCHFUL_EVENT_CREATED:
                case WATCHFUL_EVENT_RENAMED:
                    copied_path = watchful_path_create(path, NULL, false);
                    if (NULL == copied_path) goto error;
                    if (watchful_path_is_dir(copied_path)) {
                        /* TODO: inotify man page recommends scanning and adding */
                        err = add_watch(wm, copied_path);
                        if (err) free(copied_path);
                    } else {
                        free(copied_path);
                    }
                    break;
                case WATCHFUL_EVENT_DELETED:
                    if (notify_event->mask & IN_DELETE_SELF) remove_watch(wm, watch);
                    break;
                default:
                    break;
            }

            /* 8. Call callback with event data. */
            event = malloc(sizeof(WatchfulEvent));
            if (NULL == event) goto error;
            event->type = event_type;
            event->path = path;
            event->old_path = old_path;
            wm->callback(event, wm->callback_info);
        }

        /* 9. Free memory. */
        free(path);
        path = NULL;
        free(old_path);
        old_path = NULL;
        if (NULL != event) {
            event->type = 0;
            event->path = NULL;
            event->old_path = NULL;
            free(event);
            event = NULL;
        }
    }

    free(path);
    free(old_path);
    if (NULL != event) {
        event->type = 0;
        event->path = NULL;
        event->old_path = NULL;
        free(event);
    }

    return 0;

error:
    free(path);
    free(old_path);
    if (NULL != event) {
        event->type = 0;
        event->path = NULL;
        event->old_path = NULL;
        free(event);
    }

    return 1;
}

static void *loop_runner(void *arg) {
    int error = 0;
    WatchfulMonitor *wm = arg;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    error = pthread_sigmask(SIG_BLOCK, &mask, NULL);
    if (error) return NULL;

    int sfd = signalfd(-1, &mask, 0);
    if (sfd == -1) return NULL;

    pthread_mutex_lock(&start_mutex);
    start_waiting = false;
    pthread_cond_signal(&start_cond);
    pthread_mutex_unlock(&start_mutex);

    while (1) {
        int nfds = ((wm->fd < sfd) ? sfd : wm->fd) + 1;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(wm->fd, &readfds);
        FD_SET(sfd, &readfds);

        select(nfds, &readfds, NULL, NULL, NULL);
        if (FD_ISSET(sfd, &readfds)) break;

        error = handle_event(wm);
        if (error) return NULL;
    }

    close(sfd);

    return NULL;
}

static int start_loop(WatchfulMonitor *wm) {
    int error = 0;

    pthread_attr_t attr;
    error = pthread_attr_init(&attr);
    if (error) return 1;

    start_waiting = true;
    pthread_mutex_init(&start_mutex, NULL);
    pthread_cond_init(&start_cond, NULL);

    error = pthread_create(&wm->thread, &attr, loop_runner, wm);
    if (error) return 1;

    pthread_mutex_lock(&start_mutex);
    while (start_waiting) pthread_cond_wait(&start_cond, &start_mutex);
    pthread_mutex_unlock(&start_mutex);

    pthread_mutex_destroy(&start_mutex);
    pthread_cond_destroy(&start_cond);

    pthread_attr_destroy(&attr);
    return 0;
}

static int remove_watch(WatchfulMonitor *wm, WatchfulWatch *watch) {
    if (watch->wd == -1) return 1;
    inotify_rm_watch(wm->fd, watch->wd);
    watch->wd = -1;
    return 0;
}

static int remove_watches(WatchfulMonitor *wm) {
    if (NULL == wm) return 1;

    for (size_t i = 0; i < wm->watches_len; i++) {
        remove_watch(wm, wm->watches[i]);
        free(wm->watches[i]->path);
        free(wm->watches[i]);
    }

    free(wm->watches);
    wm->watches = NULL;
    wm->watches_len = 0;

    return 0;
}

static int add_watch(WatchfulMonitor *wm, char *path) {
    WatchfulWatch *watch = malloc(sizeof(WatchfulWatch));
    if (NULL == watch) return 1;

    int inotify_events = IN_ATTRIB | IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVE;
    if (!(wm->events & WATCHFUL_EVENT_MODIFIED)) {
        inotify_events = inotify_events ^ IN_ATTRIB;
        inotify_events = inotify_events ^ IN_MODIFY;
    }
    if (!(wm->events & WATCHFUL_EVENT_CREATED))
        inotify_events = inotify_events ^ IN_CREATE;
    if (!(wm->events & WATCHFUL_EVENT_DELETED))
        inotify_events = inotify_events ^ IN_DELETE;
    if (!(wm->events & WATCHFUL_EVENT_RENAMED)) {
        inotify_events = inotify_events ^ IN_MOVED_TO;
        inotify_events = inotify_events ^ IN_MOVED_FROM;
    }

    watch->wd = inotify_add_watch(wm->fd, path, inotify_events);
    if (watch->wd == -1) goto error;
    watch->path = path;

    size_t len = wm->watches_len + 1;
    WatchfulWatch **new_watches = realloc(wm->watches, sizeof(WatchfulWatch *) * len);
    if (NULL == new_watches) goto error;
    wm->watches = new_watches;
    wm->watches[wm->watches_len] = watch;
    wm->watches_len = len;

    return 0;

error:
    free(watch);
    return 1;
}

static int add_watches(WatchfulMonitor *wm) {
    int err = 0;

    wm->watches_len = 0;
    wm->watches = NULL;

    /* This assumes that the path is a directory */
    char *path = watchful_path_create(wm->path, NULL, true);
    if (NULL == path) goto error;

    err = add_watch(wm, path);
    if (err) goto error;

    size_t paths_len = 1;
    size_t paths_max = 1;
    char **paths = malloc(sizeof(char *) * paths_max);
    if (NULL == paths) goto error;

    paths[0] = path;
    path = NULL;

    DIR *dir = NULL;
    for (size_t i = 0; i < paths_len; i++) {
        dir = opendir(paths[i]);
        if (NULL == dir) continue;

        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

            path = watchful_path_create(entry->d_name, paths[i], false);
            if (NULL == path) goto error;

            DIR *child_dir = opendir(path);
            if (NULL == child_dir || watchful_monitor_excludes_path(wm, path)) {
                free(path);
                continue;
            } else {
                closedir(child_dir);

                err = add_watch(wm, path);
                if (err) goto error;

                if (i + 1 == paths_max) {
                    char **new_paths = realloc(paths, sizeof(char *) * (paths_max * 2));
                    if (NULL == new_paths) goto error;
                    paths_max = paths_max * 2;
                    paths = new_paths;
                    new_paths = NULL;
                }

                paths[paths_len] = path;
                paths_len++;
            }
        }

        paths[i] = NULL;
        closedir(dir);
    }

    free(paths);

    return 0;

error:
    free(path);
    if (NULL != paths) {
        for (size_t i = 0; i < paths_len; i++) {
            if (NULL != paths[i]) free(paths[i]);
        }
        free(paths);
    }

    remove_watches(wm);

    closedir(dir);
    close(wm->fd);
    wm->fd = -1;

    return 1;
}

static int setup(WatchfulMonitor *wm) {
    int error = 0;

    wm->fd = inotify_init();
    if (wm->fd == -1) return 1;

    error = add_watches(wm);
    if (error) return 1;

    error = start_loop(wm);
    if (error) return 1;

    return 0;
}

static int teardown(WatchfulMonitor *wm) {
    int error = 0;

    if (!pthread_equal(wm->thread, pthread_self())) {
        error = pthread_kill(wm->thread, SIGUSR1);
        if (error) return 1;
        error = pthread_join(wm->thread, NULL);
        if (error) return 1;
        wm->thread = pthread_self();
    }

    error = remove_watches(wm);
    if (error) return 1;

    error = close(wm->fd);
    if (error) return 1;
    wm->fd = -1;

    return 0;
}

WatchfulBackend watchful_inotify = {
    .name = "inotify",
    .setup = setup,
    .teardown = teardown,
};

#endif
