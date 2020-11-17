#include "../watchful.h"

#ifndef LINUX

watchful_backend_t watchful_inotify = {
    .name = "inotify",
    .setup = NULL,
    .teardown = NULL,
};

#else

static int setup(watchful_stream_t *stream) {
    return 0;
}

static int teardown(watchful_stream_t *stream) {
    return 0;
}

watchful_backend_t watchful_inotify = {
    .name = "inotify",
    .setup = setup,
    .teardown = teardown,
};

#endif
