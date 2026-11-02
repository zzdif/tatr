/* Test-only CLI: deny directory data access without denying metadata access. */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int denied(int dirfd, const char *path, int flags)
{
    const char *targets[] = {getenv("TATR_TEST_DENY"), getenv("TATR_TEST_DENY_SECOND")};
    struct stat wanted, actual;
    int saved = errno, match = 0;
    if (flags & O_DIRECTORY) {
        for (size_t i = 0; i < sizeof(targets) / sizeof(*targets); ++i)
            if (targets[i] && lstat(targets[i], &wanted) == 0 &&
                fstatat(dirfd, path, &actual, AT_SYMLINK_NOFOLLOW) == 0 &&
                wanted.st_dev == actual.st_dev && wanted.st_ino == actual.st_ino)
                match = 1;
        if (getenv("TATR_TEST_DENY_ROOT") && strcmp(path, "/") == 0) match = 1;
    }
    errno = match ? (getenv("TATR_TEST_EACCES") ? EACCES : EPERM) : saved;
    return match;
}

static int sandbox_open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags); mode = va_arg(args, int); va_end(args);
    }
    if (denied(AT_FDCWD, path, flags)) return -1;
    return open(path, flags, mode);
}

static int sandbox_openat(int fd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags); mode = va_arg(args, int); va_end(args);
    }
    if (denied(fd, path, flags)) return -1;
    return openat(fd, path, flags, mode);
}

#define open sandbox_open
#define openat sandbox_openat
#include "../src/tatr.c"
