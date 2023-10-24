#include <types.h>
#include <vfs.h>
#include <synch.h>
#include <vnode.h>
#include <kern/unistd.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <kern/errno.h>
#include <file_table.h>

// create a open_file with the state flag
struct open_file * of_create (struct vnode *vn, int flag){
    struct open_file *of;
    of = kmalloc(sizeof(struct open_file));
    if (of == NULL) {
        return NULL;
    }

    of->file_lock = lock_create("file_lock");
    if (of->flock == NULL) {
        kfree(of);
        return NULL;
    }

    of->vn = vn;
    of->flag = flag;
    of->offset = 0;
    of->refcount = 1;

    return of;
}
// destroy the open_file
void of_destroy(struct open_file *of)
{
    vfs_close(of->vn);

    lock_destroy(of->flock);
    kfree(of);
}
// same structure as vnode_incref
void of_incref(struct open_file *of){
    // replace kassert with NULL check to deal with it softer
    if (of = NULL) {
        return;
    }
    lock_acquire(of->file_lock);
    of->refcount++;
    lock_release(of->file_lock);
}
// same structure as vnode_decref
void of_decref(struct open_file *of){
    // need this bool value to execute destroy after lock_release, cause we will destroy the lock as well.
    bool destroy;
    
    // replace kassert with NULL check to deal with it softer
    if (of = NULL) {
        return;
    }

    lock_acquire(of->file_lock);

    if (of->refcount > 1) {
        of->refcount--;
        destroy = false;
    } else {
        destroy = true;
    }
    lock_release(of->file_lock);
    if (destroy) {
        of_destroy(of);
    }
}

// file table function
struct file_table * ft_create (void){
    struct open_file_table *ft;
    ft = kmalloc(sizeof(struct file_table));
    if (ft == NULL) {
        return NULL;
    }
    ft->file_table_lock = lock_create("file_table_lock");
    if (ft->file_table_lock == NULL) {
        kfree(ft);
        return NULL;
    }
    for (int i = 0; i < OPEN_MAX; i++) {
        ft->table[i] = NULL;
    }

    return ft;
}

void ft_destroy(struct file_table *ft){
    if (ft == NULL) {
        return;
    }

    lock_destroy(ft->file_table_lock);

    for (int i = 0; i < OPEN_MAX; i++) {
        if (ft->table[i] != NULL) {
            open_file_decref(ft->table[i]);
            ft->table[i] = NULL;
        }
    }
}
// initialize standard in, standard out, and standard error to file descriptor 0, 1, 2
int ft_init (filetable *ft){
    for (int i = 0; i < 3; i++){
        char con_path[32];
        struct vnode *vn;
        struct open_file *of;
        int result;
        // console device attached to con:
	    strcpy(con_path, "con:");

        if(i == STDIN_FILENO){
            result = vfs_open(con_path, O_RDONLY, 0664, &vn);
            if (result) {
                return result;
            }
            of = of_create(vn, O_RDONLY);  
            if (of == NULL) {
                return -1;
            } 
            ft->table[STDIN_FILENO] = of;

        } else if(i == STDOUT_FILENO){
            result = vfs_open(con_path, O_WRONLY, 0664, &vn);
            if (result) {
                return result;
            }

            of = of_create(vn, O_WRONLY);  
            if (of == NULL) {
                return -1;
            } 

            ft->table[STDOUT_FILENO] = of;
        }else if(i == STDERR_FILENO){

            result = vfs_open(con_path, O_WRONLY, 0664, &vn);
            if (result) {
                return result;
            }

            of = of_create(vn, O_WRONLY);  
            if (of == NULL) {
                return -1;
            } 

            ft->table[STDERR_FILENO] = of;
        }
    }
}