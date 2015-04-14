#ifndef LOCAL_ASSOC_H_
#define LOCAL_ASSOC_H_

#include "cache.h"

rstatus_t assoc_init(void);
void assoc_deinit(void);
struct item *assoc_find(const char *key, size_t nkey);
void assoc_insert(struct item *item);
void assoc_delete(const char *key, size_t nkey);

#endif
