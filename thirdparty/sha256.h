#ifndef SCHEDULER_SHA256_H_
#define SCHEDULER_SHA256_H_

#include <stddef.h>

void scheduler_sha256(const void *data, size_t length, char out[72]);

#endif // SCHEDULER_SHA256_H_
