#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include "store.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifdef STORE_TESTING
#ifndef STORE_TEST_FAILURE
#define STORE_TEST_FAILURE 0
#endif
int store_test_failure = STORE_TEST_FAILURE;
static int store_test_replacements;
#endif

static bool fail(Store *store, const char *code, const char *what)
{
    store->code = code;
    store->error[0] = '\0';
    if (what != NULL) snprintf(store->error, sizeof(store->error), "%s: %s", what, strerror(errno));
    return false;
}

static bool unsafe(Store *store, const char *what)
{
    return fail(store, "unsafe_path", what);
}

static void clear_result(Store *store)
{
    store->code = NULL;
    store->error[0] = '\0';
    store->write_state = NULL;
}

static bool same_file(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static bool valid_id(const char *id)
{
    size_t i, n;

    if (id == NULL) return false;
    n = strlen(id);
    if (n < 15 || n > 255) return false;
    for (i = 0; i < 8; ++i) if (id[i] < '0' || id[i] > '9') return false;
    if (id[8] != '-') return false;
    for (i = 9; i < 15; ++i) if (id[i] < '0' || id[i] > '9') return false;
    if (id[15] == '\0') return true;
    if (id[15] != '-') return false;
    for (i = 16; id[i] != '\0'; ++i) {
        if (!((id[i] >= 'a' && id[i] <= 'z') ||
              (id[i] >= 'A' && id[i] <= 'Z') ||
              (id[i] >= '0' && id[i] <= '9') || id[i] == '-')) return false;
    }
    return true;
}

static bool regular_single(const struct stat *st)
{
    return S_ISREG(st->st_mode) && st->st_nlink == 1;
}

static bool dir_stat(const struct stat *st)
{
    return S_ISDIR(st->st_mode);
}

/* Build a lexical absolute identity while walking every nonempty component. */
static bool open_database(Store *store, const char *db, char **identity)
{
    const char *p;
    int fd = -1, next;
    size_t n, used = 1;
    char *path;
    struct stat st;

    if (db == NULL || db[0] != '/') return unsafe(store, "database path");
    n = strlen(db);
    path = malloc(n + 2);
    if (path == NULL) return fail(store, "io_error", "malloc");
    path[0] = '/';
    path[1] = '\0';

    fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        free(path);
        return fail(store, "io_error", "open /");
    }
    p = db + 1;
    while (*p != '\0') {
        const char *part = p;
        size_t part_len;

        while (*p != '\0' && *p != '/') ++p;
        part_len = (size_t)(p - part);
        while (*p == '/') ++p;
        if (part_len == 0) continue;
        if (part_len == 1 && part[0] == '.') continue;
        if (part_len == 2 && part[0] == '.' && part[1] == '.') {
            next = openat(fd, "..", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0) { close(fd); free(path); return fail(store, "io_error", "open parent"); }
            close(fd); fd = next;
            while (used > 1 && path[used-1] != '/') --used;
            if (used > 1) --used;
            path[used] = '\0';
            continue;
        }
        {
            char *name = malloc(part_len + 1);
            if (name == NULL) {
                close(fd);
                free(path);
                return fail(store, "io_error", "malloc");
            }
            memcpy(name, part, part_len);
            name[part_len] = '\0';
            next = openat(fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            free(name);
        }
        if (next < 0) {
            int e = errno;
            close(fd);
            free(path);
            errno = e;
            if (e == ENOENT) return fail(store, "database_not_found", "open database");
            if (e == ELOOP || e == ENOTDIR) return unsafe(store, "database path component");
            return fail(store, "io_error", "open database");
        }
        if (fstat(next, &st) < 0 || !dir_stat(&st)) {
            int e = errno;
            close(next);
            close(fd);
            free(path);
            errno = e;
            return unsafe(store, "database path component");
        }
        close(fd);
        fd = next;
        if (used != 1) path[used++] = '/';
        memcpy(path + used, part, part_len);
        used += part_len;
        path[used] = '\0';
    }
    if (fstat(fd, &st) < 0 || !dir_stat(&st)) {
        int e = errno;
        close(fd);
        free(path);
        errno = e;
        return unsafe(store, "database path");
    }
    *identity = path;
    store->fd = fd;
    return true;
}

bool store_open(Store *store, const char *db, bool exclusive)
{
    char *identity = NULL;

    if (store == NULL) return false;
    store->fd = -1;
    store->path = NULL;
    clear_result(store);
    char *absolute = NULL;
    if (db && db[0] != '/') {
        char *cwd = getcwd(NULL, 0);
        if (!cwd) return fail(store, "io_error", "getcwd");
        size_t n = strlen(cwd) + strlen(db) + 2;
        absolute = malloc(n);
        if (!absolute) { free(cwd); return fail(store, "io_error", "malloc"); }
        snprintf(absolute, n, "%s/%s", cwd, db); free(cwd);
    }
    bool opened = open_database(store, absolute ? absolute : db, &identity);
    free(absolute);
    if (!opened) return false;
    if (flock(store->fd, (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) < 0) {
        int e = errno;
        close(store->fd);
        store->fd = -1;
        free(identity);
        errno = e;
        if (e == EWOULDBLOCK || e == EAGAIN) return fail(store, "busy", "lock database");
        return fail(store, "io_error", "lock database");
    }
    store->path = identity;
    return true;
}

void store_close(Store *store)
{
    if (store == NULL) return;
    if (store->fd >= 0) close(store->fd);
    free(store->path);
    store->fd = -1;
    store->path = NULL;
    store->code = NULL;
    store->error[0] = '\0';
    store->write_state = NULL;
}

static bool open_task(Store *store, const char *id, int *taskfd, struct stat *taskst)
{
    struct stat named, opened;
    int fd;

    if (fstatat(store->fd, id, &named, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno == ENOENT) return fail(store, "task_not_found", "find task");
        return fail(store, "io_error", "find task");
    }
    if (!dir_stat(&named)) return unsafe(store, "task directory");
    fd = openat(store->fd, id, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ELOOP || errno == ENOTDIR) return unsafe(store, "task directory");
        if (errno == ENOENT) return fail(store, "task_not_found", "open task");
        return fail(store, "io_error", "open task");
    }
    if (fstat(fd, &opened) < 0 || !dir_stat(&opened)) {
        int e = errno;
        close(fd);
        errno = e;
        return unsafe(store, "task directory");
    }
    if (!same_file(&named, &opened)) {
        close(fd);
        return fail(store, "conflict", "task changed");
    }
    *taskfd = fd;
    *taskst = opened;
    return true;
}

static bool open_task_file(Store *store, int taskfd, int *filefd, struct stat *filest)
{
    struct stat named, opened;
    int fd;

    if (fstatat(taskfd, "TASK.md", &named, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno == ENOENT) return fail(store, "task_not_found", "find TASK.md");
        return fail(store, "io_error", "find TASK.md");
    }
    if (!regular_single(&named)) return unsafe(store, "TASK.md");
    fd = openat(taskfd, "TASK.md", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ELOOP) return unsafe(store, "TASK.md");
        if (errno == ENOENT) return fail(store, "task_not_found", "open TASK.md");
        return fail(store, "io_error", "open TASK.md");
    }
    if (fstat(fd, &opened) < 0 || !regular_single(&opened)) {
        int e = errno;
        close(fd);
        errno = e;
        return unsafe(store, "TASK.md");
    }
    if (!same_file(&named, &opened)) {
        close(fd);
        return fail(store, "conflict", "TASK.md changed");
    }
    *filefd = fd;
    *filest = opened;
    return true;
}

static bool read_exact(int fd, void *buf, size_t len)
{
    unsigned char *p = buf;
    while (len != 0) {
        ssize_t got = read(fd, p, len);
        if (got > 0) {
            p += got;
            len -= (size_t)got;
        } else if (got < 0 && errno == EINTR) {
            continue;
        } else {
            if (got == 0) errno = EIO;
            return false;
        }
    }
    return true;
}

bool store_read(Store *store, const char *id, char **data, size_t *len, struct stat *st)
{
    int taskfd = -1, filefd = -1;
    struct stat taskst, before, after;
    char *buf = NULL;
    size_t size;

    if (store == NULL || store->fd < 0 || data == NULL || len == NULL) return false;
    *data = NULL;
    *len = 0;
    clear_result(store);
    if (!valid_id(id)) return unsafe(store, "task id");
    if (!open_task(store, id, &taskfd, &taskst)) return false;
    if (!open_task_file(store, taskfd, &filefd, &before)) goto done;
    if (before.st_size < 0 || (uintmax_t)before.st_size > SIZE_MAX - 1) {
        fail(store, "io_error", "TASK.md too large");
        goto done;
    }
    size = (size_t)before.st_size;
    buf = malloc(size + 1);
    if (buf == NULL) {
        fail(store, "io_error", "malloc");
        goto done;
    }
    if (!read_exact(filefd, buf, size) || fstat(filefd, &after) < 0) {
        fail(store, "io_error", "read TASK.md");
        goto done;
    }
    if (!same_file(&before, &after) || after.st_size != before.st_size || !regular_single(&after)) {
        fail(store, "conflict", "TASK.md changed");
        goto done;
    }
    buf[size] = '\0';
    *data = buf;
    *len = size;
    if (st != NULL) *st = after;
    buf = NULL;

done:
    free(buf);
    if (filefd >= 0) close(filefd);
    if (taskfd >= 0) close(taskfd);
    return store->code == NULL;
}

static bool write_all(int fd, const void *bytes, size_t len)
{
    const unsigned char *p = bytes;
    while (len != 0) {
        ssize_t put = write(fd, p, len);
        if (put > 0) {
            p += put;
            len -= (size_t)put;
        } else if (put < 0 && errno == EINTR) {
            continue;
        } else {
            if (put == 0) errno = EIO;
            return false;
        }
    }
    return true;
}

static bool content_is(int fd, const void *old, size_t oldlen, const struct stat *expected)
{
    unsigned char buf[8192];
    const unsigned char *want = old;
    struct stat now;
    size_t left = oldlen;

    if (fstat(fd, &now) < 0 || !regular_single(&now) || !same_file(&now, expected) ||
        now.st_size < 0 || (uintmax_t)now.st_size != oldlen || lseek(fd, 0, SEEK_SET) < 0) return false;
    while (left != 0) {
        size_t chunk = left < sizeof(buf) ? left : sizeof(buf);
        if (!read_exact(fd, buf, chunk) || memcmp(buf, want, chunk) != 0) return false;
        want += chunk;
        left -= chunk;
    }
    if (fstat(fd, &now) < 0 || !regular_single(&now) || !same_file(&now, expected) ||
        now.st_size < 0 || (uintmax_t)now.st_size != oldlen) return false;
    return true;
}

static void remove_owned(int dirfd, const char *name, const struct stat *owned);

static bool make_temp(Store *store, int dirfd, const void *bytes, size_t len,
                      mode_t mode, char name[96], struct stat *tempst)
{
    unsigned int attempt;
    int fd = -1;

    for (attempt = 0; attempt != 100; ++attempt) {
        snprintf(name, 96, ".tatr-%ld-%u.tmp", (long)getpid(), attempt);
        fd = openat(dirfd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0) break;
        if (errno != EEXIST) return fail(store, "io_error", "create temporary TASK.md");
    }
    if (fd < 0) return fail(store, "io_error", "create temporary TASK.md");
    if (fstat(fd, tempst) < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return fail(store, "io_error", "stat temporary TASK.md");
    }
    if (!write_all(fd, bytes, len) || fchmod(fd, mode & 07777) < 0 || fsync(fd) < 0) {
        int e = errno;
        close(fd);
        remove_owned(dirfd, name, tempst);
        errno = e;
        return fail(store, "io_error", "write temporary TASK.md");
    }
    if (close(fd) < 0) {
        int e = errno;
        remove_owned(dirfd, name, tempst);
        errno = e;
        return fail(store, "io_error", "close temporary TASK.md");
    }
    return true;
}

static void remove_owned(int dirfd, const char *name, const struct stat *owned)
{
    struct stat now;
    if (fstatat(dirfd, name, &now, AT_SYMLINK_NOFOLLOW) == 0 && same_file(&now, owned))
        (void)unlinkat(dirfd, name, 0);
}

static bool current_target(Store *store, const char *id, int taskfd,
                           const struct stat *taskst, const struct stat *filest)
{
    struct stat now;

    if (fstatat(store->fd, id, &now, AT_SYMLINK_NOFOLLOW) < 0 || !dir_stat(&now) ||
        !same_file(&now, taskst)) return fail(store, "conflict", "task changed");
    if (fstatat(taskfd, "TASK.md", &now, AT_SYMLINK_NOFOLLOW) < 0 || !regular_single(&now) ||
        !same_file(&now, filest)) return fail(store, "conflict", "TASK.md changed");
    return true;
}

bool store_replace(Store *store, const char *id, const void *old, size_t oldlen,
                   const struct stat *oldst, const void *next, size_t nextlen)
{
    int taskfd = -1, filefd = -1;
    struct stat taskst, filest, tempst;
    char temp[96] = "";
    bool have_temp = false;

    if (store == NULL || store->fd < 0) return false;
    clear_result(store);
    if (!valid_id(id)) return unsafe(store, "task id");
    if ((oldlen != 0 && old == NULL) || oldst == NULL || (nextlen != 0 && next == NULL))
        return fail(store, "io_error", "invalid replacement");
    if (!open_task(store, id, &taskfd, &taskst)) goto done;
    if (!open_task_file(store, taskfd, &filefd, &filest)) goto done;
    if (!same_file(&filest, oldst) || !content_is(filefd, old, oldlen, &filest)) {
        fail(store, "conflict", "TASK.md changed");
        goto done;
    }
    if (!make_temp(store, taskfd, next, nextlen, oldst->st_mode, temp, &tempst)) goto done;
    have_temp = true;
#ifdef STORE_TESTING
    if (store_test_failure == 1 || (store_test_failure == 3 && ++store_test_replacements == 2)) {
        errno = EIO; fail(store, "io_error", "injected pre-rename failure"); goto done;
    }
#endif
    if (!current_target(store, id, taskfd, &taskst, &filest)) goto done;
    if (!content_is(filefd, old, oldlen, &filest)) {
        fail(store, "conflict", "TASK.md changed");
        goto done;
    }
    if (renameat(taskfd, temp, taskfd, "TASK.md") < 0) {
        fail(store, "io_error", "rename TASK.md");
        goto done;
    }
    have_temp = false;
    store->write_state = "committed";
#ifdef STORE_TESTING
    if (store_test_failure == 2) { errno = EIO; fail(store, "io_error", "injected post-rename failure"); goto done; }
#endif
    if (fsync(taskfd) < 0) fail(store, "io_error", "fsync task directory");

done:
    if (have_temp) remove_owned(taskfd, temp, &tempst);
    if (filefd >= 0) close(filefd);
    if (taskfd >= 0) close(taskfd);
    return store->code == NULL;
}

bool store_create(Store *store, const char *id, const void *bytes, size_t len)
{
    int taskfd = -1;
    struct stat taskst = {0}, tempst, named;
    char temp[96] = "";
    bool made_dir = false, have_temp = false;

    if (store == NULL || store->fd < 0) return false;
    clear_result(store);
    if (!valid_id(id)) return unsafe(store, "task id");
    if (len != 0 && bytes == NULL) return fail(store, "io_error", "invalid content");
    if (mkdirat(store->fd, id, 0700) < 0) {
        if (errno == EEXIST) return fail(store, "conflict", "create task");
        return fail(store, "io_error", "create task");
    }
    made_dir = true;
    if (fstatat(store->fd, id, &taskst, AT_SYMLINK_NOFOLLOW) < 0 || !dir_stat(&taskst)) {
        fail(store, "io_error", "find created task");
        goto done;
    }
    if (!open_task(store, id, &taskfd, &taskst)) goto done;
    if (fstatat(taskfd, "TASK.md", &named, AT_SYMLINK_NOFOLLOW) == 0) {
        fail(store, "conflict", "TASK.md exists");
        goto done;
    }
    if (errno != ENOENT) {
        fail(store, "io_error", "find TASK.md");
        goto done;
    }
    mode_t mask = umask(0); umask(mask);
    if (!make_temp(store, taskfd, bytes, len, 0666 & ~mask, temp, &tempst)) goto done;
    have_temp = true;
    if (fstatat(store->fd, id, &named, AT_SYMLINK_NOFOLLOW) < 0 || !dir_stat(&named) ||
        !same_file(&named, &taskst)) {
        fail(store, "conflict", "task changed");
        goto done;
    }
    if (fstatat(taskfd, "TASK.md", &named, AT_SYMLINK_NOFOLLOW) == 0) {
        fail(store, "conflict", "TASK.md exists");
        goto done;
    }
    if (errno != ENOENT) {
        fail(store, "io_error", "find TASK.md");
        goto done;
    }
    if (renameat(taskfd, temp, taskfd, "TASK.md") < 0) {
        fail(store, "io_error", "rename TASK.md");
        goto done;
    }
    have_temp = false;
    store->write_state = "committed";
    if (fsync(taskfd) < 0 || fsync(store->fd) < 0) fail(store, "io_error", "fsync directory");

done:
    if (have_temp) remove_owned(taskfd, temp, &tempst);
    if (taskfd >= 0) close(taskfd);
    if (made_dir && store->write_state == NULL) {
        if (fstatat(store->fd, id, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
            dir_stat(&named) && same_file(&named, &taskst))
            (void)unlinkat(store->fd, id, AT_REMOVEDIR);
    }
    return store->code == NULL;
}
