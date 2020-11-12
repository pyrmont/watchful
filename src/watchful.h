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

/* Types */

typedef struct watchful_backend_t {
  const char *name;
  int (*setup)(struct watchful_monitor_t *wm);
  int (*watch)(struct watchful_monitor_t *wm);
  int (*teardown)(struct watchful_monitor_t *wm);
} watchful_backend_t;

typedef struct watchful_monitor_t {
  struct watchful_backend_t *backend;
  int pid;
  const uint8_t *path;
  JanetFunction *cb;
  watchful_thread_t thread;

#ifdef MACOS
  /* FSEvent Attributes */
  FSEventStreamRef fse_stream;
  CFRunLoopRef fse_loop;
#endif
} watchful_monitor_t;

/* Externs */

extern watchful_backend_t watchful_fse;
extern watchful_backend_t watchful_inotify;

#endif
