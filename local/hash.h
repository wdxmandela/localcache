#ifndef LOCAL_HASH_H_
#define LOCAL_HASH_H_
#include <stddef.h>
#include <stdint.h>

uint32_t hash(const void *key, size_t length, const uint32_t initval);

#endif

