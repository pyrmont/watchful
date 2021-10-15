#include "watchful.h"

/* Helper Functions */

static char *abs_path_create(const char *path) {
    char *abs_path;
    if (path[0] == '/') {
        abs_path = watchful_path_create(path, NULL, true);
    } else {
        size_t max_len = 1024;
        char buf[max_len];
        char *cwd = getcwd(buf, max_len);
        if (NULL == cwd) return NULL;
        abs_path = watchful_path_create(path, buf, false);
    }
    return abs_path;
}

static WatchfulExcludes *excludes_create(size_t paths_len, const char **paths) {
    WatchfulExcludes *excludes = malloc(sizeof(WatchfulExcludes));
    if (excludes == NULL) return NULL;

    excludes->paths = NULL;
    excludes->len = paths_len;

    if (excludes->len == 0) return excludes;

    excludes->paths = malloc(sizeof(char *) * paths_len);;
    if (NULL == excludes->paths) goto error;

    for (size_t i = 0; i < excludes->len; i++) {
        excludes->paths[i] = abs_path_create(paths[i]);
        if (NULL == excludes->paths[i]) goto error;
    }

    return excludes;

error:
    for (size_t i = 0; i < excludes->len; i++) {
        if (NULL != excludes->paths[i]) free(excludes->paths[i]);
    }
    free(excludes);

    return NULL;
}

/* Path Functions */

char *watchful_path_create(const char *path, const char *prefix, bool is_dir) {
    char *sep = "/";

    size_t prefix_len = (NULL == prefix) ? 0 : strlen(prefix);
    size_t sep_len = (NULL == prefix || prefix[prefix_len - 1] == '/') ? 0 : 1;
    size_t path_len = strlen(path);

    bool ends_in_slash = path[path_len - 1] == '/';
    size_t new_path_len = prefix_len + sep_len + path_len;
    if (is_dir && !ends_in_slash) new_path_len++;

    char *new_path = malloc(sizeof(char) * (new_path_len + 1));
    if (NULL == new_path) return NULL;

    memcpy(new_path, prefix, prefix_len);
    memcpy(new_path + prefix_len, sep, sep_len);
    memcpy(new_path + prefix_len + sep_len, path, path_len);

    if (is_dir && !ends_in_slash) new_path[new_path_len - 1] = '/';
    new_path[new_path_len] = '\0';

    return new_path;
}

bool watchful_path_is_dir(const char *path) {
    struct stat st;
    int error = stat(path, &st);
    if (error) return false;
    return S_ISDIR(st.st_mode);
}

bool watchful_path_is_prefixed(const char *path, const char *prefix) {
    return strncmp(prefix, path, strlen(prefix)) == 0;
}

/* Monitor Functions */

int watchful_monitor_init(WatchfulMonitor *wm, WatchfulBackend *backend, const char *path, size_t excl_paths_len, const char **excl_paths, int events, double delay, WatchfulCallback cb, void *cb_info) {
    wm->backend = (NULL == backend) ? &watchful_default_backend : backend;

    wm->path = abs_path_create(path);
    if (NULL == wm->path) goto error;

    wm->excludes = excludes_create(excl_paths_len, excl_paths);
    if (NULL == wm->excludes) goto error;

    if (watchful_monitor_excludes_path(wm, path)) goto error;

    wm->events = events;
    wm->delay = 0;
    wm->callback = cb;
    wm->callback_info = cb_info;
    wm->is_watching = false;
    wm->thread = pthread_self();

    return 0;

error:
    watchful_monitor_deinit(wm);

    return 1;
}

WatchfulMonitor *watchful_monitor_create(WatchfulBackend *backend, const char *path, size_t excl_paths_len, const char **excl_paths, int events, double delay, WatchfulCallback cb, void *cb_info) {
    if (!watchful_path_is_dir(path)) return NULL;

    WatchfulMonitor *wm = malloc(sizeof(WatchfulMonitor));
    if (NULL == wm) return NULL;

    int error = watchful_monitor_init(wm, backend, path, excl_paths_len, excl_paths, events, delay, cb, cb_info);
    if (error) goto error;

    return wm;

error:
    free(wm);
    return NULL;
}

void watchful_monitor_deinit(WatchfulMonitor *wm) {
    watchful_monitor_stop(wm);

    if (NULL != wm->path) free(wm->path);

    if (NULL != wm->excludes) {
        for (size_t i = 0; i < wm->excludes->len; i++) {
            if (NULL != wm->excludes->paths[i]) free(wm->excludes->paths[i]);
        }
        if (NULL != wm->excludes->paths) free(wm->excludes->paths);
        free(wm->excludes);
    }

    wm->events = 0;
    wm->delay = 0;
    wm->callback = NULL;
    wm->callback_info = NULL;
    wm->is_watching = false;
    wm->thread = pthread_self();

    return;
}


void watchful_monitor_destroy(WatchfulMonitor *wm) {
    watchful_monitor_deinit(wm);
    free(wm);
    return;
}

bool watchful_monitor_excludes_path(WatchfulMonitor *wm, const char *path) {
    if (wm->excludes->len == 0) return false;
    for (size_t i = 0; i < wm->excludes->len; i++) {
        const char *exclude = (const char *)wm->excludes->paths[i];
        if (wildmatch(exclude, path, WM_WILDSTAR) == WM_MATCH) return true;
    }
    return false;
}

int watchful_monitor_start(WatchfulMonitor *wm) {
    if (wm->is_watching) return 1;
    int error = 0;
    error = wm->backend->setup(wm);
    if (error) return 1;
    wm->is_watching = true;
    return 0;
}

int watchful_monitor_stop(WatchfulMonitor *wm) {
    if (!wm->is_watching) return 0;
    int error = 0;
    error = wm->backend->teardown(wm);
    if (error) return 1;
    wm->is_watching = false;
    return 0;
}
