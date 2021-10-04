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

static char *path_for_wd(WatchfulMonitor *wm, int wd) {
    for (size_t i = 0; i < wm->watches_len; i++) {
        if (wd == wm->watches[i]->wd) return wm->watches[i]->path;
    }

    return NULL;
}

static int handle_event(WatchfulMonitor *wm) {
    /* debug_print("Event handler called\n"); */
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

    for (char *ptr = buf; ptr < buf + size; ptr += sizeof(struct inotify_event) + notify_event->len) {
        int event_type = 0;
        notify_event = (const struct inotify_event *)ptr;

        if (notify_event->mask & IN_CREATE) {
            event_type = WATCHFUL_EVENT_CREATED;
        } else if (notify_event->mask & IN_DELETE) {
            event_type = WATCHFUL_EVENT_DELETED;
        } else if (notify_event->mask & IN_MOVE) {
            event_type = WATCHFUL_EVENT_MOVED;
        } else if ((notify_event->mask & IN_MODIFY) ||
                   (notify_event->mask & IN_ATTRIB)) {
            event_type = WATCHFUL_EVENT_MODIFIED;
        }

        /* TODO: Is this logic right? */
        if (!event_type || !(wm->events & event_type)) continue;

        char *path_to_watch = path_for_wd(wm, notify_event->wd);
        if (NULL == path_to_watch) continue;

        char *path;
        if (notify_event->len) {
            path = watchful_path_create(notify_event->name, path_to_watch, (notify_event->mask & IN_ISDIR != 0));
        } else {
            path = watchful_path_create(path_to_watch, NULL, true);
        }
        if (path == NULL) return 1;

        if (watchful_monitor_excludes_path(wm, path)) {
            free(path);
            continue;
        }

        WatchfulEvent *event = malloc(sizeof(WatchfulEvent));

        event->type = event_type;
        event->path = path;

        wm->callback(event, wm->callback_info);

        event->type = 0;
        free(event->path);
        free(event);
    }

    return 0;
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

static int remove_watches(WatchfulMonitor *wm) {
    if (NULL == wm) return 1;

    for (size_t i = 0; i < wm->watches_len; i++) {
        inotify_rm_watch(wm->fd, wm->watches[i]->wd);
        free(wm->watches[i]->path);
        free(wm->watches[i]);
    }

    free(wm->watches);
    wm->watches = NULL;
    wm->watches_len = 0;

    return 0;
}

static WatchfulWatch *add_watch(int fd, char *path, int events) {
    WatchfulWatch *watch = malloc(sizeof(WatchfulWatch));
    if (NULL == watch) return NULL;

    int inotify_events = IN_ATTRIB | IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVE;
    if (!(events & WATCHFUL_EVENT_CREATED))
        inotify_events = inotify_events ^ IN_CREATE;
    if (!(events & WATCHFUL_EVENT_DELETED))
        inotify_events = inotify_events ^ IN_DELETE;
    if (!(events & WATCHFUL_EVENT_MOVED))
        inotify_events = inotify_events ^ IN_MOVE;
    if (!(events & WATCHFUL_EVENT_MODIFIED)) {
        inotify_events = inotify_events ^ IN_ATTRIB;
        inotify_events = inotify_events ^ IN_MODIFY;
    }

    watch->wd = inotify_add_watch(fd, path, inotify_events);
    if (watch->wd == -1) goto error;
    watch->path = path;

    return watch;

error:
    free(watch);
    return NULL;
}

static int add_watches(WatchfulMonitor *wm) {
    wm->watches_len = 0;
    wm->watches = NULL;

    /* This assumes that the path is a directory */
    char *path = watchful_path_create(wm->path, NULL, true);
    if (NULL == path) goto error;

    WatchfulWatch *watch = add_watch(wm->fd, path, wm->events);
    if (NULL == watch) goto error;

    wm->watches = malloc(sizeof(WatchfulWatch *));
    if (NULL == wm->watches) goto error;
    wm->watches[0] = watch;
    wm->watches_len++;
    watch = NULL;

    size_t paths_len = 1;
    size_t paths_max = 1;
    char **paths = malloc(sizeof(char *) * paths_max);
    if (NULL == paths) goto error;

    paths[0] = path;
    path = NULL;

    DIR *dir;
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

                if (i + 1 == paths_max) {
                    char **new_paths = realloc(paths, sizeof(char *) * (paths_max * 2));
                    if (NULL == new_paths) goto error;
                    paths_max = paths_max * 2;
                }

                paths[paths_len] = path;
                paths_len++;

                watch = add_watch(wm->fd, path, wm->events);
                if (NULL == watch) goto error;
                path = NULL;

                WatchfulWatch **new_watches = realloc(wm->watches, sizeof(WatchfulWatch *) * (wm->watches_len + 1));
                if (NULL == new_watches) goto error;
                wm->watches[wm->watches_len] = watch;
                wm->watches_len++;
                watch = NULL;
            }
        }

        paths[i] = NULL;
        closedir(dir);
    }

    free(paths);

    return 0;

error:
    if (NULL != path) free(path);
    if (NULL != watch) free(watch);
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

    if (wm->thread) {
        error = pthread_kill(wm->thread, SIGUSR1);
        if (error) return 1;
        error = pthread_join(wm->thread, NULL);
        if (error) return 1;
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
