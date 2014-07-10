/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2008 The PHP Group                                |
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

/* If you declare any globals in php_pcache.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(pcache)
*/

#define PCACHE_KEY_MAX       256
#define PCACHE_VAL_MAX       65535
#define PCACHE_BUCKETS_SIZE  1000

typedef struct pcache_item  pcache_item_t;

struct pcache_item {
    pcache_item_t  *next;
    u_char          ksize;
    u_short         vsize;
    char            data[0];
};

/* True global resources - no need for thread safety here */
static ncx_shm_t cache_shm;
static ncx_slab_pool_t *cache_pool;
static void **cache_buckets;
static ncx_atomic_t *cache_lock;
static ncx_uint_t cache_size = 10485760; /* 10MB */

/* {{{ pcache_functions[]
 *
 * Every user visible function must have an entry in pcache_functions[].
 */
const zend_function_entry pcache_functions[] = {
	PHP_FE(confirm_pcache_compiled,	NULL)		/* For testing, remove later. */
	{NULL, NULL, NULL}	/* Must be the last line in pcache_functions[] */
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
	PHP_RINIT(pcache),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(pcache),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(pcache),
#if ZEND_MODULE_API_NO >= 20010901
	"0.1", /* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_PCACHE
ZEND_GET_MODULE(pcache)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("pcache.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_pcache_globals, pcache_globals)
    STD_PHP_INI_ENTRY("pcache.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_pcache_globals, pcache_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_pcache_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_pcache_init_globals(zend_pcache_globals *pcache_globals)
{
	pcache_globals->global_value = 0;
	pcache_globals->global_string = NULL;
}
*/
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(pcache)
{
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/

	void *space;

	cache_shm.size = cache_size;
	
	if (ncx_shm_alloc(&cache_shm) == -1) {
	    return FAILURE;
	}

    space = (void *) cache_shm.addr;

	cache_pool = (ncx_slab_pool_t *) space;

	cache_pool->addr = space;
	cache_pool->min_shift = 3;
	cache_pool->end = space + cache_size;

	ncx_slab_init(cache_pool);

	cache_lock = ncx_slab_alloc_locked(cache_pool, sizeof(ncx_atomic_t));

	ncx_memzero(cache_lock, sizeof(ncx_atomic_t)); /* init zero */

	cache_buckets = ncx_slab_alloc_locked(cache_pool,
	                   sizeof(void *) * PCACHE_BUCKETS_SIZE);

    ncx_memzero(cache_buckets, sizeof(void *) * PCACHE_BUCKETS_SIZE);

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(pcache)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/

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

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */


/* Remove the following function when you have succesfully modified config.m4
   so that your module can be compiled into PHP, it exists only for testing
   purposes. */

/* Every user-visible function in PHP should document itself in the source */
/* {{{ proto string confirm_pcache_compiled(string arg)
   Return a string to confirm that the module is compiled in */
PHP_FUNCTION(pcache_set)
{
	char *arg = NULL;
	int arg_len, len;
	char *strg;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &arg, &arg_len) == FAILURE) {
		return;
	}

	len = spprintf(&strg, 0, "Congratulations! You have successfully modified ext/%.78s/config.m4. Module %.78s is now compiled into PHP.", "pcache", arg);
	RETURN_STRINGL(strg, len, 0);
}

PHP_FUNCTION(pcache_get)
{
    
}

PHP_FUNCTION(pcache_del)
{
    
}
/* }}} */
/* The previous line is meant for vim and emacs, so it can correctly fold and 
   unfold functions in source code. See the corresponding marks just before 
   function definition, where the functions purpose is also documented. Please 
   follow this convention for the convenience of others editing your code.
*/


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
