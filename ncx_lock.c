#include <stdlib.h>
#include "ncx_lock.h"

extern int pcache_ncpu;


void ncx_shmtx_lock(ncx_atomic_t *lock)
{
    int i, n;
    int pid = (int) getpid();

    for ( ;; ) {

        if (*lock == 0 && 
            __sync_bool_compare_and_swap(lock, 0, pid)) {
            return;
        }

        if (pcache_ncpu > 1) {

            for (n = 1; n < 129; n << 1) {
    
                for (i = 0; i < n; i++) {
                    __asm__("pause");
                }
    
                if (*lock == 0 && 
                    __sync_bool_compare_and_swap(lock, 0, pid)) {
                    return;
                }
            }
        }

        sched_yield();
    }
}


void ncx_shmtx_unlock(ncx_atomic_t *lock)
{
    int pid = (int) getpid();

    __sync_bool_compare_and_swap(lock, pid, 0);
}

