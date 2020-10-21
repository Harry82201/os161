#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <current.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <filetable.h>
#include <proc.h>
#include <kern/errno.h>
#include <kern/limits.h>
#include <limits.h>
#include <vnode.h>
#include <kern/errno.h>
#include <uio.h>


int sys_open(const char *filename, int flags, int *retval){
    
    char new_path[PATH_MAX];
    size_t got;
    int err_copyinstr;
    int err_vfsopen;
    struct vnode *new_file;
    bool entry_created = false;

    // check if file is valid
    if(filename == NULL){
        return EINVAL;
    }

    // check if flag is valid
    int masked_flags = flags & O_ACCMODE; // mask flags
    if(masked_flags != O_RDONLY && masked_flags != O_WRONLY && masked_flags != O_RDWR){
        return EINVAL;
    }

    // scan and find a valid slot to create entry 
    int fd;
    for(int i = 0; i < OPEN_MAX; i++){
        if(curproc->p_filetable->ft_entries[i] == NULL){
            curproc->p_filetable->ft_entries[i] = entry_create(new_file);
            fd = i;
            entry_created = true;
            break;
        }
    }
    // no valid slot (too many files opened)
    if(entry_created == false){
        return EINVAL;
    }
    
    // check copyinstr
    err_copyinstr = copyinstr((const_userptr_t)filename, new_path, PATH_MAX, &got);
    if(err_copyinstr){
        return err_copyinstr;
    }

    // try to open file
    err_vfsopen = vfs_open(new_path, flags, 0, &new_file);
    if(err_vfsopen){
        return err_vfsopen;
    }
    
    lock_acquire(curproc->p_filetable->ft_entries[fd]->entry_lock);
    curproc->p_filetable->ft_entries[fd]->file = new_file;
    curproc->p_filetable->ft_entries[fd]->rwflags = flags;
    
    *retval = fd;
    lock_release(curproc->p_filetable->ft_entries[fd]->entry_lock);
    
    return 0;

}

int sys_read(int fd, void *buf, size_t buflen, int *retval){
    struct uio uio;
    struct iovec iovec;
    int result;
    struct file_table *ft = curproc->p_filetable;
    int masked_flags = ft->ft_entries[fd]->rwflags & O_ACCMODE;
    
    lock_acquire(ft->ft_lock);
    // check if flag is valid
    if(masked_flags != O_RDONLY && masked_flags != O_RDWR){
        lock_release(ft->ft_lock);
        return EINVAL;
    }
    // check if ft and fd are valid
    if(ft == NULL || fd < 0 || fd > OPEN_MAX - 1 || ft->ft_entries[fd] == NULL){
        lock_release(ft->ft_lock);
        return EINVAL;
    }
    
    lock_release(ft->ft_lock);

    lock_acquire(ft->ft_entries[fd]->entry_lock);
    uio.uio_segflg = UIO_USERSPACE;
    uio.uio_space = curproc->p_addrspace;
    uio_kinit(&iovec, &uio, buf, buflen, ft->ft_entries[fd]->offset, UIO_READ);

    result = VOP_READ(ft->ft_entries[fd]->file, &uio);
    if(result){
        lock_release(ft->ft_entries[fd]->entry_lock);
        return result;
    }

    off_t len = (off_t)buflen - uio.uio_resid;
    ft->ft_entries[fd]->offset = ft->ft_entries[fd]->offset + len;
    *retval = len;
    lock_release(ft->ft_entries[fd]->entry_lock);
    
    return 0;

}

int sys_write(int fd, void *buf, size_t nbytes, int *retval){
    struct uio uio;
    struct iovec iovec;
    int result;
    struct file_table *ft = curproc->p_filetable;
    int masked_flags = ft->ft_entries[fd]->rwflags & O_ACCMODE;
    
    lock_acquire(ft->ft_lock);
    // check if flag is valid
    if(masked_flags != O_WRONLY && masked_flags != O_RDWR){
        lock_release(ft->ft_lock);
        return EINVAL;
    }
    // check if ft and fd are valid
    if(ft == NULL || fd < 0 || fd > OPEN_MAX - 1 || ft->ft_entries[fd] == NULL){
        lock_release(ft->ft_lock);
        return EINVAL;
    }
    
    lock_release(ft->ft_lock);
    
    lock_acquire(ft->ft_entries[fd]->entry_lock);
    uio.uio_segflg = UIO_USERSPACE;
    uio.uio_space = curproc->p_addrspace;
    uio_kinit(&iovec, &uio, buf, nbytes, ft->ft_entries[fd]->offset, UIO_WRITE);
    
    result = VOP_WRITE(ft->ft_entries[fd]->file, &uio);
    if(result){
        lock_release(ft->ft_entries[fd]->entry_lock);
        return result;
    }

    off_t len = (off_t)nbytes - uio.uio_resid;
    ft->ft_entries[fd]->offset = ft->ft_entries[fd]->offset + len;
    *retval = len;
    lock_release(ft->ft_entries[fd]->entry_lock);
    
    return 0;

}


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