/* -*- Mode: C; tab-width: 4 -*- */
/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2009 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | author: Liexusong <liexusong@qq.com>                                 |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ext/standard/info.h"
#include <zend_exceptions.h>

#include "php.h"
#include "php_ini.h"
#include "pcache.h"


ps_module ps_mod_pcache = {
    PS_MOD(pcache)
};


extern int pcache_open;


PS_OPEN_FUNC(redis)
{
    if (pcache_open) {
        return SUCCESS;
    }
    return FAILURE;
}


PS_CLOSE_FUNC(redis)
{
	return SUCCESS;
}


PS_READ_FUNC(redis)
{
    char *retval;
    int   retlen;
    int   result;

    result = pcache_getval(key, strlen(key), 
                           &retval, &retlen);
    if (result == 0) {
        *val = retval;
        *vallen = retlen;
        return SUCCESS;
    }

    return FAILURE;
}


PS_WRITE_FUNC(redis)
{
    int result;
    int maxlifetime = INI_INT("session.gc_maxlifetime");

    result = pcache_setval(key, strlen(key),
                           val, vallen, (long)maxlifetime);
    if (result == 0) {
        return SUCCESS;
    }

    return FAILURE;
}


PS_DESTROY_FUNC(redis)
{
    int result;

    result = pcache_delval(key, strlen(key));
    if (result == 0) {
        return SUCCESS;
    }

    return FAILURE;
}


PS_GC_FUNC(redis)
{
    return SUCCESS;
}

