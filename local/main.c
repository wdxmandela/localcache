#include <stdio.h>
#include "local.h"
#include "item.h"
#include "cache.h"
#include <unistd.h>

int main (int argc, const char * argv[]) {
	struct settings *settings = local_config();
	settings->hash_power = 0;
	settings->prealloc = true;
	settings->evict_opt = EVICT_LRU;
	settings->maxbytes = 200 * 1024 * 1024;
	settings->slab_size = 1024 * 1024;
	settings->use_freeq = true;
	settings->use_lruq = true;
	settings->profile_last_id = 12;
	int i = 0, j = 0, n = 0;
	for (i = 1; i < 13; i++) {
		settings->profile[i] = 100 * i;
	}
	bool result = local_start();
	if (result) printf("cache started\n");
	else {
		printf("cache started fail\n");
		return 1;
	}
	char *key = (char*)malloc(8);
	char *value = (char*)malloc(900);
	for (i = 0; i < 10000; i++) {
		for (j = 0; j < 10000; j++) {
		    int *ptr = (int*)key;
			*ptr = i;
			*(ptr++) = j;
			n = i + j;
			n = (n % 9) * 100 + 50;
			ptr = (int*)value;
			*ptr = i;
			*(ptr + n - 4) = j;
			result = local_put(key, 8, 10, value, n);
			if (!result) {
				printf("cache put item fail\n");
				printf("%d %d\n", i, j);
				return 1;
			}
			struct item *res = local_get(key, 8);
			if (res == NULL || res->nbyte != n) {
				printf("cache get item fail\n");
				printf("%d %d\n", i, j);
				return 1;
			}
			char *data = item_data(res);
			if (*((int*)data) != i) {
				printf("cache get value fail\n");
				printf("%d %d\n", i, j);
				return 1;
			}
		}
	}
    return 0;
}
