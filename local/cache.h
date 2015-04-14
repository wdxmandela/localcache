#ifndef LOCAL_CACHE_H_
#define LOCAL_CACHE_H_

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include<sys/time.h>
#include <string.h>
#include <assert.h>

#define MC_OK 0
#define MC_ERROR -1
#define MC_EAGAIN -2
#define MC_ENOMEM -3
typedef int rstatus_t;

#define EVICT_NONE 0x00 //no eviction
#define EVICT_LRU 0x01 //lru
#define EVICT_RS 0x02 //random
#define EVICT_AS 0x04 //least accessed
#define EVICT_CS 0x08 //least created
#define EVICT_INVALID 0x10 //go no further

struct settings {
	int     hash_power;
	bool    prealloc;
	int     evict_opt;
	size_t  maxbytes;
	size_t  profile[UCHAR_MAX];
    uint8_t profile_last_id;
	bool    use_freeq;
	size_t  slab_size;
	bool    use_lruq;
};

#define TAILQ_ENTRY(type) \
struct { \
    struct type *tqe_next; \
    struct type **tqe_prev; \
}

#define SLIST_ENTRY(type) \
struct { \
    struct type *sle_next; \
}

#define SLIST_FIRST(head) ((head)->slh_first)
#define SLIST_NEXT(elm, field) ((elm)->field.sle_next)

#define SLIST_INIT(head) do { \
    SLIST_FIRST((head)) = NULL; \
} while (0)

#define SLIST_FOREACH_SAFE(var, head, field, tvar) \
for ((var) = SLIST_FIRST((head)); \
(var) && ((tvar) = SLIST_NEXT((var), field), 1); (var) = (tvar))

#define SLIST_INSERT_HEAD(head, elm, field) do { \
    SLIST_NEXT((elm), field) = SLIST_FIRST((head)); \
    SLIST_FIRST((head)) = (elm); \
} while (0)

#define SLIST_REMOVE(head, elm, type, field) do { \
    if (SLIST_FIRST((head)) == (elm)) { \
        SLIST_REMOVE_HEAD((head), field); \
    } else { \
        struct type *curelm = SLIST_FIRST((head)); \
        while (SLIST_NEXT(curelm, field) != (elm)) { \
            curelm = SLIST_NEXT(curelm, field); \
        } \
        SLIST_REMOVE_AFTER(curelm, field); \
    } \
} while (0)

#define SLIST_REMOVE_AFTER(elm, field) do { \
    SLIST_NEXT(elm, field) = SLIST_NEXT(SLIST_NEXT(elm, field), field); \
} while (0)

#define SLIST_REMOVE_HEAD(head, field) do { \
    SLIST_FIRST((head)) = SLIST_NEXT(SLIST_FIRST((head)), field); \
} while (0)

#define SLIST_HEAD(name, type) \
struct name { \
    struct type *slh_first; \
}

#define STAILQ_HEAD(name, type) \
struct name { \
    struct type *stqh_first; \
    struct type **stqh_last; \
}

#define TAILQ_HEAD(name, type) \
struct name { \
    struct type *tqh_first; \
    struct type **tqh_last; \
}

#define TAILQ_ENTRY(type) \
struct { \
    struct type *tqe_next; \
    struct type **tqe_prev; \
}

#define TAILQ_EMPTY(head) ((head)->tqh_first == NULL)
#define TAILQ_FIRST(head) ((head)->tqh_first)
#define TAILQ_LAST(head, headname) (*(((struct headname *)((head)->tqh_last))->tqh_last))
#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)

#define TAILQ_INIT(head) do { \
    TAILQ_FIRST((head)) = NULL; \
    (head)->tqh_last = &TAILQ_FIRST((head)); \
} while (0)

#define TAILQ_INSERT_TAIL(head, elm, field) do { \
    TAILQ_NEXT((elm), field) = NULL; \
    (elm)->field.tqe_prev = (head)->tqh_last; \
    *(head)->tqh_last = (elm); \
    (head)->tqh_last = &TAILQ_NEXT((elm), field); \
} while (0)

#define TAILQ_REMOVE(head, elm, field) do { \
    if ((TAILQ_NEXT((elm), field)) != NULL) { \
        TAILQ_NEXT((elm), field)->field.tqe_prev = (elm)->field.tqe_prev; \
    } else { \
        (head)->tqh_last = (elm)->field.tqe_prev; \
    } \
    *(elm)->field.tqe_prev = TAILQ_NEXT((elm), field); \
} while (0)

#define TAILQ_INSERT_HEAD(head, elm, field) do { \
    if ((TAILQ_NEXT((elm), field) = TAILQ_FIRST((head))) != NULL) \
        TAILQ_FIRST((head))->field.tqe_prev = &TAILQ_NEXT((elm), field); \
    else \
        (head)->tqh_last = &TAILQ_NEXT((elm), field); \
    TAILQ_FIRST((head)) = (elm); \
    (elm)->field.tqe_prev = &TAILQ_FIRST((head)); \
} while (0)

int time_now(void);
rstatus_t time_init(void);
void time_deinit(void);

#endif


