#ifndef _FILE_TABLE_H_
#define _FILE_TABLE_H_

#include <types.h>
#include <synch.h>
#include <limits.h>
#include <vnode.h>
#include <lib.h>

struct file_entry {
    int ref_count;
    struct lock *entry_lock;
    struct vnode *file;
    off_t offset;
    const char* path;
    int rwflags;
};

struct file_table {
    struct lock *ft_lock;
    struct file_entry *ft_entries[OPEN_MAX];
};

// File Table functions
struct file_table *ft_create(void);
void ft_destroy(struct file_table *ft);

// File Entry functions
struct file_entry *entry_create(struct vnode *vnode);
void entry_destroy(struct file_entry *file_entry);
void entry_incref(struct file_entry *file_entry);
void entry_decref(struct file_entry *file_entry);

// Std init function
int ft_init_std(struct file_table *ft);


#endif