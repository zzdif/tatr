#ifndef TATR_STORE_H
#define TATR_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>

typedef struct Store {
    int fd;
    char *path;
    const char *code;
    char error[256];
    const char *write_state;
} Store;

bool store_open(Store *store, const char *db, bool exclusive);
void store_close(Store *store);
bool store_read(Store *store, const char *id, char **data, size_t *len,
                struct stat *st);
bool store_replace(Store *store, const char *id, const void *old,
                   size_t oldlen, const struct stat *oldst,
                   const void *next, size_t nextlen);
bool store_create(Store *store, const char *id, const void *bytes,
                  size_t len);

#endif
