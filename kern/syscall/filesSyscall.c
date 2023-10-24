#include <types.h>
#include <kern/errno.h>
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <synch.h>
#include <filesSyscall.h>
#include <proc.h>
#include <vfs.h>
#include <limits.h>
#include <copyinout.h>
#include <uio.h>
#include <kern/unistd.h>
#include <kern/fcntl.h>
#include <kern/ioctl.h>
#include <kern/reboot.h>
#include <kern/seek.h>
#include <kern/time.h>
#include <file_table.h>
#include <kern/stat.h>
#include <kern/errno.h>


int open(const char *filename, int flags, mode_t mode, int *retval){
    int result;
    char *file_name;
    size_t *path_len;
    struct vnode *vn;
    struct open_file *of;

    file_name = kmalloc(PATH_MAX);
    if(file_name == NULL){
        return ENOSPC;
    }

    path_len = kmalloc(sizeof(size_t));
    if (path_len == NULL) {
        kfree(file_name);
        return ENOSPC;
    }   

    result = copyinstr(filename, file_name, PATH_MAX, path_len);
    if (result) {
        kfree(path_len);
        kfree(file_name);
        return result;
    }

    result = vfs_open(file_name, flags, mode, &vn);
    if (result) {
        kfree(path_len);
        kfree(file_name);
        return result;
    }

    of = open_file_create(vn, flags);
    if (of == NULL) {
        kfree(path_len);
        kfree(file_name);
        vfs_close(vn);
        return ENOSPC;
    }

    lock_acquire(curproc->ft->file_table_lock);
    for (int i = 0; i < OPEN_MAX; i++) {
        if (curproc->ft->table[i] == NULL) {
            curproc->ft->table[i] = of;
            kfree(file_name);
            kfree(path_len);

            lock_release(curproc->ft->file_table_lock);
            *retval = i;
            return 0;
        }
    }
    lock_release(curproc->ft->file_table_lock);

    open_file_destroy(of);
    kfree(file_name);
    kfree(path_len);

    
    return EMFILE;
}

ssize_t read(int fd, void *buf, size_t buflen, int *retval){
    struct open_file *of;
    int result; 
    struct iovec *iov;
    struct uio *uio;

    if(fd < 0 || fd >= OPEN_MAX){
        
        return EBADF;
    }

    lock_acquire(curproc->ft->file_table_lock);

    if (curproc->ft->table[fd] == NULL) {
        lock_release(curproc->ft->file_table_lock);
        
        return EBADF;
    }else{
        of = curproc->ft->table[fd];
        if (of->flag == O_WRONLY) {
            lock_release(curproc->ft->file_table_lock);
            
            return EBADF;
        }

        lock_acquire(of->file_lock);
        lock_release(curproc->ft->file_table_lock);
    }

    iov = kmalloc(sizeof(struct iovec));
    uio = kmalloc(sizeof(struct uio));
    uio_uinit(iov, uio, buf, buflen, of->offset, UIO_READ);
    result = VOP_READ(of->vn, uio);

    if (result) {// false to VOP_READ
        lock_release(of->file_lock);
        return result;
    }

    *retval = (uio->uio_offset) - (of->offset);
    of->offset = uio->uio_offset;
    lock_release(of->file_lock);
    return 0;
}

ssize_t write(int fd, const void *buf, size_t nbytes, int *retval){// 
    struct open_file *of;
    int result; 
    struct iovec *iov;
    struct uio *uio;

    if(fd < 0 || fd >= OPEN_MAX){
        
        return EBADF;
    }

    lock_acquire(curproc->ft->file_table_lock);

    if (curproc->ft->table[fd] == NULL) {
        lock_release(curproc->ft->file_table_lock);
        
        return EBADF;
    }else{
        of = curproc->ft->table[fd];
        if (of->flag == O_RDONLY) {
            lock_release(curproc->ft->file_table_lock);
            
            return EBADF;
        }

        lock_acquire(of->file_lock);
        lock_release(curproc->ft->file_table_lock);
    }

    iov = kmalloc(sizeof(struct iovec));
    uio = kmalloc(sizeof(struct uio));
    uio_uinit(iov, uio, buf, buflen, of->offset, UIO_WRITE);
    result = VOP_WRITE(of->vn, uio);

    if (result) {//fail to VOP_WRITE
        lock_release(of->file_lock);
        return result;
    }

    *retval = (uio->uio_offset) - (of->offset);
    of->offset = uio->uio_offset;
    lock_release(of->file_lock);
    return 0;
}

int close(int fd, int *retval){
    if(fd < 0 || fd >= OPEN_MAX){
        
        return EBADF;
    }

    lock_acquire(curproc->ft->file_table_lock);

    if (curproc->ft->table[fd] == NULL) {
        lock_release(curproc->ft->file_table_lock);
        
        return EBADF;
    }else{
        of_decref(curproc->ft->table[fd]);
        curproc->ft->table[fd] = NULL;
    }

    lock_release(curproc->oft->table_lock);

    return 0;
}

int lseek(int fd, off_t pos, int whence, off_t* ret_pos){
    struct open_file *of;
    int result;

    if(fd < 0 || fd >= OPEN_MAX){
        
        return EBADF;
    }

    lock_acquire(curproc->ft->file_table_lock);


    if (curproc->ft->table[fd] == NULL) {
        lock_release(curproc->ft->file_table_lock);
        
        return EBADF;
    }else{
        of = curproc->ft->table[fd];
        lock_acquire(of->file_lock);
        lock_release(curproc->ft->file_table_lock);
    }

    if(whence == SEEK_SET){
        of -> offset = pos;
    }else if(whence == SEEK_CUR){
        of -> offset += pos;
    }else if(whence == SEEK_END){
        struct stat *statbuf = kmalloc(sizeof(struct stat));
        result = VOP_STAT(of->vn, statbuf);
        if(result){//fail to VOP_STAT, if success, the statbuf should have vaule fot st_size
            kfree(statbuf);
            
            lock_release(of->file_lock);
            return result;
        }else{
            of->offset = statbuf->st_size + pos;
            kfree(statbuf);
        }
    }else{
        lock_release(of->file_lock);
        
        return EINVAL;
    }

    *ret_pos = of->offset;
    lock_release(of->file_lock);

    return 0;
}

int chdir(const char *pathname, int *retval){

}

int dup2(int oldfd, int newfd, int *retval){
    int result;
    struct open_file *of_old;
    struct open_file *of_new;
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
        
        return EBADF;
    }

    lock_acquire(curproc->ft->file_table_lock);

    if(curproc->ft->table[oldfd] == NULL){
        
        lock_release(curproc->ft->file_table_lock);
        return EBADF;
    }
    of_old = curproc->ft->table[oldfd];
    lock_acquire(of_old->file_lock);

    if(curproc->ft->table[newfd] == NULL){
        lock_release(curproc->ft->file_table_lock);
        curproc->ft->table[newfd] = of_old;
        lock_release(of_old->file_lock);
        *retval = newfd;
        return 0;
    }

    if (curproc->ft->table[oldfd] == curproc->ft->table[newfd]){
        lock_release(curproc->ft->file_table_lock);
        lock_acquire(of_old->file_lock);
        *retval = newfd;
        return 0;
    }

    of_new = curproc->ft->table[newfd];
    lock_acquire(of_new->file_lock);
    lock_release(curproc->ft->file_table_lock);

    of_new->refcount--;
    if(of_new->refcount == 0){
        lock_release(of_new->file_lock);
        of_destroy(of_new);
    }
    curproc->ft->table[newfd] = of_old;
    of_old -> refcount++;
    lock_release(of_old->file_lock);
    *retval = newfd;
    return 0;
}

int __getcwd(char *buf, size_t buflen, int *retval){

}
