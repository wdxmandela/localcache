#include "item.h"
#include "slabs.h"
#include <stdio.h>

extern struct settings settings;
extern pthread_mutex_t cache_lock;

struct slab_heapinfo {
    uint8_t         *base;
    uint8_t         *curr;
    uint32_t        nslab;
    uint32_t        max_nslab;
    struct slab     **slab_table;
    struct slab_tqh slab_lruq;
};

struct slabclass slabclass[SLABCLASS_MAX_IDS];
uint8_t slabclass_max_id;
static struct slab_heapinfo heapinfo;

size_t slab_size(void) {
    return settings.slab_size - SLAB_HDR_SIZE;
}

void slab_acquire_refcount(struct slab *slab) {
    assert(pthread_mutex_trylock(&cache_lock) != 0);
    assert(slab->magic == SLAB_MAGIC);
    slab->refcount++;
}

void slab_release_refcount(struct slab *slab) {
    assert(pthread_mutex_trylock(&cache_lock) != 0);
    assert(slab->magic == SLAB_MAGIC);
    assert(slab->refcount > 0);
    slab->refcount--;
}

static struct item* slab_2_item(struct slab *slab, uint32_t idx, size_t size) {
    struct item *it;
    uint32_t offset = idx * size;
    assert(slab->magic == SLAB_MAGIC);
    assert(offset < settings.slab_size);
    it = (struct item *)((uint8_t *)slab->data + offset);
    return it;
}

size_t slab_item_size(uint8_t id) {
    assert(id >= SLABCLASS_MIN_ID && id <= slabclass_max_id);
    return slabclass[id].size;
}

uint8_t slab_id(size_t size) {
    uint8_t id, imin, imax;
    assert(size != 0);
    imin = SLABCLASS_MIN_ID;
    imax = slabclass_max_id;
    while (imax >= imin) {
        id = (imin + imax) / 2;
        if (size > slabclass[id].size) {
            imin = id + 1;
        } else if (id > SLABCLASS_MIN_ID && size <= slabclass[id - 1].size) {
            imax = id - 1;
        } else {
            break;
        }
    }
    if (imin > imax) {
        return SLABCLASS_INVALID_ID;
    }
    return id;
}

static void slab_slabclass_init(void) {
    uint8_t id;
    size_t *profile;
    profile = settings.profile;
    slabclass_max_id = settings.profile_last_id;
    assert(slabclass_max_id <= SLABCLASS_MAX_ID);
    for (id = SLABCLASS_MIN_ID; id <= slabclass_max_id; id++) {
        struct slabclass *p;
        uint32_t nitem;
        size_t item_sz;
        nitem = slab_size() / profile[id];
        item_sz = profile[id];
        p = &slabclass[id];
        p->nitem = nitem;
        p->size = item_sz;
        p->nfree_itemq = 0;
        TAILQ_INIT(&p->free_itemq);
        p->nfree_item = 0;
        p->free_item = NULL;
    }
}

static rstatus_t slab_heapinfo_init(void) {
    heapinfo.nslab = 0;
    heapinfo.max_nslab = settings.maxbytes / settings.slab_size;
    heapinfo.base = NULL;
    if (settings.prealloc) {
        heapinfo.base = malloc(heapinfo.max_nslab * settings.slab_size);
        if (heapinfo.base == NULL) {
            return MC_ENOMEM;
        }
    }
    heapinfo.curr = heapinfo.base;
    heapinfo.slab_table = malloc(sizeof(*heapinfo.slab_table) * heapinfo.max_nslab);
    if (heapinfo.slab_table == NULL) {
        return MC_ENOMEM;
    }
    TAILQ_INIT(&heapinfo.slab_lruq);
    return MC_OK;
}

rstatus_t slab_init(void) {
    rstatus_t status;
    slab_slabclass_init();
    status = slab_heapinfo_init();
    return status;
}

static void slab_hdr_init(struct slab *slab, uint8_t id) {
    assert(id >= SLABCLASS_MIN_ID && id <= slabclass_max_id);
    slab->magic = SLAB_MAGIC;
    slab->id = id;
    slab->unused = 0;
    slab->refcount = 0;
}

static bool slab_heap_full(void) {
    return (heapinfo.nslab >= heapinfo.max_nslab);
}

static struct slab* slab_heap_alloc(void) {
    struct slab *slab;
    if (settings.prealloc) {
        slab = (struct slab *)heapinfo.curr;
        heapinfo.curr += settings.slab_size;
    } else {
        slab = malloc(settings.slab_size);
    }
    return slab;
}

static void slab_table_update(struct slab *slab) {
    assert(heapinfo.nslab < heapinfo.max_nslab);
    heapinfo.slab_table[heapinfo.nslab] = slab;
    heapinfo.nslab++;
}

static struct slab* slab_table_rand(void) {
    uint32_t rand_idx;
    rand_idx = (uint32_t)rand() % heapinfo.nslab;
    return heapinfo.slab_table[rand_idx];
}

static struct slab* slab_lruq_head() {
    return TAILQ_FIRST(&heapinfo.slab_lruq);
}

static void slab_lruq_append(struct slab *slab) {
    TAILQ_INSERT_TAIL(&heapinfo.slab_lruq, slab, s_tqe);
}

static void slab_lruq_remove(struct slab *slab) {
    TAILQ_REMOVE(&heapinfo.slab_lruq, slab, s_tqe);
}

static struct slab* slab_get_new(void) {
    struct slab *slab;
    if (slab_heap_full()) {
        return NULL;
    }
    slab = slab_heap_alloc();
    if (slab == NULL) {
        return NULL;
    }
    slab_table_update(slab);
    return slab;
}

static void _slab_link_lruq(struct slab *slab) {
    slab->utime = time_now();
    slab_lruq_append(slab);
}

static void _slab_unlink_lruq(struct slab *slab) {
    slab_lruq_remove(slab);
}

static void slab_evict_one(struct slab *slab) {
    struct slabclass *p;
    struct item *it;
    uint32_t i;
    p = &slabclass[slab->id];
    if (p->free_item != NULL && slab == item_2_slab(p->free_item)) {
        p->nfree_item = 0;
        p->free_item = NULL;
    }
    for (i = 0; i < p->nitem; i++) {
        it = slab_2_item(slab, i, p->size);
        assert(it->magic == ITEM_MAGIC);
        assert(it->refcount == 0);
        assert(it->offset != 0);
        if (item_is_linked(it)) {
            item_reuse(it);
        } else if (item_is_slabbed(it)) {
            assert(slab == item_2_slab(it));
            assert(!TAILQ_EMPTY(&p->free_itemq));
            it->flags &= ~ITEM_SLABBED;
            assert(p->nfree_itemq > 0);
            p->nfree_itemq--;
            TAILQ_REMOVE(&p->free_itemq, it, i_tqe);
        }
    }
    slab_lruq_remove(slab);
}

static struct slab* slab_evict_rand(void) {
    struct slab *slab;
    uint32_t tries;
    tries = SLAB_RAND_MAX_TRIES;
    do {
        slab = slab_table_rand();
        tries--;
    } while (tries > 0 && slab->refcount != 0);
    if (tries == 0) {
        return NULL;
    }
    slab_evict_one(slab);
    return slab;
}

static struct slab* slab_evict_lru(int id) {
    struct slab *slab;
    uint32_t tries;
    for (tries = SLAB_LRU_MAX_TRIES, slab = slab_lruq_head(); tries > 0 && slab != NULL; tries--, slab = TAILQ_NEXT(slab, s_tqe)) {
        if (slab->refcount == 0) {
            break;
        }
    }
    if (tries == 0 || slab == NULL) {
        return NULL;
    }
    slab_evict_one(slab);
    return slab;
}

static void slab_add_one(struct slab *slab, uint8_t id) {
    struct slabclass *p;
    struct item *it;
    uint32_t i, offset;
    p = &slabclass[id];
    slab_hdr_init(slab, id);
    slab_lruq_append(slab);
    for (i = 0; i < p->nitem; i++) {
        it = slab_2_item(slab, i, p->size);
        offset = (uint32_t)((uint8_t *)it - (uint8_t *)slab);
        item_hdr_init(it, offset, id);
    }
    p->nfree_item = p->nitem;
    p->free_item = (struct item *)&slab->data[0];
}

static rstatus_t slab_get(uint8_t id) {
    rstatus_t status;
    struct slab *slab;
    assert(slabclass[id].free_item == NULL);
    assert(TAILQ_EMPTY(&slabclass[id].free_itemq));
    slab = slab_get_new();
    if (slab == NULL && (settings.evict_opt & (EVICT_CS | EVICT_AS))) {
        slab = slab_evict_lru(id);
    }
    if (slab == NULL && (settings.evict_opt & EVICT_RS)) {
        slab = slab_evict_rand();
    }
    if (slab != NULL) {
        slab_add_one(slab, id);
        status = MC_OK;
    } else {
        status = MC_ENOMEM;
    }
    return status;
}

static struct item* slab_get_item_from_freeq(uint8_t id) {
    struct slabclass *p;
    struct item *it;
    if (!settings.use_freeq) {
        return NULL;
    }
    p = &slabclass[id];
    if (p->nfree_itemq == 0) {
        return NULL;
    }
    it = TAILQ_FIRST(&p->free_itemq);
    assert(it->magic == ITEM_MAGIC);
    assert(item_is_slabbed(it));
    assert(!item_is_linked(it));
    it->flags &= ~ITEM_SLABBED;
    assert(p->nfree_itemq > 0);
    p->nfree_itemq--;
    TAILQ_REMOVE(&p->free_itemq, it, i_tqe);
    return it;
}

static struct item* _slab_get_item(uint8_t id) {
    struct slabclass *p;
    struct item *it;
    p = &slabclass[id];
    it = slab_get_item_from_freeq(id);
    if (it != NULL) {
        return it;
    }
    if (p->free_item == NULL && (slab_get(id) != MC_OK)) {
        return NULL;
    }
    it = p->free_item;
    if (--p->nfree_item != 0) {
        p->free_item = (struct item *)(((uint8_t *)p->free_item) + p->size);
    } else {
        p->free_item = NULL;
    }
    return it;
}

struct item* slab_get_item(uint8_t id) {
    struct item *it;
    assert(id >= SLABCLASS_MIN_ID && id <= slabclass_max_id);
    it = _slab_get_item(id);
    return it;
}

static void slab_put_item_into_freeq(struct item *it) {
    uint8_t id = it->id;
    struct slabclass *p = &slabclass[id];
    assert(id >= SLABCLASS_MIN_ID && id <= slabclass_max_id);
    assert(item_2_slab(it)->id == id);
    assert(!item_is_linked(it));
    assert(!item_is_slabbed(it));
    assert(it->refcount == 0);
    assert(it->offset != 0);
    it->flags |= ITEM_SLABBED;
    p->nfree_itemq++;
    TAILQ_INSERT_HEAD(&p->free_itemq, it, i_tqe);
}

static void _slab_put_item(struct item *it) {
    slab_put_item_into_freeq(it);
}

void slab_put_item(struct item *it) {
    _slab_put_item(it);
}

void slab_lruq_touch(struct slab *slab, bool allocated) {
    if (!(allocated && (settings.evict_opt & EVICT_CS)) && !(settings.evict_opt & EVICT_AS)) {
        return;
    }
    if (slab->utime >= (time_now() - SLAB_LRU_UPDATE_INTERVAL)) {
        return;
    }
    _slab_unlink_lruq(slab);
    _slab_link_lruq(slab);
}
