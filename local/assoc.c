#include "hash.h"
#include "item.h"
#include "assoc.h"

#define HASHSIZE(_n) (1UL << (_n))
#define HASHMASK(_n) (HASHSIZE(_n) - 1)
#define HASH_DEFAULT_MOVE_SIZE 1
#define HASH_DEFAULT_POWER 16

extern struct settings settings;
extern pthread_mutex_t cache_lock;
//primary hash table
static struct item_slh *primary_hashtable;
//older hash table
static struct item_slh *old_hashtable;
//hash item size
static uint32_t nhash_item;
//hash power
static uint32_t hash_power;
//expanding flag
static int expanding;
//size to move per transfer
static uint32_t nhash_move_size;
//size transfered
static uint32_t expand_bucket;
//maintenance thread related
static pthread_cond_t maintenance_cond;
static pthread_t maintenance_tid;
//maintenance thread switch
static volatile int run_maintenance_thread;

static struct item_slh *assoc_create_table(uint32_t table_sz) {
    struct item_slh *table = NULL;
    uint32_t i;
    table = malloc(sizeof(*table) * table_sz);
    if (table == NULL) return NULL;
    for (i = 0; i < table_sz; i++) {
        SLIST_INIT(&table[i]);
    }
    return table;
}

static void *assoc_maintenance_thread(void *arg) {
    uint32_t i, hv;
    struct item_slh *old_bucket, *new_bucket;
    struct item *it, *next;
    while (run_maintenance_thread) {
        pthread_mutex_lock(&cache_lock);
        for (i = 0; i < nhash_move_size && expanding == 1; i++) {
            old_bucket = &old_hashtable[expand_bucket];
            SLIST_FOREACH_SAFE(it, old_bucket, h_sle, next) {
                hv = hash(item_key(it), it->nkey, 0);
                new_bucket = &primary_hashtable[hv & HASHMASK(hash_power)];
                SLIST_REMOVE(old_bucket, it, item, h_sle);
                SLIST_INSERT_HEAD(new_bucket, it, h_sle);
            }
            expand_bucket++;
            if (expand_bucket == HASHSIZE(hash_power - 1)) {
                expanding = 0;
                free(old_hashtable);
            }
        }
        if (expanding == 0) {
            pthread_cond_wait(&maintenance_cond, &cache_lock);
        }
        pthread_mutex_unlock(&cache_lock);
    }
    return NULL;
}

static rstatus_t assoc_start_maintenance_thread(void) {
	int err;
    err = pthread_create(&maintenance_tid, NULL, assoc_maintenance_thread, NULL);
    if (err != 0) {
        return MC_ERROR;
    }
    return MC_OK;
}

rstatus_t assoc_init(void) {
    rstatus_t status;
    uint32_t hashtable_sz;
    primary_hashtable = NULL;
    hash_power = settings.hash_power > 0 ? settings.hash_power : HASH_DEFAULT_POWER;
    old_hashtable = NULL;
    nhash_move_size = HASH_DEFAULT_MOVE_SIZE;
    nhash_item = 0;
    expanding = 0;
    expand_bucket = 0;
    hashtable_sz = HASHSIZE(hash_power);
    primary_hashtable = assoc_create_table(hashtable_sz);
    if (primary_hashtable == NULL) {
        return MC_ENOMEM;
    }
    pthread_cond_init(&maintenance_cond, NULL);
    run_maintenance_thread = 1;
    status = assoc_start_maintenance_thread();
    if (status != MC_OK) {
        return status;
    }
    return MC_OK;
}

static void assoc_stop_maintenance_thread(void) {
    pthread_mutex_lock(&cache_lock);
    run_maintenance_thread = 0;
    pthread_cond_signal(&maintenance_cond);
    pthread_mutex_unlock(&cache_lock);
    pthread_join(maintenance_tid, NULL);
}

void assoc_deinit(void) {
    assoc_stop_maintenance_thread();
}

static struct item_slh *assoc_get_bucket(const char *key, size_t nkey) {
    struct item_slh *bucket;
    uint32_t hv, oldbucket, curbucket;
    hv = hash(key, nkey, 0);
    oldbucket = hv & HASHMASK(hash_power - 1);
    curbucket = hv & HASHMASK(hash_power);
    if ((expanding == 1) && oldbucket >= expand_bucket) {
        bucket = &old_hashtable[oldbucket];
    } else {
        bucket = &primary_hashtable[curbucket];
    }
    return bucket;
}

struct item* assoc_find(const char *key, size_t nkey) {
    struct item_slh *bucket;
    struct item *it;
    uint32_t depth;
    assert(pthread_mutex_trylock(&cache_lock) != 0);
    assert(key != NULL && nkey != 0);
    bucket = assoc_get_bucket(key, nkey);
    for (depth = 0, it = SLIST_FIRST(bucket); it != NULL; depth++, it = SLIST_NEXT(it, h_sle)) {
        if ((nkey == it->nkey) && (memcmp(key, item_key(it), nkey) == 0)) {
            break;
        }
    }
    return it;
}

static bool assoc_expand_needed(void) {
    return ((settings.hash_power == 0) && (expanding == 0) && (nhash_item > (HASHSIZE(hash_power) * 3 / 2)));
}

static void assoc_expand(void) {
    uint32_t hashtable_sz = HASHSIZE(hash_power + 1);
    old_hashtable = primary_hashtable;
    primary_hashtable = assoc_create_table(hashtable_sz);
    if (primary_hashtable == NULL) {
        primary_hashtable = old_hashtable;
        return;
    }
    hash_power++;
    expanding = 1;
    expand_bucket = 0;
    pthread_cond_signal(&maintenance_cond);
}

void assoc_insert(struct item *it) {
    struct item_slh *bucket;
    assert(pthread_mutex_trylock(&cache_lock) != 0);
    assert(assoc_find(item_key(it), it->nkey) == NULL);
    bucket = assoc_get_bucket(item_key(it), it->nkey);
    SLIST_INSERT_HEAD(bucket, it, h_sle);
    nhash_item++;
    if (assoc_expand_needed()) {
        assoc_expand();
    }
}

void assoc_delete(const char *key, size_t nkey) {
    struct item_slh *bucket;
    struct item *it, *prev;
    assert(pthread_mutex_trylock(&cache_lock) != 0);
    assert(assoc_find(key, nkey) != NULL);
    bucket = assoc_get_bucket(key, nkey);
    for (prev = NULL, it = SLIST_FIRST(bucket); it != NULL; prev = it, it = SLIST_NEXT(it, h_sle)) {
        if ((nkey == it->nkey) && (memcmp(key, item_key(it), nkey) == 0)) {
            break;
        }
    }
    if (prev == NULL) {
        SLIST_REMOVE_HEAD(bucket, h_sle);
    } else {
        SLIST_REMOVE_AFTER(prev, h_sle);
    }
    nhash_item--;
}
