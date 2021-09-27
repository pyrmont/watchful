#ifndef WATCHER_H
#define WATCHER_H

#ifdef LINUX
#define INOTIFY
#endif

#ifdef MACOS
#define FSE
#endif

/* General */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Wildmatch */
#include "wildmatch.h"

/* POSIX */
#include <pthread.h>
#include <sys/stat.h>

#ifdef INOTIFY
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#endif

#ifdef FSE
#include <CoreServices/CoreServices.h>
#endif

/* Constants */
#define WATCHFUL_EVENT_ALL      0xF
#define WATCHFUL_EVENT_NONE     0x0
#define WATCHFUL_EVENT_CREATED  0x1
#define WATCHFUL_EVENT_DELETED  0x2
#define WATCHFUL_EVENT_MOVED    0x4
#define WATCHFUL_EVENT_MODIFIED 0x8

/* Forward Declarations */
struct WatchfulWatch;
struct WatchfulEvent;
struct WatchfulBackend;
struct WatchfulMonitor;
/* struct WatchfulStream; */

/* Type Aliases */
typedef pthread_t WatchfulThread;
typedef int (*WatchfulCallback)(struct WatchfulEvent *, void *);

/* Types */

typedef struct WatchfulWatch {
  int wd;
  char *path;
} WatchfulWatch;

typedef struct WatchfulEvent {
  int type;
  char *path;
} WatchfulEvent;

typedef struct WatchfulBackend {
  const char *name;
  int (*setup)(struct WatchfulMonitor *wm);
  int (*teardown)(struct WatchfulMonitor *wm);
} WatchfulBackend;

typedef struct WatchfulExcludes {
  char **paths;
  size_t len;
} WatchfulExcludes;

typedef struct WatchfulMonitor {
  WatchfulBackend *backend;
  char *path;
  WatchfulExcludes *excludes;
  int events;
  WatchfulCallback callback;
  WatchfulEvent *callback_info;
  WatchfulThread thread;
  double delay;
#ifdef INOTIFY
  int fd;
  size_t watches_len;
  WatchfulWatch **watches;
#elif FSE
  FSEventStreamRef ref;
  CFRunLoopRef loop;
#endif
} WatchfulMonitor;

/* typedef struct WatchfulStream { */
/*   WatchfulMonitor *wm; */
/*   WatchfulThread thread; */
/*   int (*callback)(WatchfulEvent *event); */
/*   double delay; */
/* #ifdef INOTIFY */
/*   int fd; */
/*   size_t watches_len; */
/*   WatchfulWatch **watches; */
/* #elif FSE */
/*   FSEventStreamRef ref; */
/*   CFRunLoopRef loop; */
/* #endif */
/* } WatchfulStream; */

/* Externs */
extern WatchfulBackend watchful_fse;
extern WatchfulBackend watchful_inotify;

#ifdef LINUX
#define watchful_default_backend watchful_inotify
#elif MACOS
#define watchful_default_backend watchful_fse
#endif

/* Path Functions */
char *watchful_path_create(const char *path, const char *prefix, bool is_dir);
bool watchful_path_is_dir(const char *path);

/* Monitor Functions */
int watchful_monitor_init(WatchfulMonitor *wm, WatchfulBackend *backend, const char *path, size_t excl_paths_len, const char** excl_paths, int events, WatchfulCallback cb, void *cb_info);
WatchfulMonitor *watchful_monitor_create(WatchfulBackend *backend, const char *path, size_t excl_paths_len, const char** excl_paths, int events, WatchfulCallback cb, void *cb_info);
void watchful_monitor_deinit(WatchfulMonitor *wm);
void watchful_monitor_destroy(WatchfulMonitor *wm);
bool watchful_monitor_excludes_path(WatchfulMonitor *wm, const char *path);
int watchful_monitor_start(WatchfulMonitor *wm);
int watchful_monitor_stop(WatchfulMonitor *wm);

/* Debugging Functions */
#define WATCHFUL_DEBUG 1
#ifdef WATCHFUL_DEBUG
#define debug_print(...) fprintf(stderr, __VA_ARGS__)
#else
#define debug_print(...) (void)0
#endif

#endif
