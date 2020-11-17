#include "../watchful.h"

#ifndef LINUX

watchful_backend_t watchful_inotify = {
    .name = "inotify",
    .setup = NULL,
    .teardown = NULL,
};

#else

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

    char buf[4096];

    printf("Entering the run loop...\n");
    while (1) {
        int nfds = ((stream->notifier < sfd) ? sfd : stream->notifier) + 1;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(stream->notifier, &readfds);
        FD_SET(sfd, &readfds);

        select(nfds, &readfds, NULL, NULL, NULL);
        if (FD_ISSET(sfd, &readfds)) break;

        printf("Select complete\n");

        int size = read(stream->notifier, buf, sizeof(buf));
        if (size == -1) break;

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

    stream->notifier = inotify_init();
    if (stream->notifier == -1) return 1;

    int error = start_loop(stream);
    if (error) return 1;

    return 0;
}

static int teardown(watchful_stream_t *stream) {
    printf("Tearing down...\n");

    pthread_kill(stream->thread, SIGUSR1);
    pthread_join(stream->thread, NULL);

    int error = close(stream->notifier);
    if (error) return 1;

    stream->notifier = 0;

    return 0;
}

watchful_backend_t watchful_inotify = {
    .name = "inotify",
    .setup = setup,
    .teardown = teardown,
};

#endif
