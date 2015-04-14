#ifndef LOCAL_H_
#define LOCAL_H_
#include "cache.h"
#include "item.h"

//get local configs to set
struct settings *local_config(void);
//start cache model
bool local_start(void);
//put cache item back
void local_back(struct item *value);
//get cache item
struct item *local_get(const char *key, uint16_t nkey);
//set cache item
bool local_put(char *key, uint16_t nkey, int exptime, char *value, uint32_t nbyte);

#endif
