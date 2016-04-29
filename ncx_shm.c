#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "ncx_shm.h"


#ifdef MAP_ANON

int
ncx_shm_alloc(ncx_shm_t *shm)
{
    shm->addr = (void *) mmap(NULL, shm->size,
                              PROT_READ | PROT_WRITE,
                              MAP_ANON | MAP_SHARED,
                              -1, 0);

    if (shm->addr == NULL) {
        return -1;
    }

    return 0;
}


void
ncx_shm_free(ncx_shm_t *shm)
{
    if (shm->addr) {
        munmap((void *) shm->addr, shm->size);
    }
}

#else

int
ncx_shm_alloc(ncx_shm_t *shm)
{
    ngx_fd_t  fd;

    fd = open("/dev/zero", O_RDWR);
    if (fd == -1) {
        return -1;
    }

    shm->addr = (void *) mmap(NULL, shm->size,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, 0);

    close(fd);

    if (shm->addr == NULL) {
        return -1;
    }

    return 0;
}


void
ncx_shm_free(ncx_shm_t *shm)
{
    if (shm->addr) {
        munmap((void *) shm->addr, shm->size);
    }
}

#endif
