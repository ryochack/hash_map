#ifndef __HASHMAP_H__
#define __HASHMAP_H__

#include <stdio.h>
#include <stdbool.h>

#define OK (1)
#define NG (0)

typedef struct tag_map_handle *HashMapHandle;

HashMapHandle HashMap_make(const size_t cellsz, const int tblsz, int (* hash_func)(char *key, int tblsz));
int HashMap_free(HashMapHandle handle);
int HashMap_insert(HashMapHandle handle, char *key, void *data);
void* HashMap_get(HashMapHandle handle, char *key);
int HashMap_erase(HashMapHandle handle, char *key);
int HashMap_clear(HashMapHandle handle);
int HashMap_show(HashMapHandle handle);
bool HashMap_empty(HashMapHandle handle);
int HashMap_maxsize(HashMapHandle handle);
int HashMap_size(HashMapHandle handle);
void* HashMap_next(HashMapHandle handle);
void HashMap_begin(HashMapHandle handle);
bool HashMap_hasNext(HashMapHandle handle);
int HashMap_optimum(HashMapHandle handle);

#endif

