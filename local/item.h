#ifndef LOCAL_ITEM_H_
#define LOCAL_ITEM_H_

#include "cache.h"

#define ITEM_MAGIC 0xfeedface

typedef enum item_flags {
    ITEM_LINKED  = 1,
    ITEM_SLABBED = 2,
    ITEM_RALIGN  = 4,
} item_flags_t;

struct item {
    uint32_t          magic;
    TAILQ_ENTRY(item) i_tqe;
    SLIST_ENTRY(item) h_sle;
    int               atime;
    int               exptime;
    uint32_t          nbyte;
    uint32_t          offset;
    uint16_t          refcount;
    uint8_t           flags;
    uint8_t           id;
    uint16_t          nkey;
    char              end[1];
};

SLIST_HEAD(item_slh, item);
TAILQ_HEAD(item_tqh, item);

#define ITEM_HDR_SIZE offsetof(struct item, end)

static inline char *item_key(struct item *it) {
    char *key;
    assert(it->magic == ITEM_MAGIC);
    key = it->end;
    return key;
}

static inline bool item_is_linked(struct item *it) {
    return (it->flags & ITEM_LINKED);
}

static inline bool item_is_slabbed(struct item *it) {
    return (it->flags & ITEM_SLABBED);
}

static inline bool item_is_raligned(struct item *it) {
    return (it->flags & ITEM_RALIGN);
}

static inline size_t item_ntotal(uint16_t nkey, uint32_t nbyte) {
    size_t ntotal = ITEM_HDR_SIZE + nkey + nbyte;
    return ntotal;
}

static inline size_t item_size(struct item *it) {
    assert(it->magic == ITEM_MAGIC);
    return item_ntotal(it->nkey, it->nbyte);
}

void item_init(void);
char *item_data(struct item *it);
struct slab *item_2_slab(struct item *it);
void item_reuse(struct item *it);
void item_hdr_init(struct item *it, uint32_t offset, uint8_t id);
uint8_t item_slabid(uint16_t nkey, uint32_t nbyte);
struct item *item_alloc(uint8_t id, char *key, uint16_t nkey, int exptime, char *value, uint32_t nbyte);
void item_delete(struct item *it);
void item_remove(struct item *it);
void item_touch(struct item *it);
struct item *item_get(const char *key, uint16_t nkey);

#endif

