/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2014 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:   Liexusong (c) Liexusong@qq.com                             |
  +----------------------------------------------------------------------+
*/

/* $Id: header,v 1.16.2.1.2.1.2.1 2008/02/07 19:39:50 iliaa Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_pcache.h"
#include "ncx_slab.h"
#include "ncx_shm.h"
#include "ncx_lock.h"
#include "list.h"

#include <sys/types.h>
#include <unistd.h>
#include <time.h>


#define PCACHE_KEY_MAX       256
#define PCACHE_VAL_MAX       65535
#define PCACHE_BUCKETS_SIZE  1021

#if PHP_VERSION_ID >= 70000

#define NEW_VALUE_LEN ZSTR_LEN(new_value)
#define NEW_VALUE ZSTR_VAL(new_value)
#define _RETURN_STRINGL(k,l) RETURN_STRINGL(k,l)

#else

#define NEW_VALUE_LEN new_value_length
#define NEW_VALUE new_value
#define _RETURN_STRINGL(k,l) RETURN_STRINGL(k,l,0)

#endif

typedef struct pcache_cache  pcache_cache_t;

struct pcache_cache {
    struct list_head hash;
    struct list_head lru;
    long             expire;
    u_char           key_size;
    u_short          val_size;
    char             data[0];
};

struct pcache_status {
    int miss;
    int hits;
    int fails;
    int oom;
    int used;
};

/* True global resources - no need for thread safety here */
static ncx_shm_t cache_shm;
static ncx_slab_pool_t *cache_pool;

static struct list_head *cache_buckets;
static ncx_atomic_t *cache_lock;
static struct list_head *cache_lru_queue;
static struct pcache_status *cache_status;

/* configure entries */
static ncx_uint_t cache_size = 10485760; /* 10MB */
static ncx_uint_t buckets_size = PCACHE_BUCKETS_SIZE;
static int cache_gc_threshold;
static int cache_enable = 1;

int pcache_ncpu;

/* {{{ pcache_functions[]
 *
 * Every user visible function must have an entry in pcache_functions[].
 */
const zend_function_entry pcache_functions[] = {
    PHP_FE(pcache_set,    NULL)
    PHP_FE(pcache_get,    NULL)
    PHP_FE(pcache_del,    NULL)
    {NULL, NULL, NULL}    /* Must be the last line in pcache_functions[] */
};
/* }}} */

/* {{{ pcache_module_entry
 */
zend_module_entry pcache_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
    STANDARD_MODULE_HEADER,
#endif
    "pcache",
    pcache_functions,
    PHP_MINIT(pcache),
    PHP_MSHUTDOWN(pcache),
    PHP_RINIT(pcache),
    PHP_RSHUTDOWN(pcache),
    PHP_MINFO(pcache),
#if ZEND_MODULE_API_NO >= 20010901
    "0.3", /* Replace with version number for your extension */
#endif
    STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_PCACHE
ZEND_GET_MODULE(pcache)
#endif


void pcache_atoi(const char *str, int *ret, int *len)
{
    const char *ptr = str;
    char ch;
    int absolute = 1;
    int rlen, result;

    ch = *ptr;

    if (ch == '-') {
        absolute = -1;
        ++ptr;
    } else if (ch == '+') {
        absolute = 1;
        ++ptr;
    }

    for (rlen = 0, result = 0; *ptr != '\0'; ptr++) {
        ch = *ptr;

        if (ch >= '0' && ch <= '9') {
            result = result * 10 + (ch - '0');
            rlen++;
        } else {
            break;
        }
    }

    if (ret) *ret = absolute * result;
    if (len) *len = rlen;
}


ZEND_INI_MH(pcache_set_enable)
{
    if (NEW_VALUE_LEN == 0) {
        return FAILURE;
    }

    if (!strcasecmp(NEW_VALUE, "on") || !strcmp(NEW_VALUE, "1")) {
        cache_enable = 1;
    } else {
        cache_enable = 0;
    }

    return SUCCESS;
}


ZEND_INI_MH(pcache_set_cache_size)
{
    int len;

    if (NEW_VALUE_LEN == 0) {
        return FAILURE;
    }

    pcache_atoi((const char *)NEW_VALUE, (int *)&cache_size, &len);

    if (len > 0 && len < NEW_VALUE_LEN) { /* have unit */
        switch (NEW_VALUE[len]) {
            case 'k':
            case 'K':
                cache_size *= 1024;
                break;
            case 'm':
            case 'M':
                cache_size *= 1024 * 1024;
                break;
            case 'g':
            case 'G':
                cache_size *= 1024 * 1024 * 1024;
                break;
            default:
                return FAILURE;
        }

    } else if (len == 0) {
        return FAILURE;
    }

    return SUCCESS;
}


ZEND_INI_MH(pcache_set_buckets_size)
{
    if (NEW_VALUE_LEN == 0) {
        return FAILURE;
    }

    buckets_size = atoi(NEW_VALUE);
    if (buckets_size < PCACHE_BUCKETS_SIZE) {
        buckets_size = PCACHE_BUCKETS_SIZE;
    }

    return SUCCESS;
}


PHP_INI_BEGIN()
    PHP_INI_ENTRY("pcache.cache_size", "10485760", PHP_INI_SYSTEM,
          pcache_set_cache_size)
    PHP_INI_ENTRY("pcache.buckets_size", "1021", PHP_INI_SYSTEM,
          pcache_set_buckets_size)
    PHP_INI_ENTRY("pcache.enable", "1", PHP_INI_SYSTEM,
          pcache_set_enable)
PHP_INI_END()


/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(pcache)
{
    void *space;
    int i;

    REGISTER_INI_ENTRIES();

    if (!cache_enable) {
        return SUCCESS;
    }

    cache_gc_threshold = cache_size * 0.9;
    if (cache_gc_threshold <= 0) {
        return FAILURE;
    }

    cache_shm.size = cache_size;

    if (ncx_shm_alloc(&cache_shm) == -1) { /* alloc share memory */
        return FAILURE;
    }

    space = (void *) cache_shm.addr;

    cache_pool = (ncx_slab_pool_t *) space;

    cache_pool->addr = space;
    cache_pool->min_shift = 3;
    cache_pool->end = space + cache_size;

    ncx_slab_init(cache_pool); /* init slab */

    /* alloc cache lock */
    cache_lock = ncx_slab_alloc_locked(cache_pool, sizeof(ncx_atomic_t));
    if (!cache_lock) {
        goto failed;
    }

    ncx_memzero(cache_lock, sizeof(ncx_atomic_t));

    /* alloc cache buckets */
    cache_buckets = ncx_slab_alloc_locked(cache_pool,
                        sizeof(struct list_head) * buckets_size);
    if (!cache_buckets) {
        goto failed;
    }

    for (i = 0; i < buckets_size; i++) {
        INIT_LIST_HEAD(&cache_buckets[i]);
    }

    /* alloc LRU queue leader */
    cache_lru_queue = ncx_slab_alloc_locked(cache_pool,
                        sizeof(struct list_head));
    if (!cache_lru_queue) {
        goto failed;
    }

    INIT_LIST_HEAD(cache_lru_queue);

    /* alloc cache status struct */
    cache_status = ncx_slab_alloc_locked(cache_pool,
                        sizeof(struct pcache_status));
    if (!cache_status) {
        goto failed;
    }

    cache_status.miss  = 0;
    cache_status.hits  = 0;
    cache_status.fails = 0;
    cache_status.oom   = 0;
    cache_status.used  = 0;

    /* get cpu's core number */
    pcache_ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (pcache_ncpu <= 0) {
        pcache_ncpu = 1;
    }

    return SUCCESS;

failed:
    ncx_shm_free(&cache_shm);
    return FAILURE;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(pcache)
{
    UNREGISTER_INI_ENTRIES();

    ncx_shm_free(&cache_shm);

    return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(pcache)
{
    return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(pcache)
{
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(pcache)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "pcache support", "enabled");
    php_info_print_table_end();

    DISPLAY_INI_ENTRIES();
}
/* }}} */


static long pcache_hash(char *key, int len)
{
    long h = 0, g;
    char *kend = key + len;

    while (key < kend) {
        h = (h << 4) + *key++;
        if ((g = (h & 0xF0000000))) {
            h = h ^ (g >> 24);
            h = h ^ g;
        }
    }

    return h;
}


void pcache_try_run_gc(int overflow)
{
    pcache_cache_t *item;
    struct list_head *curr, *prev;
    int size;

    ncx_shmtx_lock(cache_lock);

    if (cache_status.used + overflow < cache_gc_threshold) {
        ncx_shmtx_unlock(cache_lock);
        return;
    }

    list_for_each_prev_safe(curr, prev, cache_lru_queue) {
        item = list_entry(curr, pcache_cache_t, lru);

        size = item->key_size + item->val_size + sizeof(pcache_cache_t);

        list_del(&item->hash); /* delete from hashtable */
        list_del(&item->lru);  /* delete from LRU queue */

        ncx_slab_free(cache_pool, item);

        cache_status.used -= size;
        overflow -= size;

        if (overflow <= 0) {
            break;
        }
    }

    ncx_shmtx_unlock(cache_lock);
}


void pcache_flush_all()
{
    pcache_cache_t *item;
    struct list_head *curr, *next;
    int size;

    list_for_each_safe(curr, next, cache_lru_queue) {
        item = list_entry(curr, pcache_cache_t, lru);

        size = item->key_size + item->val_size + sizeof(pcache_cache_t);

        list_del(&item->hash); /* delete from hashtable */
        list_del(&item->lru);  /* delete from LRU queue */

        ncx_slab_free(cache_pool, item);

        cache_status.used -= size;
    }
}


/* {{{ proto string pcache_set(string key, string val)
   Return a boolean */
PHP_FUNCTION(pcache_set)
{
    char *key = NULL, *val = NULL;
    int key_len, val_len;
    long expire = 0;
    pcache_cache_t *item, *temp;
    struct list_head *head, *curr, *next;
    int index;
    int nsize;

    if (!cache_enable) {
        RETURN_FALSE;
    }

#if PHP_VERSION_ID >= 70000

    zend_string *pkey, *pval;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "SS|l",
          &pkey, &pval, &expire) == FAILURE)
    {
        RETURN_FALSE;
    }

    key = ZSTR_VAL(pkey);
    key_len = ZSTR_LEN(pkey);

    val = ZSTR_VAL(pval);
    val_len = ZSTR_LEN(pval);

#else

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|l",
          &key, &key_len, &val, &val_len, &expire) == FAILURE)
    {
        RETURN_FALSE;
    }

#endif

    /* key length and value length are valid? */

    if (key_len > PCACHE_KEY_MAX || val_len > PCACHE_VAL_MAX) {
        RETURN_FALSE;
    }

    if (expire > 0) {
        expire += (long)time(NULL); /* update expire time */
    }

    nsize = sizeof(pcache_cache_t) + key_len + val_len;

    pcache_try_run_gc(nsize);

    item = ncx_slab_alloc(cache_pool, nsize);
    if (!item) {
        ncx_shmtx_lock(cache_lock);

        cache_status.fails++;

        if (++cache_status.oom >= 10) {
            pcache_flush_all();
            cache_status.oom = 0;
        }

        ncx_shmtx_unlock(cache_lock);

        RETURN_FALSE;
    }

    /* init item fields */

    item->next = NULL;
    item->expire = expire;
    item->key_size = key_len;
    item->val_size = val_len;

    memcpy(item->data, key, key_len);
    memcpy(item->data + key_len, val, val_len);

    index = pcache_hash(key, key_len) % buckets_size; /* bucket index */

    ncx_shmtx_lock(cache_lock);

    head = &cache_buckets[index];

    list_for_each_safe(curr, next, head) { /* for each hash list */

        temp = list_entry(curr, pcache_cache_t, hash);

        if (item->key_size == temp->key_size &&
            !memcmp(item->data, temp->data, item->key_size))
        {
            list_del(&item->hash);
            list_del(&item->lru);
            ncx_slab_free(cache_pool, temp);
            break;
        }
    }

    list_add(&item->hash, head);            /* add to hashtable */
    list_add(&item->lru, cache_lru_queue);  /* add to LRU queue */

    cache_status.used += nsize;

    ncx_shmtx_unlock(cache_lock);

    RETURN_TRUE;
}


PHP_FUNCTION(pcache_get)
{
    char *key = NULL;
    int key_len;
    struct list_head *head, *curr;
    pcache_cache_t *item;
    int index;
    int found = 0;
    int retlen = 0;
    char *retval = NULL;

    if (!cache_enable) {
        RETURN_FALSE;
    }

#if PHP_VERSION_ID >= 70000

    zend_string *pkey, *pval;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "S",
          &pkey) == FAILURE)
    {
        RETURN_FALSE;
    }
    key = ZSTR_VAL(pkey);
    key_len = ZSTR_LEN(pkey);

#else

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s",
          &key, &key_len) == FAILURE)
    {
        RETURN_FALSE;
    }

#endif

    index = pcache_hash(key, key_len) % buckets_size;

    ncx_shmtx_lock(cache_lock);

    head = &cache_buckets[index];

    list_for_each(curr, head) {

        item = list_entry(curr, pcache_cache_t, hash);

        if (item->key_size == key_len &&
            !memcmp(item->data, key, key_len))
        {
            found = 1;
            break;
        }
    }

    if (found) {
        if (item->expire > 0 && item->expire <= (long)time(NULL)) { /* expire */
            cache_status.miss++;
            cache_status.used -=
                (item->key_size + item->val_size + sizeof(pcache_cache_t));

            list_del(&item->lru);
            list_del(&item->hash);
            ncx_slab_free(cache_pool, item);

        } else {
            /* move item to LRU head */
            list_del(&item->lru);
            list_add(&item->lru, cache_lru_queue);

            /* copy value to user space */
            retlen = item->val_size;
            retval = emalloc(retlen + 1);

            if (retval) {
                memcpy(retval, item->data + item->key_size, retlen);
                retval[retlen] = 0;
            }

            cache_status.hits++;
        }

    } else {
        cache_status.miss++;
    }

    ncx_shmtx_unlock(cache_lock);

    if (retval) {
        _RETURN_STRINGL(retval, retlen);
    } else {
        RETURN_FALSE;
    }
}


PHP_FUNCTION(pcache_del)
{
    char *key = NULL;
    int key_len;
    struct list_head *head, *curr, *next;
    pcache_cache_t *item;
    int index;
    int found = 0;

    if (!cache_enable) {
        RETURN_FALSE;
    }

#if PHP_VERSION_ID >= 70000

    zend_string *pkey, *pval;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "S",
          &pkey) == FAILURE)
    {
        RETURN_FALSE;
    }
    key = ZSTR_VAL(pkey);
    key_len = ZSTR_LEN(pkey);

#else

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s",
          &key, &key_len) == FAILURE)
    {
        RETURN_FALSE;
    }

#endif

    index = pcache_hash(key, key_len) % buckets_size;

    ncx_shmtx_lock(cache_lock);

    head = &cache_buckets[index];

    list_for_each_safe(curr, next, head) {

        item = list_entry(curr, pcache_cache_t, hash);

        if (key_len == item->key_size &&
            !memcmp(key, item->data, key_len))
        {
            cache_status.used -=
                (item->key_size + item->val_size + sizeof(pcache_cache_t));

            list_del(&item->hash);
            list_del(&item->lru);
            ncx_slab_free(cache_pool, item);

            found = 1;

            break;
        }
    }

    ncx_shmtx_unlock(cache_lock);

    if (found) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE;
    }
}

/* }}} */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
