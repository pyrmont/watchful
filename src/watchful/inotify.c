#include "../watchful.h"

#ifndef LINUX

watchful_backend_t watchful_inotify = {
    .name = "inotify",
    .setup = NULL,
    .teardown = NULL,
};

#else

static int handle_event(watchful_stream_t *stream) {
    char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *notify_event;

    int size = read(stream->fd, buf, sizeof(buf));
    if (size <= 0) return 1;

	for (char *ptr = buf; ptr < buf + size; ptr += sizeof(struct inotify_event) + notify_event->len) {
		notify_event = (const struct inotify_event *)ptr;

        /* Print event type */
        if (notify_event->mask & IN_MODIFY)
            printf("IN_MODIFY: ");
        if (notify_event->mask & IN_MOVE)
            printf("IN_MOVE: ");
        if (notify_event->mask & IN_ATTRIB)
            printf("IN_ATTRIB: ");
        if (notify_event->mask & IN_DELETE)
            printf("IN_DELETE: ");

        /* Print the name of the file */
        if (notify_event->len)
            printf("%s", notify_event->name);
        else
            printf("%s", stream->wm->path);

        /* Print type of filesystem object */
        if (notify_event->mask & IN_ISDIR)
            printf(" [directory]\n");
        else
            printf(" [file]\n");

        watchful_event_t *event = (watchful_event_t *)malloc(sizeof(watchful_event_t));

        event->type = 5;

        if (notify_event->mask & IN_ISDIR) {
            size_t path_len = strlen((char *)stream->wm->path) + 1;
            event->path = (char *)malloc(path_len);
            memcpy(event->path, stream->wm->path, path_len);
        } else {
            size_t root_len = strlen((char *)stream->wm->path);
            event->path = (char *)malloc(root_len + notify_event->len);
            memcpy(event->path, stream->wm->path, root_len);
            memcpy(event->path + root_len, notify_event->name, notify_event->len);
        }

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

    printf("Entering the run loop...\n");
    while (1) {
        int nfds = ((stream->fd < sfd) ? sfd : stream->fd) + 1;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(stream->fd, &readfds);
        FD_SET(sfd, &readfds);

        select(nfds, &readfds, NULL, NULL, NULL);
        if (FD_ISSET(sfd, &readfds)) break;

        printf("Select complete\n");

        error = handle_event(stream);
        if (error) return NULL;

        printf("Read complete\n");
    }
    printf("Leaving the run loop...\n");

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

static int setup(watchful_stream_t *stream) {
    printf("Setting up...\n");
    int error = 0;

    stream->fd = inotify_init();
    if (stream->fd == -1) return 1;

    stream->wd = inotify_add_watch(stream->fd, (const char *)stream->wm->path, IN_MODIFY | IN_MOVE | IN_ATTRIB | IN_DELETE);
    if (stream->wd == -1) return 1;

    error = start_loop(stream);
    if (error) return 1;

    return 0;
}

static int teardown(watchful_stream_t *stream) {
    printf("Tearing down...\n");
    int error = 0;

    if (stream->thread) {
        pthread_kill(stream->thread, SIGUSR1);
        pthread_join(stream->thread, NULL);
    }

    error = inotify_rm_watch(stream->fd, stream->wd);
    error = close(stream->fd);
    if (error) return 1;

    stream->fd = 0;
    stream->wd = 0;

    return 0;
}

watchful_backend_t watchful_inotify = {
    .name = "inotify",
    .setup = setup,
    .teardown = teardown,
};

#endif
