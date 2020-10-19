#include <types.h>
#include <syscall.h>
#include <kern/errno.h>

int sys_close(int fd) 
{
    if (fd < 0) {
        return EBADF;
    }
    
    return 0;
}

int sys_dup2(int oldfd, int newfd)
{
    (void) oldfd;
    (void) newfd;
    return 0;
}

int sys_chdir(userptr_t pathname)
{
    (void) pathname;
    return 0;
}

int sys___getcwd(userptr_t buf, size_t buflen)
{
    (void) buf;
    (void) buflen;
    return 0;
}