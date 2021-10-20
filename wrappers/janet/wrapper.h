#ifndef WATCHER_JANET_WRAPPER_H
#define WATCHER_JANET_WRAPPER_H

#include "../../src/watchful.h"
#include <janet.h>

typedef struct {
    JanetVM *vm;
    JanetFunction *fn;
} CallbackInfo;

extern const JanetAbstractType watchful_monitor_type;

#endif
