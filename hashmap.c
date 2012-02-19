#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "hashmap.h"


//#define _DEBUG_ENABLE_
#ifdef _DEBUG_ENABLE_
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG(...)
#endif


#define KEY_MAX_LEN     (32)
#define INIT_KEY        ("")
#define CRASH_ADDRESS   (0xFFFFFF00000000)  /*! Inhibit Access Area.(System Dependent) */
#define HANDLE_START_ID (55)                /*! Value has no meaning. */
#define INVALID_CORD    (-1)


/*! Check Initialized and Exit */
#define PRE_SAFE_CHECK(handle, ret, ercd, label)                                      \
do {                                                                                  \
	if (false == map_is_init(handle)) {                                               \
		ret = ercd;                                                                   \
		fprintf(stderr, "error ! HashMapHandle is not initialized ! @%s() \n", __func__); \
		goto label;                                                                   \
	}                                                                                 \
} while (0)

#define PRE_KEY_CHECK(key, ret, ercd, label)                                          \
do {                                                                                  \
	if (0 == strcmp(key, INIT_KEY)) {                                                 \
		ret = ercd;                                                                   \
		fprintf(stderr, "error ! invalid key ! @%s() \n", __func__);                  \
		goto label;                                                                   \
	}                                                                                 \
} while (0)


/*! Hash Table Data Structure */
typedef struct tag_map_data {
	char key[KEY_MAX_LEN];                /* Hash key. */
	void *container;                      /* Data stored hash table. */
} HashData;


/*! Map Handle Information */
struct tag_map_handle {
	int hdl_id;                           /* Handle id. Use initialize check. */
	int (* hash)(char *key, int tblsz);   /* Hash func pointer. HashMap_make can fook hash. */
	size_t cellsz;                        /* Container data size @HashData. */
	int tblsz;                            /* Hash table size. */
	int iterator_pos;                     /* Iterator position. */
	HashData *hash_table;                 /* Hash table data pointer. */
}; /* HashMapHandle define */


/*! memory release step level */
typedef enum {
	LITTLE_CLEANUP = 0,
	MIDDLE_CLEANUP,
	FULL_CLEANUP
} ECleanUpLevel;


static int hash_func_default(char *key, int tblsz);
static int *map_get_handle_id_base(void);
static int map_get_handle_id(void);
static bool map_is_init(HashMapHandle handle);
static void map_cleanup(HashMapHandle handle, ECleanUpLevel level);
static int map_find(HashMapHandle handle, char *key, int index, int *misshit);



/*=========================================================================================
 * @name:	static int hash_func_default(char *key, int tblsz)
 * @brief:	Default Hash Function
 * @note:	HashMap_make()時にhash関数が指定されなかった場合、このhash関数が使用される
 * @attention:
 =========================================================================================*/
static int hash_func_default(char *key, int tblsz)
{
	int len = strlen(key);
	int ret = (len + (4 * (key[0] + (4 * key[len/2])))) % tblsz;
	//fprintf(stderr, "%s: len=%d, key[0]=%d, key[len/2]=%d, ret=%d \n", key, len, key[len], key[len/2], ret);
	return ret;
}


/*=========================================================================================
 * @name:	static int *map_get_handle_id_base(void)
 * @brief:	Keep Handle ID
 * @note:	ハンドルIDを保持する。IDはHANDLE_START_IDから始まる。
 * @attention:
 =========================================================================================*/
static int *map_get_handle_id_base(void)
{
	static int handle_num = HANDLE_START_ID;
	return &handle_num;
}
/*=========================================================================================
 * @name:	static int map_get_handle_id(void)
 * @brief:	Manage Handle ID
 * @note:	呼び出される毎にインクリメントする。
 * @attention:
 =========================================================================================*/
static int map_get_handle_id(void)
{
	int *handle_num = map_get_handle_id_base();
	LOG("get handle_id = %d \n", (*handle_num));
	return (*handle_num)++;
}


/*=========================================================================================
 * @name:	static bool map_is_init(HashMapHandle handle)
 * @brief:	Check Initialize HashMapHandle
 * @note:
 * @attention:	C言語の初期値は不定のため、以下のチェックを入れても安全とは言えない。
 =========================================================================================*/
static bool map_is_init(HashMapHandle handle)
{
	bool is_init = true;
	int *handle_id_max = map_get_handle_id_base();
	LOG("[init check] hdl_id: %d\n", handle->hdl_id);

	if      (NULL == handle)                   { is_init = false; }
	else if (CRASH_ADDRESS <= (unsigned long)handle)  { is_init = false; }	/* このままメンバにアクセスすると吹っ飛ぶ */
	else if (HANDLE_START_ID > handle->hdl_id) { is_init = false; }
	else if (*handle_id_max <= handle->hdl_id) { is_init = false; }
	else                                       { is_init = true;  }

	return is_init;
}


/*=========================================================================================
 * @name:	static void map_cleanup(HashMapHandle handle, ECleanUpLevel level)
 * @brief:	Free Map Handle by cleanup level
 * @note:	本当はhdl_idの領域だけ残した方が0で上書きされる可能性が減って安全かもしれない
 * @attention:
 =========================================================================================*/
static void map_cleanup(HashMapHandle handle, ECleanUpLevel level)
{
	switch (level) {
		case FULL_CLEANUP:
		{
			int i;
			for (i=0; i<handle->tblsz; i++) {
				strcpy(handle->hash_table[i].key, INIT_KEY);
				free(handle->hash_table[i].container);
			}
		}
		case MIDDLE_CLEANUP:
			free(handle->hash_table);
			handle->hdl_id = INVALID_CORD;
		case LITTLE_CLEANUP:
			free(handle);
			break;
		default:
			fprintf(stderr, "error ! invalid cleanup level ! @map_cleanup() \n");
			break;
	}
}


/*=========================================================================================
 * @name:	static int map_find(HashMapHandle handle, char *key, EFindOption option)
 * @brief:	Find Key on Hash Table
 * @note:	
 * @attention:	
 =========================================================================================*/
static int map_find(HashMapHandle handle, char *key, int index, int *misshit)
{
	int i, end;
	int ret = INVALID_CORD;
	int dummy = 0;
	LOG("key=%s, hash=%d \n", key, index);

	/* Invalid misshit */
	if (NULL == misshit) { misshit = &dummy; }

	end = (0==index) ? (handle->tblsz - 1) : (index - 1);
	for (i=index; ; i=(i+1)%(handle->tblsz)) {
		//LOG("- [%2d] key=%s \n", i, handle->hash_table[i].key);
		if (0 == strcmp(handle->hash_table[i].key, key)) {
			ret = i;		/* hit */
			break;
		} else {
			(*misshit)++;
		}
		if (end == i) {
			ret = INVALID_CORD;
			break;
		}
	}

	return ret;
}


/*==
 * =======================================================================================
 * @name:	HashMapHandle HashMap_make(const size_t cellsz, const int tblsz, int (* hash_fook)(char *key, int tblsz))
 * @brief:	Make Hash Table
 * @note:	Hashテーブルサイズ、格納データ、Hash関数を引数で指定する
 *       	hash関数指定がNULLのときはdefaultの関数を利用する
 * @attention:
 =========================================================================================*/
HashMapHandle HashMap_make(const size_t cellsz, const int tblsz, int (* hash_fook)(char *key, int tblsz))
{
	HashMapHandle handle;
	int i;
	LOG("Enter %s -> \n", __func__);
	LOG("cellsz = %zd \n", cellsz);
	LOG("tblsz  = %d \n", tblsz);
	LOG("hash fook? -> %s \n", (NULL == hash_fook) ? ("no") : ("yes"));

	if (cellsz <= 0) {
		fprintf(stderr, "error ! cell size must be more than 0 ! \n");
		handle = NULL;
		goto catch_exit;
	}

	if (tblsz <= 0) {
		fprintf(stderr, "error ! table size must be more than 0 ! \n");
		handle = NULL;
		goto catch_exit;
	}

	/*! make handle */
	handle = (HashMapHandle)malloc(sizeof(struct tag_map_handle));
	if (NULL == handle) {
		fprintf(stderr, "error ! memory alocate failed ! [%zd byte] \n", sizeof(HashMapHandle));
		goto catch_exit;
	}

	/*! configure handle */
	handle->hdl_id = map_get_handle_id();
	/*! fook hash? */
	if (NULL == hash_fook) {
		handle->hash = hash_func_default;
	} else {
		/*! fook! */
		handle->hash = hash_fook;
	}

	/*! make hash table */
	handle->cellsz = cellsz;
	handle->tblsz = tblsz;
	handle->iterator_pos = 0;
	handle->hash_table = (HashData *)malloc(sizeof(HashData) * handle->tblsz);
	if (NULL == handle->hash_table) {
		fprintf(stderr, "error ! memory alocate failed ! [%zd byte] \n", sizeof(HashData) * handle->tblsz);
		map_cleanup(handle, LITTLE_CLEANUP);
		handle = NULL;
		goto catch_exit;
	}
	for (i=0; i<handle->tblsz; i++) {
		handle->hash_table[i].container = (void *)malloc(handle->cellsz);
		if (NULL == handle->hash_table[i].container) {
			fprintf(stderr, "error ! memory alocate failed ! [%zd byte] \n", sizeof(handle->cellsz));
			map_cleanup(handle, MIDDLE_CLEANUP);
			handle = NULL;
			goto catch_exit;
		}
		strcpy(handle->hash_table[i].key, INIT_KEY);
	}

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return handle;
}


/*=========================================================================================
 * @name:	int HashMap_free(HashMapHandle handle)
 * @brief:	Free Map Memory
 * @note:
 * @attention:
 =========================================================================================*/
int HashMap_free(HashMapHandle handle)
{
	int ret = OK;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, ret, NG, catch_exit);

	map_cleanup(handle, FULL_CLEANUP);

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return ret;
}


/*=========================================================================================
 * @name:	int HashMap_insert(HashMapHandle handle, char *key, void *data)
 * @brief:	Register Data on Hash Table
 * @note:
 * @attention:
 =========================================================================================*/
int HashMap_insert(HashMapHandle handle, char *key, void *data)
{
	int ret, hash_value, index;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, ret, NG, catch_exit);
	PRE_KEY_CHECK(key, ret, NG, catch_exit);

	LOG("handle_id=%d key=\"%s\" @%s \n", handle->hdl_id, key, __func__);

	/*! search to check key is already registerd */
	hash_value = handle->hash(key, handle->tblsz);
	if (INVALID_CORD < map_find(handle, key, hash_value, NULL)) {
		ret = NG;
		fprintf(stderr, "error ! \"%s\" is already registerd ! @HashMap_insert() \n", key);
		goto catch_exit;
	}

	/*! search blank table */
	index = map_find(handle, INIT_KEY, hash_value, NULL);
	if (index == INVALID_CORD) {
		fprintf(stderr, "error ! \"%s\" failed to register hash table ! @HashMap_insert() \n", key);
		ret = NG;
	} else {
		strcpy(handle->hash_table[index].key, key);
		memcpy(handle->hash_table[index].container, data, handle->cellsz);
		LOG("addr:%08lX -> %08lX (%zd B) \n", (unsigned long)data, (unsigned long)(handle->hash_table[index].container), handle->cellsz);
		ret = OK;
	}

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return ret;
}


/*=========================================================================================
 * @name:	void* HashMap_get(HashMapHandle handle, char *key)
 * @brief:	Get Hash Table Element Pointer
 * @note:	返却値はvoidポインタのため、コール側でキャストすること
 * @attention:	
 =========================================================================================*/
void* HashMap_get(HashMapHandle handle, char *key)
{
	void* ret;
	int hash_value, index;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, ret, NULL, catch_exit);
	PRE_KEY_CHECK(key, ret, NULL, catch_exit);

	hash_value = handle->hash(key, handle->tblsz);
	index = map_find(handle, key, hash_value, NULL);
	if (index == INVALID_CORD) {
		fprintf(stderr, "error ! \"%s\" isn't registered on hash table ! @HashMap_get() \n", key);
		ret = NULL;
	} else {
		ret = handle->hash_table[index].container;
	}

	LOG("index=%d addr=0x%08lX \n", index, (unsigned long)(ret));
catch_exit:
	LOG("Leave %s <- \n", __func__);
	return ret;
}


/*=========================================================================================
 * @name:	int HashMap_erase(HashMapHandle handle, char *key)
 * @brief:	Erase Hash Table Element
 * @note:	
 * @attention:	
 =========================================================================================*/
int HashMap_erase(HashMapHandle handle, char *key)
{
	int ret, hash_value, index;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, ret, NG, catch_exit);
	PRE_KEY_CHECK(key, ret, NG, catch_exit);

	hash_value = handle->hash(key, handle->tblsz);
	index = map_find(handle, key, hash_value, NULL);
	if (index == INVALID_CORD) {
		fprintf(stderr, "error ! \"%s\" isn't registered on hash table ! @HashMap_remove() \n", key);
		ret = NG;
	} else {
		/* erase(initialize) */
		strcpy(handle->hash_table[index].key, INIT_KEY);
		ret = OK;
		LOG("erase key=\"%s\" index=%d \n", key, index);
	}

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return ret;
}


/*=========================================================================================
 * @name:	int HashMap_clear(HashMapHandle handle)
 * @brief:	All Clear Hash Table (not free)
 * @note:	
 * @attention:	
 =========================================================================================*/
int HashMap_clear(HashMapHandle handle)
{
	int i, ret;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, ret, NG, catch_exit);

	for (i=0; i<handle->tblsz; i++) {
		strcpy(handle->hash_table[i].key, INIT_KEY);
	}
	ret = OK;

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return ret;
}


/*=========================================================================================
 * @name:	int HashMap_show(HashMapHandle handle)
 * @brief:	Show Current Hash Table
 * @note:	For Debug
 * @attention:	
 =========================================================================================*/
int HashMap_show(HashMapHandle handle)
{
	int i, ret = OK;
	HashData *p;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, ret, NG, catch_exit);

	for (i=0; i<handle->tblsz; i++) {
		p = &(handle->hash_table[i]);
		if (0 != strcmp(p->key, INIT_KEY)) {
			printf("[%2d] data-addr:0x%08lX key:\"%s\" hash:%2d \n",
			       i, (unsigned long)(p->container), p->key, handle->hash(p->key, handle->tblsz));
		}
	}

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return ret;
}


/*=========================================================================================
 * @name:	int HashMap_empty(HashMapHandle handle)
 * @brief:	Check Hash Table is Empty
 * @note:	return "true" or "false"
 * @attention:	
 =========================================================================================*/
bool HashMap_empty(HashMapHandle handle)
{
	bool ret = true;
	int i;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, ret, false, catch_exit);

	for (i=0; i<handle->tblsz; i++) {
		if (0 != strcmp(handle->hash_table[i].key, INIT_KEY)) {
			ret = false;
			break;
		}
	}

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return ret;
}


/*========================================================================================
 * @name:	int HashMap_maxsize(HashMapHandle handle)
 * @brief:	Get Hash Table Max Size
 * @note:	
 * @attention:	
 =========================================================================================*/
int HashMap_maxsize(HashMapHandle handle)
{
	int ret;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, ret, NG, catch_exit);

	ret = handle->tblsz;

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return ret;
}


/*========================================================================================
 * @name:	int HashMap_size(HashMapHandle handle)
 * @brief:	Get Hash Table Data Num
 * @note:	
 * @attention:	
 =========================================================================================*/
int HashMap_size(HashMapHandle handle)
{
	int i, size = 0;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, size, NG, catch_exit);

	for (i=0; i<handle->tblsz; i++) {
		if (0 != strcmp(handle->hash_table[i].key, INIT_KEY)) {
			size++;
		}
	}

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return size;
}


/*========================================================================================
 * @name:	void* HashMap_next(HashMapHandle handle)
 * @brief:	Iterator
 * @note:	
 * @attention:	
 =========================================================================================*/
void* HashMap_next(HashMapHandle handle)
{
	void *ret = NULL;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, ret, NULL, catch_exit);

	if (handle->tblsz > handle->iterator_pos) {
		int i;
		for (i=handle->iterator_pos; i<handle->tblsz; i++) {
			if (0 != strcmp(handle->hash_table[i].key, INIT_KEY)) {
				ret = handle->hash_table[i].container;
				break;
			}
		}
		handle->iterator_pos = i+1;
	}

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return ret;
}


/*========================================================================================
 * @name:	void HashMap_begin(HashMapHandle handle)
 * @brief:	Reset Iterator
 * @note:	
 * @attention:	
 =========================================================================================*/
void HashMap_begin(HashMapHandle handle)
{
	bool ret;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, ret, false, catch_exit);

	handle->iterator_pos = 0;

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return;
}


/*========================================================================================
 * @name:	void HashMap_hasNext(HashMapHandle handle)
 * @brief:	Check Hash Table End
 * @note:	Return "true" or "false".
 * @attention:	
 =========================================================================================*/
bool HashMap_hasNext(HashMapHandle handle)
{
	bool ret = false;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, ret, false, catch_exit);

	if (handle->tblsz > handle->iterator_pos) {
		int i;
		for (i=handle->iterator_pos; i<handle->tblsz; i++) {
			if (0 != strcmp(handle->hash_table[i].key, INIT_KEY)) {
				ret = true;
				break;
			}
		}
	}

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return ret;
}


/*========================================================================================
 * @name:	int HashMap_optimum(HashMapHandle handle)
 * @brief:	Check Hash Optimum Index
 * @note:	ハッシュテーブル(関数)が最適化されているかの確認。
 *       	無駄な探索をしている回数を返す。値が小さいほど最適化されている。
 * @attention:	
 =========================================================================================*/
int HashMap_optimum(HashMapHandle handle)
{
	int i, optimum_index = 0;
	LOG("Enter %s -> \n", __func__);
	PRE_SAFE_CHECK(handle, optimum_index, INVALID_CORD, catch_exit);

	for (i=0; i<handle->tblsz; i++) {
		char *search_key = handle->hash_table[i].key;
		int hash_value = handle->hash(search_key, handle->tblsz);
		int miss_hit = 0;
		if (0 == strcmp(search_key, INIT_KEY)) { continue; }
		map_find(handle, search_key, hash_value, &miss_hit);
		optimum_index += miss_hit;
	}

catch_exit:
	LOG("Leave %s <- \n", __func__);
	return optimum_index;
}

