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

int sys_dup2(int oldfd, int newfd, int *retval)
{
    (void) oldfd;
    (void) newfd;
    (void) retval;
    return 0;
}

int sys_chdir(const char* pathname)
{
    (void) pathname;
    return 0;
}

int sys___getcwd(char* buf, size_t buflen, int *retval)
{
    (void) buf;
    (void) buflen;
    (void) retval;
    return 0;
}