#include "item.h"
#include "assoc.h"
#include "slabs.h"

extern struct settings settings;

#define ITEM_UPDATE_INTERVAL 3
#define ITEM_LRUQ_MAX_TRIES 50

pthread_mutex_t cache_lock;
struct item_tqh item_lruq[SLABCLASS_MAX_IDS];

static bool item_expired(struct item *it) {
    assert(it->magic == ITEM_MAGIC);
    return (it->exptime > 0 && it->exptime < time_now()) ? true : false;
}

void item_init(void) {
    uint8_t i;
    pthread_mutex_init(&cache_lock, NULL);
    for (i = SLABCLASS_MIN_ID; i <= SLABCLASS_MAX_ID; i++) {
        TAILQ_INIT(&item_lruq[i]);
    }
}

char* item_data(struct item *it) {
    char *data;
    assert(it->magic == ITEM_MAGIC);
    if (item_is_raligned(it)) data = (char *)it + slab_item_size(it->id) - it->nbyte;
    else data = it->end + it->nkey;
    return data;
}

struct slab* item_2_slab(struct item *it) {
    struct slab *slab;
    assert(it->magic == ITEM_MAGIC);
    assert(it->offset < settings.slab_size);
    slab = (struct slab *)((uint8_t *)it - it->offset);
    assert(slab->magic == SLAB_MAGIC);
    return slab;
}

static void item_acquire_refcount(struct item *it) {
	assert(pthread_mutex_trylock(&cache_lock) != 0);
	assert(it->magic == ITEM_MAGIC);
    it->refcount++;
    slab_acquire_refcount(item_2_slab(it));
}

static void item_release_refcount(struct item *it) {
	assert(pthread_mutex_trylock(&cache_lock) != 0);
	assert(it->magic == ITEM_MAGIC);
	assert(it->refcount > 0);
    it->refcount--;
    slab_release_refcount(item_2_slab(it));
}

void item_hdr_init(struct item *it, uint32_t offset, uint8_t id) {
	assert(offset >= SLAB_HDR_SIZE && offset < settings.slab_size);
    it->magic = ITEM_MAGIC;
    it->offset = offset;
    it->id = id;
    it->refcount = 0;
    it->flags = 0;
}

static void item_link_q(struct item *it, bool allocated) {
    uint8_t id = it->id;
    assert(id >= SLABCLASS_MIN_ID && id <= SLABCLASS_MAX_ID);
    assert(it->magic == ITEM_MAGIC);
    assert(!item_is_slabbed(it));
    it->atime = time_now();
    TAILQ_INSERT_TAIL(&item_lruq[id], it, i_tqe);
    slab_lruq_touch(item_2_slab(it), allocated);
}

static void item_unlink_q(struct item *it) {
    uint8_t id = it->id;
    assert(id >= SLABCLASS_MIN_ID && id <= SLABCLASS_MAX_ID);
    assert(it->magic == ITEM_MAGIC);
    TAILQ_REMOVE(&item_lruq[id], it, i_tqe);
}

void item_reuse(struct item *it) {
	assert(pthread_mutex_trylock(&cache_lock) != 0);
	assert(it->magic == ITEM_MAGIC);
	assert(!item_is_slabbed(it));
	assert(item_is_linked(it));
	assert(it->refcount == 0);
    it->flags &= ~ITEM_LINKED;
    assoc_delete(item_key(it), it->nkey);
    item_unlink_q(it);
}

static struct item* item_get_from_lruq(uint8_t id) {
    struct item *it;
    struct item *uit;
    uint32_t tries;
    if (!settings.use_lruq) {
        return NULL;
    }
    for (tries = ITEM_LRUQ_MAX_TRIES, it = TAILQ_FIRST(&item_lruq[id]), uit = NULL;
	it != NULL && tries > 0; tries--, it = TAILQ_NEXT(it, i_tqe)) {
        if (it->refcount != 0) {
            continue;
        }
        if (item_expired(it)) {
            return it;
        } else if (uit == NULL) {
            uit = it;
        }
    }
    return uit;
}

uint8_t item_slabid(uint16_t nkey, uint32_t nbyte) {
    size_t ntotal;
    uint8_t id;
    ntotal = item_ntotal(nkey, nbyte);
    id = slab_id(ntotal);
    return id;
}

static struct item* _item_alloc(uint8_t id, char *key, uint16_t nkey, int exptime, char *value, uint32_t nbyte) {
    struct item *it;
    struct item *uit;
    assert(id >= SLABCLASS_MIN_ID && id <= SLABCLASS_MAX_ID);
    it = item_get_from_lruq(id);
    if (it != NULL && item_expired(it)) {
        item_reuse(it);
        goto done;
    }
    uit = (settings.evict_opt & EVICT_LRU)? it : NULL;
    it = slab_get_item(id);
    if (it != NULL) {
        goto done;
    }
    if (uit != NULL) {
        it = uit;
        item_reuse(it);
        goto done;
    }
    return NULL;
done:
    assert(it->id == id);
    assert(!item_is_linked(it));
    assert(!item_is_slabbed(it));
    assert(it->offset != 0);
    assert(it->refcount == 0);
    it->flags = 0;
    it->nbyte = nbyte;
    it->exptime = exptime + time_now();
    it->nkey = nkey;
    memcpy(item_key(it), key, nkey);
    memcpy(item_key(it) + nkey, value, nbyte);
    return it;
}

static void item_free(struct item *it) {
	assert(it->magic == ITEM_MAGIC);
    slab_put_item(it);
}

static void _item_link(struct item *it) {
	assert(it->magic == ITEM_MAGIC);
	assert(!item_is_linked(it));
	assert(!item_is_slabbed(it));
    it->flags |= ITEM_LINKED;
    assoc_insert(it);
    item_link_q(it, true);
}

static void _item_unlink(struct item *it) {
	assert(it->magic == ITEM_MAGIC);
	assert(item_is_linked(it));
    if (item_is_linked(it)) {
        it->flags &= ~ITEM_LINKED;
        assoc_delete(item_key(it), it->nkey);
        item_unlink_q(it);
        if (it->refcount == 0) {
            item_free(it);
        }
    }
}

static void _item_remove(struct item *it) {
	assert(it->magic == ITEM_MAGIC);
	assert(!item_is_slabbed(it));
    if (it->refcount != 0) {
        item_release_refcount(it);
    }
    if (it->refcount == 0 && !item_is_linked(it)) {
        item_free(it);
    }
}

void item_remove(struct item *it) {
    pthread_mutex_lock(&cache_lock);
    _item_remove(it);
    pthread_mutex_unlock(&cache_lock);
}

void item_delete(struct item *it) {
    pthread_mutex_lock(&cache_lock);
    _item_unlink(it);
    _item_remove(it);
    pthread_mutex_unlock(&cache_lock);
}

static void _item_touch(struct item *it) {
	assert(it->magic == ITEM_MAGIC);
	assert(!item_is_slabbed(it));
    if (it->atime >= (time_now() - ITEM_UPDATE_INTERVAL)) {
        return;
    }
    assert(item_is_linked(it));
    item_unlink_q(it);
    item_link_q(it, false);
}

void item_touch(struct item *it) {
    if (it->atime >= (time_now() - ITEM_UPDATE_INTERVAL)) {
        return;
    }
    pthread_mutex_lock(&cache_lock);
    _item_touch(it);
    pthread_mutex_unlock(&cache_lock);
}

static void _item_replace(struct item *it, struct item *nit) {
    assert(it->magic == ITEM_MAGIC);
    assert(!item_is_slabbed(it));
    assert(nit->magic == ITEM_MAGIC);
    assert(!item_is_slabbed(nit));
    _item_unlink(it);
    _item_link(nit);
}

static struct item* _item_get(const char *key, uint16_t nkey) {
    struct item *it;
    it = assoc_find(key, nkey);
    if (it == NULL) return NULL;
    if (it->exptime != 0 && it->exptime <= time_now()) {
        _item_unlink(it);
        return NULL;
    }
    item_acquire_refcount(it);
    _item_touch(it);
    return it;
}

struct item* item_get(const char *key, uint16_t nkey) {
    struct item *it;
    pthread_mutex_lock(&cache_lock);
    it = _item_get(key, nkey);
    pthread_mutex_unlock(&cache_lock);
    return it;
}

struct item *item_alloc(uint8_t id, char *key, uint16_t nkey, int exptime, char *value, uint32_t nbyte) {
    struct item *it, *oit;
    pthread_mutex_lock(&cache_lock);
    it = _item_alloc(id, key, nkey, exptime, value, nbyte);
    if (it == NULL) return NULL;
    oit = _item_get(key, nkey);
    if (oit != NULL) _item_replace(oit, it);
    else {
    	_item_link(it);
    }
    if (oit != NULL) _item_remove(oit);
    pthread_mutex_unlock(&cache_lock);
    return it;
}
