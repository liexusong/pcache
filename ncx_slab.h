#ifndef _NCX_SLAB_H_INCLUDED_
#define _NCX_SLAB_H_INCLUDED_


#include "ncx_core.h"
#include "ncx_lock.h"

typedef struct ncx_slab_page_s  ncx_slab_page_t;

struct ncx_slab_page_s {
    uintptr_t         slab;
    ncx_slab_page_t  *next;
    uintptr_t         prev;
};


typedef struct {
    size_t            min_size;
    size_t            min_shift;

    ncx_slab_page_t  *pages;
    ncx_slab_page_t   free;

    u_char           *start;
    u_char           *end;

    ncx_atomic_t      mutex;

    void             *addr;
} ncx_slab_pool_t;


void ncx_slab_init(ncx_slab_pool_t *pool);
void *ncx_slab_alloc(ncx_slab_pool_t *pool, size_t size);
void *ncx_slab_alloc_locked(ncx_slab_pool_t *pool, size_t size);
void ncx_slab_free(ncx_slab_pool_t *pool, void *p);
void ncx_slab_free_locked(ncx_slab_pool_t *pool, void *p);


#endif /* _NCX_SLAB_H_INCLUDED_ */
