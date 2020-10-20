#include <types.h>
#include <filetable.h>
#include <proc.h>
#include <current.h>
#include <limits.h>
#include <syscall.h>
#include <kern/errno.h>
#include <uio.h>
#include <vfs.h>
#include <copyinout.h>

int sys_close(int fd) 
{
    struct file_table *ft = curproc->p_filetable;

    KASSERT(ft != NULL);

    lock_acquire(ft->ft_lock);
    
    if (fd < 0 || fd > OPEN_MAX || ft->ft_entries[fd] != NULL) {
        lock_release(ft->ft_lock);
        return EBADF;
    }
    // close entry if being used
    struct file_entry *entry = ft->ft_entries[fd];
    lock_acquire(entry->entry_lock);
    vfs_close(entry->file);
    entry_decref(entry);
    ft->ft_entries[fd] = NULL;
    lock_release(entry->entry_lock);
    
    lock_release(ft->ft_lock);
    return 0;
}

int sys_dup2(int oldfd, int newfd, int *retval)
{
    struct file_table *ft = curproc->p_filetable;

    KASSERT(ft != NULL);

    lock_acquire(ft->ft_lock);
    if (newfd < 0 || oldfd < 0 || 
        newfd >= OPEN_MAX || oldfd >= OPEN_MAX ||
        ft->ft_entries[oldfd] != NULL) {

        lock_release(ft->ft_lock);
        return EBADF;
    }

    struct file_entry *old_entry = ft->ft_entries[oldfd];

    // close entry if being used
    struct file_entry *new_entry = ft->ft_entries[newfd];
    if (new_entry != NULL) {
        lock_acquire(new_entry->entry_lock);
        entry_decref(new_entry);
        ft->ft_entries[newfd] = NULL;
        lock_release(new_entry->entry_lock);
    }

    // assign new file descriptor to old file_entry and increment ref count
    ft->ft_entries[newfd] = old_entry;
    lock_acquire(old_entry->entry_lock);
    entry_incref(old_entry);
    lock_release(old_entry->entry_lock);
    
    lock_release(ft->ft_lock);
    *retval = newfd;

    return 0;
}

int sys_chdir(const char* pathname)
{
    char *path = kmalloc(PATH_MAX);
    size_t *path_len = kmalloc(sizeof(size_t));

    // copy string from user space to kernel space
    int err = copyinstr((const_userptr_t) pathname, path, PATH_MAX, path_len);
    if (err) {
        kfree(path);
        kfree(path_len);
        return err;
    }

    // use vfs_chdir to change directories
    int result = vfs_chdir(path);
    if (result) {
        return result;
    }

    kfree(path);
    kfree(path_len);
    return 0;
}

int sys___getcwd(char* buf, size_t buflen, int *retval)
{
    struct iovec iov;
    struct uio u;

    // create uio struct so that we can get the working directory 
    // from the virtual file system
    uio_kinit(&iov, &u, (userptr_t) buf, buflen, 0, UIO_READ);
    // buf is from User address space
    u.uio_segflg = UIO_USERSPACE;
    u.uio_space = curproc->p_addrspace;

    int result = vfs_getcwd(&u);
    if (result) {
        return result;
    }

    // retval is the amount of data transfered into buf
    *retval = buflen - u.uio_resid;

    return 0;
}