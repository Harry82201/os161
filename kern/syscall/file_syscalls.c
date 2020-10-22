#include <types.h>
#include <filetable.h>
#include <proc.h>
#include <current.h>
#include <kern/limits.h>
#include <limits.h>
#include <copyinout.h>
#include <syscall.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <uio.h>
#include <vnode.h>
#include <stat.h>
#include <kern/seek.h>


int sys_open(const char *filename, int flags, int *retval){
    
    char new_path[PATH_MAX];
    size_t got;
    int err_copyinstr;
    int err_vfsopen;
    struct vnode *new_file;
    bool entry_created = false;

    // check if file is valid
    if(filename == NULL){
        return EFAULT;
    }

    // check if flag is valid
    int masked_flags = flags & O_ACCMODE; // mask flags
    if(masked_flags != O_RDONLY && masked_flags != O_WRONLY && masked_flags != O_RDWR){
        return EINVAL;
    }

    // copy string from user space to kernel space
    err_copyinstr = copyinstr((const_userptr_t)filename, new_path, PATH_MAX, &got);
    if(err_copyinstr){
        return err_copyinstr;
    }

    // try to open file
    err_vfsopen = vfs_open(new_path, flags, 0, &new_file);
    if(err_vfsopen){
        return err_vfsopen;
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
    
    lock_acquire(curproc->p_filetable->ft_entries[fd]->entry_lock);
    // set current entry's filepath and flag
    curproc->p_filetable->ft_entries[fd]->file = new_file;
    curproc->p_filetable->ft_entries[fd]->rwflags = flags;
    
    *retval = fd;
    lock_release(curproc->p_filetable->ft_entries[fd]->entry_lock);
    
    return 0;

}

ssize_t sys_read(int fd, void *buf, size_t buflen, int *retval){
    struct uio uio;
    struct iovec iovec;
    int result;
    struct file_table *ft = curproc->p_filetable;

    KASSERT(ft != NULL);
    
    lock_acquire(ft->ft_lock);
    // check if ft and fd are valid
    if(fd < 0 || fd > OPEN_MAX - 1 || ft->ft_entries[fd] == NULL){
        lock_release(ft->ft_lock);
        return EBADF;
    }

    // check if flag is valid
    int masked_flags = ft->ft_entries[fd]->rwflags & O_ACCMODE;
    if(masked_flags != O_RDONLY && masked_flags != O_RDWR){
        lock_release(ft->ft_lock);
        return EBADF;
    }
    
    // check the buf refers to valid memory
    if (buf == NULL) {
        lock_release(ft->ft_lock);
        return EFAULT;
    }
    
    lock_release(ft->ft_lock);

    lock_acquire(ft->ft_entries[fd]->entry_lock);
    // create uio struct to get the working directory from virtual file system
    uio_kinit(&iovec, &uio, buf, buflen, ft->ft_entries[fd]->offset, UIO_READ);
    uio.uio_segflg = UIO_USERSPACE;
    uio.uio_space = curproc->p_addrspace;

    // use VOP_READ to read file
    result = VOP_READ(ft->ft_entries[fd]->file, &uio);
    if(result){
        lock_release(ft->ft_entries[fd]->entry_lock);
        return result;
    }

    // retval is the amount of data transfered
    off_t len = (off_t)buflen - uio.uio_resid;
    ft->ft_entries[fd]->offset = ft->ft_entries[fd]->offset + len;
    *retval = len;
    lock_release(ft->ft_entries[fd]->entry_lock);
    
    return 0;

}

ssize_t sys_write(int fd, void *buf, size_t nbytes, int *retval){
    struct uio uio;
    struct iovec iovec;
    int result;
    struct file_table *ft = curproc->p_filetable;
    
    KASSERT(ft != NULL);

    lock_acquire(ft->ft_lock);
    // check if ft and fd are valid
    if(fd < 0 || fd > OPEN_MAX - 1 || ft->ft_entries[fd] == NULL){
        lock_release(ft->ft_lock);
        return EBADF;
    }

    // check if flag is valid
    int masked_flags = ft->ft_entries[fd]->rwflags & O_ACCMODE;
    if(masked_flags != O_WRONLY && masked_flags != O_RDWR){
        lock_release(ft->ft_lock);
        return EBADF;
    }

    // check the buf refers to valid memory
    if (buf == NULL) {
        lock_release(ft->ft_lock);
        return EFAULT;
    }
    
    lock_release(ft->ft_lock);
    
    lock_acquire(ft->ft_entries[fd]->entry_lock);
    // create uio struct to get the working directory from virtual file system
    uio_kinit(&iovec, &uio, buf, nbytes, ft->ft_entries[fd]->offset, UIO_WRITE);
    uio.uio_segflg = UIO_USERSPACE;
    uio.uio_space = curproc->p_addrspace;
    
    // use VOP_WRITE to write file
    result = VOP_WRITE(ft->ft_entries[fd]->file, &uio);
    if(result){
        lock_release(ft->ft_entries[fd]->entry_lock);
        return result;
    }

    // retval is the amount of data transfered
    off_t len = (off_t)nbytes - uio.uio_resid;
    ft->ft_entries[fd]->offset = ft->ft_entries[fd]->offset + len;
    *retval = len;
    lock_release(ft->ft_entries[fd]->entry_lock);
    
    return 0;

}

off_t sys_lseek(int fd, off_t pos, int whence, int *retval_low, int *retval_high){
    
    struct file_table *ft = curproc->p_filetable;
    struct stat statbuff;
    
    KASSERT(ft != NULL);

    // check if whence is invalid
    if(whence < 0 || whence > 2){
        return EINVAL;
    }

    lock_acquire(ft->ft_lock);

    // check if ft and fd are valid
    if(fd < 0 || fd > OPEN_MAX - 1){
        lock_release(ft->ft_lock);
        return EBADF;
    }

    struct file_entry *entry = ft->ft_entries[fd];
    lock_acquire(entry->entry_lock);
    off_t seek_pos = entry->offset;

    if(entry == NULL){
        lock_release(entry->entry_lock);
        return EBADF;
    }

    // check if seek is illegal
    if(!VOP_ISSEEKABLE(entry->file)){
        return ESPIPE;
    }

    int err = VOP_STAT(entry->file, &statbuff);

    // set new position based on whence
    if(whence == SEEK_SET){
        seek_pos = pos;
    }else if(whence == SEEK_CUR){
        seek_pos = seek_pos + pos;
    }else if(whence == SEEK_END){
        if(err){
            lock_release(entry->entry_lock);
            return err;
        }
        seek_pos = statbuff.st_size + pos;
    }

    // check if seek position is valid
    if(seek_pos < 0){
        lock_release(entry->entry_lock);
        return EINVAL;
    }

    entry->offset = seek_pos;
    *retval_low = seek_pos >> 32;
    *retval_high = seek_pos & 0xffffffff;
    lock_release(entry->entry_lock);
    lock_release(ft->ft_lock);
    
    return 0;
    
}


int sys_close(int fd) 
{
    struct file_table *ft = curproc->p_filetable;

    KASSERT(ft != NULL);

    lock_acquire(ft->ft_lock);
    
    if (fd < 0 || fd > OPEN_MAX || ft->ft_entries[fd] == NULL) {
        lock_release(ft->ft_lock);
        return EBADF;
    }
    // close entry if being used
    struct file_entry *entry = ft->ft_entries[fd];
    lock_acquire(entry->entry_lock);
    if (entry->ref_count == 1) {
        entry_decref(entry);
    } else {
        entry_decref(entry);
        lock_release(entry->entry_lock);
    }
    ft->ft_entries[fd] = NULL;
    
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
        ft->ft_entries[oldfd] == NULL) {

        lock_release(ft->ft_lock);
        return EBADF;
    }

    struct file_entry *old_entry = ft->ft_entries[oldfd];

    // close entry if being used
    struct file_entry *new_entry = ft->ft_entries[newfd];
    if (new_entry != NULL) {
        lock_acquire(new_entry->entry_lock);
        if (new_entry->ref_count == 1) {
            entry_decref(new_entry);
        } else {
            entry_decref(new_entry);
            lock_release(new_entry->entry_lock);
        }
        
        ft->ft_entries[newfd] = NULL;
    }

    // assign new file descriptor to old file_entry and increment ref count
    ft->ft_entries[newfd] = old_entry;
    lock_acquire(old_entry->entry_lock);
    VOP_INCREF(old_entry->file);
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