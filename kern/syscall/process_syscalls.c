#include <types.h>
#include <current.h>
#include <limits.h>
#include <syscall.h>

int sys_getpid(int *retval)
{
    (void) retval;

    return 0;
}

int sys_fork(struct trapframe *tf, int *retval)
{
    (void) tf;
    (void) retval;

    return 0;
}

int sys_execv(const char *program, char **args)
{
    (void) program;
    (void) args;

    return 0;
}

int sys_waitpid(pid_t pid, int *status, int options)
{
    (void) pid;
    (void) status;
    (void) options;

    return 0;
}

void sys__exit(int waitcode)
{
    (void) waitcode;
}