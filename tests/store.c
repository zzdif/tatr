#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include "../src/store.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>
#include <unistd.h>

static const char *const id = "20260101-123456-test";

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void path(char out[1024], const char *root, const char *tail)
{
    int n = snprintf(out, 1024, "%s/%s", root, tail);
    check(n > 0 && n < 1024, "test path fits");
}

static void write_file(const char *name, const char *contents)
{
    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    size_t n = strlen(contents);
    check(fd >= 0, "open fixture file");
    check(write(fd, contents, n) == (ssize_t)n, "write fixture file");
    check(close(fd) == 0, "close fixture file");
}

static void expect_code(const Store *store, const char *code, const char *message)
{
    check(store->code != NULL && strcmp(store->code, code) == 0, message);
}

static void check_no_temps(const char *task)
{
    DIR *dir = opendir(task);
    struct dirent *entry;
    check(dir != NULL, "open task to check temp cleanup");
    while ((entry = readdir(dir)) != NULL)
        check(strncmp(entry->d_name, ".tatr-", 6) != 0, "temporary artifact left behind");
    closedir(dir);
}

static void test_lock(const char *db)
{
    int ready[2], release[2];
    pid_t child;
    Store store = { .fd = -1 };

    check(pipe(ready) == 0 && pipe(release) == 0, "create lock pipes");
    child = fork();
    check(child >= 0, "fork lock holder");
    if (child == 0) {
        char byte;
        Store held = { .fd = -1 };
        close(ready[0]);
        close(release[1]);
        if (!store_open(&held, db, true)) _exit(2);
        if (write(ready[1], "L", 1) != 1) _exit(3);
        if (read(release[0], &byte, 1) != 1) _exit(4);
        store_close(&held);
        _exit(0);
    }
    close(ready[1]);
    close(release[0]);
    {
        char byte;
        check(read(ready[0], &byte, 1) == 1 && byte == 'L', "exclusive lock held before contention test");
    }
    check(!store_open(&store, db, false), "shared open must not bypass held exclusive lock");
    expect_code(&store, "busy", "busy lock diagnostic");
    check(write(release[1], "R", 1) == 1, "release lock holder");
    check(waitpid(child, NULL, 0) == child, "wait lock holder");
    check(store_open(&store, db, false), "lock becomes available after release");
    store_close(&store);
    close(ready[0]);
    close(release[1]);
}

int main(void)
{
    char template[] = "/tmp/tatr-store-test.XXXXXX";
    char root[1024], db[1024], outside[1024], task[1024], task_md[1024], other[1024], linked[1024];
    char *data = NULL;
    size_t len = 0;
    struct stat before, after;
    Store store = { .fd = -1 }, probe = { .fd = -1 };

    check(mkdtemp(template) != NULL && realpath(template, root) != NULL, "make temporary root");
    path(db, root, "db");
    path(outside, root, "outside");
    check(mkdir(db, 0700) == 0 && mkdir(outside, 0700) == 0, "make fixture databases");

    char *cwd = getcwd(NULL, 0);
    check(chdir(root) == 0, "enter fixture root");
    check(store_open(&probe, "./db", false), "relative database resolves from cwd");
    check(strcmp(probe.path, db) == 0, "relative absolute identity");
    store_close(&probe);
    check(chdir(cwd) == 0, "restore cwd"); free(cwd);
    check(!store_open(&probe, "/definitely/not/a/tatr/database", false), "missing database rejected");
    expect_code(&probe, "database_not_found", "missing database diagnostic");
    path(linked, root, "db-link");
    check(symlink(db, linked) == 0, "make database symlink");
    check(!store_open(&probe, linked, false), "database symlink rejected");
    expect_code(&probe, "unsafe_path", "database symlink diagnostic");

    check(store_open(&store, db, true), "open exclusive store");
    check(!store_create(&store, "bad/id", "x", 1), "bad id rejected before use");
    expect_code(&store, "unsafe_path", "bad id diagnostic");
    mode_t oldmask = umask(077);
    check(store_create(&store, id, "old", 3), "create task");
    umask(oldmask);
    check(store.write_state != NULL && strcmp(store.write_state, "committed") == 0, "create commit state");
    check(!store_create(&store, id, "again", 5), "duplicate create conflicts");
    expect_code(&store, "conflict", "duplicate create diagnostic");
    path(task, db, id);
    path(task_md, task, "TASK.md");
    check_no_temps(task);

    check(store_read(&store, id, &data, &len, &before), "read created task");
    check(len == 3 && memcmp(data, "old", 3) == 0 && data[3] == '\0', "read bytes and terminator");
    check((before.st_mode & 0777) == 0600, "creation respects restrictive umask");
    pid_t failure_child = fork();
    check(failure_child >= 0, "fork creation IO failure");
    if (failure_child == 0) {
        struct rlimit limit = {0, 0};
        signal(SIGXFSZ, SIG_IGN);
        if (setrlimit(RLIMIT_FSIZE, &limit) < 0) _exit(2);
        if (store_create(&store, "20260105-123456-failed", "payload", 7)) _exit(3);
        if (!store.code || strcmp(store.code, "io_error") || store.write_state) _exit(4);
        _exit(0);
    }
    int failure_status;
    check(waitpid(failure_child, &failure_status, 0) == failure_child && WIFEXITED(failure_status) && WEXITSTATUS(failure_status) == 0, "prepublication failure result");
    struct stat absent;
    check(fstatat(store.fd, "20260105-123456-failed", &absent, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT, "prepublication failure cleans owned directory and temp");
    check(chmod(task_md, 0751) == 0, "set old permissions");
    free(data);
    data = NULL;
    check(store_read(&store, id, &data, &len, &before), "read after chmod");
    check(store_replace(&store, id, data, len, &before, "new content", 11), "replace current task");
    free(data);
    data = NULL;
    check(store_read(&store, id, &data, &len, &after), "read replacement");
    check(len == 11 && memcmp(data, "new content", 11) == 0, "replacement bytes");
    check((after.st_mode & 07777) == 0751, "replacement preserves permission bits");
    check(!store_replace(&store, id, "old", 3, &before, "stale", 5), "stale replacement conflicts");
    expect_code(&store, "conflict", "stale replacement diagnostic");
    free(data);
    data = NULL;
    check_no_temps(task);
#ifdef STORE_TESTING
    extern int store_test_failure;
    check(store_read(&store, id, &data, &len, &before), "read before injection");
    store_test_failure = 1;
    check(!store_replace(&store, id, data, len, &before, "injected", 8), "pre-rename injection fails");
    expect_code(&store, "io_error", "pre-rename diagnostic");
    check(store.write_state == NULL, "pre-rename failure not committed");
    check_no_temps(task); free(data); data = NULL;
    check(store_read(&store, id, &data, &len, &before), "read after pre-rename failure");
    check(len == 11 && memcmp(data, "new content", 11) == 0, "pre-rename preserves original bytes");
    store_test_failure = 2;
    check(!store_replace(&store, id, data, len, &before, "committed", 9), "post-rename injection fails");
    expect_code(&store, "io_error", "post-rename diagnostic");
    check(store.write_state && !strcmp(store.write_state, "committed"), "post-rename commit state");
    free(data); data = NULL; store_test_failure = 0;
    check(store_read(&store, id, &data, &len, &before), "reconcile after post-rename error");
    check(len == 9 && memcmp(data, "committed", 9) == 0, "post-rename fresh read observes committed bytes");
    free(data); data = NULL; check_no_temps(task);
#endif
    store_close(&store);

    test_lock(db);
    check(store_open(&store, db, true), "reopen store after lock test");

    path(linked, db, "20260102-123456-link");
    check(symlink(outside, linked) == 0, "make task symlink");
    check(!store_read(&store, "20260102-123456-link", &data, &len, NULL), "task symlink rejected");
    expect_code(&store, "unsafe_path", "task symlink diagnostic");

    check(mkdirat(store.fd, "20260103-123456-filelink", 0700) == 0, "make hardlink task directory");
    path(other, root, "other");
    write_file(other, "shared");
    {
        char hard_task[1024];
        path(hard_task, db, "20260103-123456-filelink/TASK.md");
        check(link(other, hard_task) == 0, "make TASK.md hard link");
    }
    check(!store_read(&store, "20260103-123456-filelink", &data, &len, NULL), "hard linked TASK.md rejected");
    expect_code(&store, "unsafe_path", "hardlink diagnostic");

    check(mkdirat(store.fd, "20260104-123456-symlink", 0700) == 0, "make symlink task directory");
    {
        char sym_task[1024];
        path(sym_task, db, "20260104-123456-symlink/TASK.md");
        check(symlink(other, sym_task) == 0, "make TASK.md symlink");
    }
    check(!store_read(&store, "20260104-123456-symlink", &data, &len, NULL), "TASK.md symlink rejected");
    expect_code(&store, "unsafe_path", "TASK.md symlink diagnostic");
    store_close(&store);

    check(unlink(task_md) == 0 && rmdir(task) == 0, "remove created task fixture");
    path(linked, db, "20260102-123456-link");
    check(unlink(linked) == 0, "remove task symlink fixture");
    path(linked, db, "20260103-123456-filelink/TASK.md");
    check(unlink(linked) == 0, "remove hardlink fixture");
    path(linked, db, "20260103-123456-filelink");
    check(rmdir(linked) == 0, "remove hardlink task fixture");
    path(linked, db, "20260104-123456-symlink/TASK.md");
    check(unlink(linked) == 0, "remove file symlink fixture");
    path(linked, db, "20260104-123456-symlink");
    check(rmdir(linked) == 0, "remove file symlink task fixture");
    check(unlink(other) == 0, "remove shared file fixture");
    path(linked, root, "db-link");
    check(unlink(linked) == 0, "remove database symlink fixture");
    check(rmdir(db) == 0 && rmdir(outside) == 0 && rmdir(root) == 0, "remove temporary root");

    printf("PASS: store API, locks, stale writes, symlinks, hardlinks, permissions, restrictive umask, kernel-induced creation IO failure cleanup\n");
#ifdef STORE_TESTING
    puts("PASS: A18 injected pre-rename failure preserves bytes/cleans owned temp; post-rename error reports committed and fresh read reconciles");
#endif
    return 0;
}
