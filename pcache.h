#ifndef __PCACHE_H
#define __PCACHE_H

int pcache_getval(char *key, int keylen,
    char **retval, int *retlen);
int pcache_setval(char *key, int keylen,
    char *val, int vallen, long maxlifetime);
int pcache_delval(char *key, int keylen);

#endif
