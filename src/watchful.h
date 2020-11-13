#ifndef WATCHER_H
#define WATCHER_H

/* General */
#include <string.h>

/* POSIX */
#include <pthread.h>

#ifdef MACOS
/* macOS */
#include <CoreServices/CoreServices.h>
#endif

/* Janet */
#include <janet.h>

/* Type Aliases */

typedef pthread_t watchful_thread_t;

/* Forward Declarations */

struct watchful_monitor_t;
struct watchful_backend_t;
struct watchful_stream_t;

/* Types */

typedef struct watchful_backend_t {
  const char *name;
  int (*setup)(struct watchful_stream_t *stream);
  int (*teardown)(struct watchful_stream_t *stream);
} watchful_backend_t;

typedef struct watchful_monitor_t {
  struct watchful_backend_t *backend;
  const uint8_t *path;
} watchful_monitor_t;

typedef struct watchful_stream_t {
  struct watchful_monitor_t *wm;
  watchful_thread_t thread;
  JanetThread *parent;

#ifdef MACOS
    FSEventStreamRef ref;
    CFRunLoopRef loop;
#endif
} watchful_stream_t;

/* Externs */

extern watchful_backend_t watchful_fse;
extern watchful_backend_t watchful_inotify;

#endif