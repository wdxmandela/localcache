#ifndef LOCAL_SLABS_H_
#define LOCAL_SLABS_H_
#include "cache.h"
#include "item.h"

#define SLAB_MAGIC 0xdeadbeef
#define SLAB_HDR_SIZE offsetof(struct slab, data)
#define SLAB_MIN_SIZE ((size_t) 512)
#define SLAB_MAX_SIZE ((size_t) (128 * MB))
#define SLABCLASS_MIN_ID 1
#define SLABCLASS_MAX_ID (UCHAR_MAX - 1)
#define SLABCLASS_INVALID_ID UCHAR_MAX
#define SLABCLASS_MAX_IDS UCHAR_MAX
#define SLAB_RAND_MAX_TRIES 50
#define SLAB_LRU_MAX_TRIES 50
#define SLAB_LRU_UPDATE_INTERVAL 1

struct slab {
    uint32_t          magic;
    uint8_t           id;
    uint8_t           unused;
    uint16_t          refcount;
    TAILQ_ENTRY(slab) s_tqe;
    int               utime;
    uint8_t           data[1];
};

TAILQ_HEAD(slab_tqh, slab);

struct slabclass {
    uint32_t        nitem;
    size_t          size;
    uint32_t        nfree_itemq;
    struct item_tqh free_itemq;
    uint32_t        nfree_item;
    struct item     *free_item;
};

size_t slab_size(void);
void slab_acquire_refcount(struct slab *slab);
void slab_release_refcount(struct slab *slab);
size_t slab_item_size(uint8_t id);
uint8_t slab_id(size_t size);
rstatus_t slab_init(void);
struct item *slab_get_item(uint8_t id);
void slab_put_item(struct item *it);
void slab_lruq_touch(struct slab *slab, bool allocated);

#endif

