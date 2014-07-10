#ifndef _NCX_LOCK_H_
#define _NCX_LOCK_H_

typedef volatile unsigned int ncx_atomic_t;

void ncx_shmtx_lock(ncx_atomic_t *lock);
void ncx_shmtx_unlock(ncx_atomic_t *lock);

#endif
