#include <filetable.h>
#include <limits.h>
#include <vfs.h>
#include <kern/errno.h>
#include <kern/fcntl.h>

/*
 * Create a file table struct with no entries
 */
struct file_table *ft_create() 
{
    struct file_table *ft;

    ft = kmalloc(sizeof(struct file_table));
    if (ft == NULL) {
        return NULL;
    }

    ft->ft_lock = lock_create("fs_lock");
    if (ft->ft_lock == NULL) {
        kfree(ft);
        return NULL;
    }

    // files the file table with NULL entries
    for (int i = 0; i < OPEN_MAX; i++) {
        ft->ft_entries[i] = NULL;
    }

    return ft;
}
/*
 * Clean up memory created with a file table
 */
void ft_destroy(struct file_table *ft) 
{
    KASSERT(ft != NULL);
    bool free = true;
    for (int i = 0; i < OPEN_MAX; i++) {
        
        if (ft->ft_entries[i] != NULL) {
            
            lock_acquire(ft->ft_entries[i]->entry_lock);
            if (ft->ft_entries[i]->ref_count == 1) {
                entry_decref(ft->ft_entries[i]);
            } else {
                free = false;
                entry_decref(ft->ft_entries[i]);
                lock_release(ft->ft_entries[i]->entry_lock);
            } 
        }   
    }
    if (free) {
        lock_destroy(ft->ft_lock);
        kfree(ft);
    }
}

/*
 * Create a new file entry with a given vnode
 */
struct file_entry *entry_create(struct vnode *vnode)
{
    KASSERT(vnode != NULL);

    struct file_entry *entry;

    entry = kmalloc(sizeof(struct file_entry));
    if (entry == NULL) {
        return NULL;
    }

    entry->entry_lock = lock_create("file entry lock");
    if (entry->entry_lock == NULL) {
        kfree(entry);
        return NULL;
    } 

    entry->file = vnode;
    entry->offset = 0;
    entry->ref_count = 0;

    return entry;
}

/*
 * Clean up memory created with a file entry
 */
void entry_destroy(struct file_entry *entry) 
{
    KASSERT(entry != NULL);

    vfs_close(entry->file);
    if (entry->file->vn_refcount > 0) {
        // This is ok because the duplicate will clear the memory
        entry = NULL;
        return;
    }
    lock_destroy(entry->entry_lock);
    kfree(entry);
}

/*
 * increment reference counter for specified file_entry
 */  
void entry_incref(struct file_entry *file_entry)
{
    KASSERT(file_entry != NULL);
    file_entry->ref_count++;

}

/*
 * Decrement reference counter for specified file_entry
 * if reference counter is zero as a result of decrementing
 * it will also de-allocate the file_entry
 */ 
void entry_decref(struct file_entry *file_entry)
{
    KASSERT(file_entry != NULL);
    KASSERT(lock_do_i_hold(file_entry->entry_lock));

    file_entry->ref_count--;
    if (file_entry->ref_count == 0) {
        lock_release(file_entry->entry_lock);
        entry_destroy(file_entry);
        return;
    }
}

/*
 * Initializes file descriptors for standard input, output and error
 * These are all attached to the console device
 */
int ft_init_std(struct file_table *ft)
{
    KASSERT(ft != NULL);

    int err;
    const char *console = "con:";

    struct vnode *std_in;
    // kstrdup is used otherwise cons would be destroyed
    err = vfs_open(kstrdup(console), O_RDONLY, 0, &std_in);
    if (err) {
        return err;
    }

    struct vnode *std_out;
    // kstrdup is used otherwise cons would be destroyed
    err = vfs_open(kstrdup(console), O_WRONLY, 0, &std_out);
    if (err) {
        return err;
    }

    struct vnode *std_err;
    // kstrdup is used otherwise cons would be destroyed
    err = vfs_open(kstrdup(console), O_WRONLY, 0, &std_err);
    if (err) {
        return err;
    }

    // Create each of the file entries in the file table
    ft->ft_entries[0] = entry_create(std_in);
    if (ft->ft_entries[0] == NULL) {
        return ENOMEM;
    }
    ft->ft_entries[1] = entry_create(std_out);
    if (ft->ft_entries[1] == NULL) {
        return ENOMEM;
    }
    ft->ft_entries[2] = entry_create(std_err);
    if (ft->ft_entries[2] == NULL) {
        return ENOMEM;
    }

    ft->ft_entries[0]->rwflags = O_RDONLY;
    ft->ft_entries[1]->rwflags = O_WRONLY;
    ft->ft_entries[2]->rwflags = O_WRONLY;

    // Std entries are incremented because they are open by the console
    entry_incref(ft->ft_entries[0]);
    entry_incref(ft->ft_entries[1]);
    entry_incref(ft->ft_entries[2]);

    return 0;
}

