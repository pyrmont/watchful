#ifndef WATCHER_H
#define WATCHER_H

/* General */
#include <string.h>
#include <time.h>

/* POSIX */
#include <pthread.h>

#ifdef LINUX
/* Linux */
#include <signal.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/signalfd.h>
#elif MACOS
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

#ifdef LINUX
  int notifier;
#elif MACOS
  FSEventStreamRef ref;
  CFRunLoopRef loop;
#endif
} watchful_stream_t;

typedef struct watchful_event_t {
  int type;
  char *path;
} watchful_event_t;

/* Externs */

extern watchful_backend_t watchful_fse;
extern watchful_backend_t watchful_inotify;

#ifdef LINUX
#define watchful_default_backend watchful_inotify
#elif MACOS
#define watchful_default_backend watchful_fse
#endif

#endif
