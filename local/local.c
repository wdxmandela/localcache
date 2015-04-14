#include "slabs.h"
#include "local.h"
#include "assoc.h"
#include "item.h"

struct settings settings;

struct settings *local_config(void) {
    return &settings;
}

bool local_start(void) {
	item_init();
	rstatus_t status = assoc_init();
    if (status != MC_OK) return false;
    status = time_init();
    if (status != MC_OK) return false;
    status = slab_init();
    if (status != MC_OK) return false;
    return true;
}

void local_back(struct item *value) {
    if (value == NULL) return;
    item_remove(value);
}

struct item *local_get(const char *key, uint16_t nkey) {
	if (key == NULL || nkey <= 0) return NULL;
    return item_get(key, nkey);
}

bool local_put(char *key, uint16_t nkey, int exptime, char *value, uint32_t nbyte) {
    if (key == NULL || value == NULL || nkey <= 0 || nbyte <= 0 || exptime < 0) return false;
	uint8_t id = item_slabid(nkey, nbyte);
    if (id == SLABCLASS_INVALID_ID) return false;
    struct item *store = item_alloc(id, key, nkey, exptime, value, nbyte);
    return store == NULL ? false : true;
}
