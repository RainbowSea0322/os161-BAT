#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <types.h>
#include <synch.h>
#include <limits.h>
#include <vnode.h>

struct open_file { 
    struct vnode *vn;
    int flag;
    int refcount;
    off_t offset;
    struct lock *file_lock;
} open_file;

struct file_table {
    struct open_file *table[OPEN_MAX];
	struct lock *file_table_lock;
} file_table;

// function for open_file
struct open_file * of_create (struct vnode *vn, int flag);
void of_destroy(struct open_file *of);
void of_incref(struct open_file *of);
void of_decref(struct open_file *of);

//function for file table
struct file_table * ft_create(void);
void ft_destroy(struct file_table *ft);
int ft_init (struct file_table *ft);
#endif