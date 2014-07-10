#ifndef __NCX_SHM_H
#define __NCX_SHM_H

typedef struct {
    void    *addr;
    size_t   size;
} ncx_shm_t;

int ncx_shm_alloc(ncx_shm_t *shm);
void ncx_shm_free(ncx_shm_t *shm);

#endif
