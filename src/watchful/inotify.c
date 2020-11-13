#include "../watchful.h"

#ifndef LINUX

watchful_backend_t watchful_inotify = {
    .name = "inotify",
    .setup = NULL,
    .teardown = NULL,
};

#endif
