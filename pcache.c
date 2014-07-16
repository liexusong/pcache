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
#include "fastlz.h"

#include <time.h>


#define PCACHE_KEY_MAX       256
#define PCACHE_VAL_MAX       65535
#define PCACHE_BUCKETS_SIZE  1000
#define PCACHE_MIN_COMPRESS  16


typedef struct pcache_item  pcache_item_t;

struct pcache_item {
    pcache_item_t  *next;
    long            expire;
    u_char          key_size;
    u_short         val_size;
    u_short         org_size;
    char            data[0];
};

/* True global resources - no need for thread safety here */
static ncx_shm_t cache_shm;
static ncx_slab_pool_t *cache_pool;
static pcache_item_t **cache_buckets;
static ncx_atomic_t *cache_lock;

// configure entries
static ncx_uint_t cache_size = 1048576; /* 1MB */
static ncx_uint_t buckets_size = PCACHE_BUCKETS_SIZE;
static int cache_enable = 1;
static int compress_enable = 1;
static int compress_min = PCACHE_MIN_COMPRESS;

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
    "0.2", /* Replace with version number for your extension */
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
    if (new_value_length == 0) {
        return FAILURE;
    }

    if (!strcasecmp(new_value, "on") || !strcmp(new_value, "1")) {
        cache_enable = 1;
    } else {
        cache_enable = 0;
    }

    return SUCCESS;
}


ZEND_INI_MH(pcache_set_cache_size) 
{
    int len;

    if (new_value_length == 0) { 
        return FAILURE;
    }

    pcache_atoi(new_value, &cache_size, &len);

    if (len > 0 && len < new_value_length) { /* have unit */
        switch (new_value[len]) {
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

    } else if (len == 0) { /*failed */
        return FAILURE;
    }

    return SUCCESS;
}


ZEND_INI_MH(pcache_set_buckets_size)
{
    if (new_value_length == 0) {
        return FAILURE;
    }

    buckets_size = atoi(new_value);
    if (buckets_size < PCACHE_BUCKETS_SIZE) {
        buckets_size = PCACHE_BUCKETS_SIZE;
    }

    return SUCCESS;
}


ZEND_INI_MH(pcache_set_compress_enable)
{
    if (new_value_length == 0) {
        return FAILURE;
    }

    if (!strcasecmp(new_value, "on") || !strcmp(new_value, "1")) {
        compress_enable = 1;
    } else {
        compress_enable = 0;
    }

    return SUCCESS;
}


ZEND_INI_MH(pcache_set_compress_min)
{
    if (new_value_length == 0) {
        return FAILURE;
    }

    compress_min = atoi(new_value);
    if (compress_min < PCACHE_MIN_COMPRESS) {
        compress_min = PCACHE_MIN_COMPRESS;
    }

    return SUCCESS;
}


PHP_INI_BEGIN()
    PHP_INI_ENTRY("pcache.cache_size", "1048576", PHP_INI_SYSTEM,
          pcache_set_cache_size)
    PHP_INI_ENTRY("pcache.buckets_size", "1000", PHP_INI_SYSTEM,
          pcache_set_buckets_size)
    PHP_INI_ENTRY("pcache.enable", "1", PHP_INI_SYSTEM,
          pcache_set_enable)
    PHP_INI_ENTRY("pcache.compress_enable", "1", PHP_INI_SYSTEM,
          pcache_set_compress_enable)
    PHP_INI_ENTRY("pcache.compress_min", "16", PHP_INI_SYSTEM,
          pcache_set_compress_min)
PHP_INI_END()


/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(pcache)
{
    void *space;

    REGISTER_INI_ENTRIES();

    if (!cache_enable) {
        return SUCCESS;
    }

    cache_shm.size = cache_size;
    
    if (ncx_shm_alloc(&cache_shm) == -1) { // alloc share memory
        return FAILURE;
    }

    space = (void *) cache_shm.addr;

    cache_pool = (ncx_slab_pool_t *) space;

    cache_pool->addr = space;
    cache_pool->min_shift = 3;
    cache_pool->end = space + cache_size;

    ncx_slab_init(cache_pool); // init slab

    cache_lock = ncx_slab_alloc_locked(cache_pool, sizeof(ncx_atomic_t));
    if (!cache_lock) {
        ncx_shm_free(&cache_shm);
        return FAILURE;
    }

    ncx_memzero(cache_lock, sizeof(ncx_atomic_t)); /* init zero */

    cache_buckets = ncx_slab_alloc_locked(cache_pool,
                       sizeof(pcache_item_t *) * buckets_size);
    if (!cache_buckets) {
        ncx_shm_free(&cache_shm);
        return FAILURE;
    }

    ncx_memzero(cache_buckets, sizeof(pcache_item_t *) * buckets_size);

    pcache_ncpu = sysconf(_SC_NPROCESSORS_ONLN); /* get cpus */
    if (pcache_ncpu <= 0) {
        pcache_ncpu = 1;
    }

    return SUCCESS;
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


/* {{{ proto string pcache_set(string key, string val)
   Return a boolean */
PHP_FUNCTION(pcache_set)
{
    char *key = NULL, *val = NULL;
    int key_len, val_len, org_len;
    long expire = 0;
    pcache_item_t *item, *prev,
                  *next, *temp;
    int index;
    int nsize;
    char out_data[PCACHE_VAL_MAX * 2];
    int comp_len = 0, compress = 0;

    if (!cache_enable) {
        RETURN_FALSE;
    }

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|l",
          &key, &key_len, &val, &val_len, &expire) == FAILURE)
    {
        RETURN_FALSE;
    }

    // key length and value length are valid?

    if (key_len > PCACHE_KEY_MAX || val_len > PCACHE_VAL_MAX) {
        RETURN_FALSE;
    }

    if (expire > 0) {
        expire += (long)time(NULL); // update expire time
    }

    org_len = val_len; // save the original size

    // try to compress the value

    if (compress_enable && val_len >= compress_min) {
        comp_len = fastlz_compress((void *)val, val_len, (void *)out_data);
        // after compress and the value small then original
        if (comp_len != 0 && comp_len < val_len) {
            val = out_data;
            val_len = comp_len;
        }
    }

    nsize = sizeof(pcache_item_t) + key_len + val_len;

    item = ncx_slab_alloc(cache_pool, nsize);
    if (!item) {
        RETURN_FALSE;
    }

    // init item fields

    item->next = NULL;
    item->expire = expire;
    item->key_size = key_len;
    item->val_size = val_len;
    item->org_size = org_len;

    memcpy(item->data, key, key_len);
    memcpy(item->data + key_len, val, val_len);

    index = pcache_hash(key, key_len) % buckets_size; // bucket index

    // insert into hashtable

    ncx_shmtx_lock(cache_lock);

    prev = NULL;
    next = cache_buckets[index];

    while (next) {
        // key exists
        if (item->key_size == next->key_size &&
            !memcmp(item->data, next->data, item->key_size))
        {
            temp = next;

            // skip this
            if (prev) {
                prev->next = next->next;
            } else {
                cache_buckets[index] = next->next;
            }

            next = next->next;
            ncx_slab_free(cache_pool, temp);

            continue;
        }

        prev = next;
        next = next->next;
    }

    if (prev) {
        prev->next = item;
    } else {
        cache_buckets[index] = item;
    }

    ncx_shmtx_unlock(cache_lock);

    RETURN_TRUE;
}


PHP_FUNCTION(pcache_get)
{
    char *key = NULL;
    int key_len;
    pcache_item_t *item, *prev;
    int index;
    int retlen = 0;
    char *retval = NULL;

    if (!cache_enable) {
        RETURN_FALSE;
    }

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s",
          &key, &key_len) == FAILURE)
    {
        RETURN_FALSE;
    }

    index = pcache_hash(key, key_len) % buckets_size; // bucket index

    ncx_shmtx_lock(cache_lock);

    prev = NULL;
    item = cache_buckets[index];

    while (item) {
        if (item->key_size == key_len &&
            !memcmp(item->data, key, key_len))
        {
            break;
        }

        prev = item;
        item = item->next;
    }

    if (item) {
        // item was expired
        if (item->expire > 0 && item->expire <= (long)time(NULL)) {
            if (prev) {
                prev->next = item->next;
            } else {
                cache_buckets[index] = item->next;
            }

            ncx_slab_free(cache_pool, item);

        } else { // copy value to user space
            retlen = item->org_size;
            retval = emalloc(retlen + 1);
    
            if (retval) {
                if (item->val_size < item->org_size) { // decompress
                    fastlz_decompress((void *)(item->data + item->key_size),
                                    item->val_size, (void *)retval, retlen);

                } else {
                    memcpy(retval, item->data + item->key_size, retlen);
                }
    
                retval[retlen] = '\0';
            }
        }
    }

    ncx_shmtx_unlock(cache_lock);

    if (retval) {
        RETURN_STRINGL(retval, retlen, 0);
    } else {
        RETURN_FALSE;
    }
}


PHP_FUNCTION(pcache_del)
{
    char *key = NULL;
    int key_len;
    pcache_item_t *prev, *next;
    int index;
    int found = 0;

    if (!cache_enable) {
        RETURN_FALSE;
    }

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s",
          &key, &key_len) == FAILURE)
    {
        RETURN_FALSE;
    }

    index = pcache_hash(key, key_len) % buckets_size; // bucket index

    ncx_shmtx_lock(cache_lock);

    prev = NULL;
    next = cache_buckets[index];

    while (next) {
        if (key_len == next->key_size &&
            !memcmp(key, next->data, key_len))
        {
            if (prev) {
                prev->next = next->next;
            } else {
                cache_buckets[index] = next->next;
            }

            ncx_slab_free(cache_pool, next);

            found = 1;

            break;
        }

        prev = next;
        next = next->next;
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
