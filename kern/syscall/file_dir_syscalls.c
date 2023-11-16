#include <types.h>
#include <kern/errno.h>
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <synch.h>
#include <file_dir_syscalls.h>
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
#include <endian.h>


int open(const char *filename, int flags, mode_t mode, int *retval){
    int result;
    char *kernel_dest;
    size_t *actual_len;
    struct vnode *vn;
    struct open_file *of;

    kernel_dest = kmalloc(PATH_MAX);
    if (kernel_dest == NULL) {
        *retval = -1;
        return ENOSPC;
    }

    actual_len = kmalloc(sizeof(size_t));
    if (actual_len == NULL) {
        kfree(kernel_dest);
        *retval = -1;
        return ENOSPC;
    }
    // const_userptr_t usersrc, char *dest, size_t len, size_t *actual
    result = copyinstr((const_userptr_t)filename, kernel_dest, PATH_MAX, actual_len);
    if (result) {//fail to call the copyinstr
        kfree(kernel_dest);
        kfree(actual_len);
        *retval = -1;
        return result;
    }
    // char *path, int openflags, mode_t mode, struct vnode **ret
    result = vfs_open(kernel_dest, flags, mode, &vn);
    if (result) {//fail to call the vfs_open
        kfree(kernel_dest);
        kfree(actual_len);
        *retval = -1;
        return result;
    }

    of = of_create(vn, flags);
    if (of == NULL) {// fail to create open file
        kfree(kernel_dest);
        kfree(actual_len);
        vfs_close(vn);
        *retval = -1;
        return ENOSPC;
    }
    // find a place in the table to save the of
    lock_acquire(curproc->ft->file_table_lock);
    for (int i = 0; i < OPEN_MAX; i++) {
        if (curproc->ft->table[i] == NULL) { // success and return the fd
            curproc->ft->table[i] = of;
            *retval = i;
            lock_release(curproc->ft->file_table_lock);
            kfree(kernel_dest);
            kfree(actual_len);
            return 0;
        }
    }

    //no place for this open file
    lock_release(curproc->ft->file_table_lock);
    of_destroy(of);
    kfree(kernel_dest);
    kfree(actual_len);
    return EMFILE;
}

ssize_t read(int fd, void *buf, size_t buflen, int *retval){
    struct open_file *of;
    int result; 
    struct iovec *iov;
    struct uio *uio;
    size_t *actual_len;

    // check fd valid
    if(fd < 0 || fd >= OPEN_MAX){
        *retval = -1;
        return EBADF;
    }

    lock_acquire(curproc->ft->file_table_lock);
    // check open file exist
    if (curproc->ft->table[fd] == NULL) {
        lock_release(curproc->ft->file_table_lock);
        *retval = -1;
        return EBADF;
    }

    of = curproc->ft->table[fd];
    // check open file permission
    if ((of->flag)%4 == O_WRONLY) {  // we only care the last 2 bit binary, filter out other flags
        lock_release(curproc->ft->file_table_lock);
        *retval = -1;
         return EBADF;
    }

    // all checking pass, get the file lock and others can access the table now
    lock_acquire(of->file_lock);
    lock_release(curproc->ft->file_table_lock);

    actual_len = kmalloc(sizeof(size_t));
    if (actual_len == NULL) {
        lock_release(of->file_lock);
        *retval = -1;
        return ENOSPC;
    }

    iov = kmalloc(sizeof(struct iovec));
    if (iov == NULL) {
        lock_release(of->file_lock);;
        kfree(actual_len);
        *retval = -1;
        return ENOSPC;
    }

    uio = kmalloc(sizeof(struct uio));
    if (uio == NULL) {
        lock_release(of->file_lock);
        kfree(actual_len);
        kfree(iov);
        *retval = -1;
        return ENOSPC;
    }
    // struct iovec *, struct uio *, userptr_t *ubuf, size_t len, off_t pos, enum uio_rw rw
    uio_user_init(iov, uio, (userptr_t) buf, buflen, of->offset, UIO_READ);

    result = VOP_READ(of->vn, uio);
    if (result) { // false to VOP_READ
        lock_release(of->file_lock);
        kfree(actual_len);
        kfree(iov);
        kfree(uio);
        *retval = -1;
        return result;
    }

    // calculate size of actual reading size
    *retval = (uio->uio_offset) - (of->offset);
    // synchronize open file offset
    of->offset = uio->uio_offset;
    lock_release(of->file_lock);
    kfree(actual_len);
    kfree(iov);
    kfree(uio);
    return 0;
}

ssize_t write(int fd, const void *buf, size_t nbytes, int *retval){
    struct open_file *of;
    int result; 
    struct iovec *iov;
    struct uio *uio;
    size_t *actual_len;
    // check fd valid
    if(fd < 0 || fd >= OPEN_MAX){
        *retval = -1;
        return EBADF;
    }

    lock_acquire(curproc->ft->file_table_lock);
    // check open file exist
    if (curproc->ft->table[fd] == NULL) {
        lock_release(curproc->ft->file_table_lock);
        *retval = -1;
        return EBADF;
    }
    of = curproc->ft->table[fd];
    // check open file permission
    if ((of->flag)%4 == O_RDONLY) {
        lock_release(curproc->ft->file_table_lock);
        *retval = -1;
        return EBADF;
    }
    // all checks pass
    lock_acquire(of->file_lock);
    lock_release(curproc->ft->file_table_lock);
    
    actual_len = kmalloc(sizeof(size_t));
    if (actual_len == NULL) {
        lock_release(of->file_lock);
        *retval = -1;
        return ENOSPC;
    }

    iov = kmalloc(sizeof(struct iovec));
    if (iov == NULL) {
        lock_release(of->file_lock);
        kfree(actual_len);
        *retval = -1;
        return ENOSPC;
    }

    uio = kmalloc(sizeof(struct uio));
    if (uio == NULL) {
        lock_release(of->file_lock);
        kfree(actual_len);
        kfree(iov);
        *retval = -1;
        return ENOSPC;
    }
    // struct iovec *, struct uio *, userptr_t *ubuf, size_t len, off_t pos, enum uio_rw rw
    uio_user_init(iov, uio, (userptr_t)buf, nbytes, of->offset, UIO_WRITE);

    result = VOP_WRITE(of->vn, uio);
    if (result) {//fail to VOP_WRITE
        lock_release(of->file_lock);
        kfree(actual_len);
        kfree(iov);
        kfree(uio);
        *retval = -1;
        return result;
    }

    *retval = (uio->uio_offset) - (of->offset);
    of->offset = uio->uio_offset;
    lock_release(of->file_lock);
    kfree(actual_len);
    kfree(iov);
    kfree(uio);
    return 0;
}

int close(int fd, int *retval){
    if(fd < 0 || fd >= OPEN_MAX){
        *retval = -1;
        return EBADF;
    }

    lock_acquire(curproc->ft->file_table_lock);

    if (curproc->ft->table[fd] == NULL) {
        lock_release(curproc->ft->file_table_lock);
        *retval = -1;
        return EBADF;
    }
        
    of_decref(curproc->ft->table[fd]);
    curproc->ft->table[fd] = NULL;
    
    lock_release(curproc->ft->file_table_lock);
    return 0;
}

int lseek(int fd, off_t pos, int whence, off_t* ret_pos){
    struct open_file *of;
    int result;

    if(fd < 0 || fd >= OPEN_MAX){
        *ret_pos = -1;
        return EBADF;
    }

    lock_acquire(curproc->ft->file_table_lock);

    if (curproc->ft->table[fd] == NULL) {
        lock_release(curproc->ft->file_table_lock);
        *ret_pos = -1;
        return EBADF;
    }

    of = curproc->ft->table[fd];
    lock_acquire(of->file_lock);
    
    if (of->vn->vn_fs == 0) { //check vn_fs to check device
        // can't modify console devices
        lock_release(curproc->ft->file_table_lock);
        lock_release(of->file_lock);
        *ret_pos = -1;
        return ESPIPE;
    }

    // device check
    if (of == curproc->ft->table[STDIN_FILENO]||of == curproc->ft->table[STDOUT_FILENO]||of == curproc->ft->table[STDERR_FILENO]) {
        // can't modify console devices
        lock_release(curproc->ft->file_table_lock);
        lock_release(of->file_lock);
        *ret_pos = -1;
        return ESPIPE;
    }

    lock_release(curproc->ft->file_table_lock);
    
    if(whence == SEEK_SET){
        // negativity check
        if (pos < 0) {
            lock_release(of->file_lock);
            *ret_pos = -1;
            return EINVAL;
        }
        of->offset = pos;
    }else if(whence == SEEK_CUR){
        if (of->offset + pos < 0) {
            lock_release(of->file_lock);
            *ret_pos = -1;
            return EINVAL;
        }
        of->offset += pos;
    }else if(whence == SEEK_END){
        struct stat *statbuf = kmalloc(sizeof(struct stat));
        if (statbuf == NULL) {
            lock_release(of->file_lock);
            *ret_pos = -1;
            return ENOSPC;
        }
        result = VOP_STAT(of->vn, statbuf);
        if(result){//fail to VOP_STAT, if success, the statbuf should have vaule fot st_size
            lock_release(of->file_lock);
            kfree(statbuf);
            *ret_pos = -1;
            return result;
        }
        if (statbuf->st_size + pos < 0) {
            lock_release(of->file_lock);
            kfree(statbuf);
            *ret_pos = -1;
            return EINVAL;
        }
        of->offset = statbuf->st_size + pos;
        kfree(statbuf);
    } else{
        // whence is invalid.
        lock_release(of->file_lock);
        return EINVAL;
    }
    *ret_pos = of->offset;
    lock_release(of->file_lock);
    return 0;
}

int chdir(const char *pathname, int *retval){
    char * kernel_path;
    size_t *actual_len;
    int result;

    kernel_path = kmalloc(PATH_MAX);
    if (kernel_path == NULL) {
        *retval = -1;
        return ENOSPC;
    }

    actual_len = kmalloc(sizeof(size_t));
    if (actual_len == NULL) {
        kfree(kernel_path);
        *retval = -1;
        return ENOSPC;
    }
    // const_userptr_t usersrc, char *dest, size_t len, size_t *actual
    result = copyinstr((const_userptr_t)pathname, kernel_path, PATH_MAX, actual_len);
    if (result) {
        kfree(kernel_path);
        kfree(actual_len);
        *retval = -1;
        return result;
    }
    // char *path
    result = vfs_chdir(kernel_path);
    if (result) {
        kfree(kernel_path);
        kfree(actual_len);
        *retval = -1;
        return result;
    }
    kfree(kernel_path);
    kfree(actual_len);
    return 0;
}

int dup2(int oldfd, int newfd, int *retval){
    struct open_file *of_old;
    struct open_file *of_new;
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
        *retval = -1;
        return EBADF;
    }

    // only I can change the table content
    lock_acquire(curproc->ft->file_table_lock);

    if(curproc->ft->table[oldfd] == NULL){
        lock_release(curproc->ft->file_table_lock);
        *retval = -1;
        return EBADF;
    }
    of_old = curproc->ft->table[oldfd];
    lock_acquire(of_old->file_lock);
    // new place is empty, no need to deal with it, simply copy the pointer
    if(curproc->ft->table[newfd] == NULL){
        lock_release(curproc->ft->file_table_lock);
        curproc->ft->table[newfd] = of_old;
        of_old->refcount++;
        lock_release(of_old->file_lock);
        *retval = newfd;
        return 0;
    }
    // already duplicate of each other, do nothing
    if (curproc->ft->table[oldfd] == curproc->ft->table[newfd]){
        lock_release(curproc->ft->file_table_lock);
        lock_release(of_old->file_lock);
        *retval = newfd;
        return 0;
    }
    // hard case, need to close new places previous file before dup
    of_new = curproc->ft->table[newfd];
    lock_acquire(of_new->file_lock);
    lock_release(curproc->ft->file_table_lock);
    // manually close the file without calling of_close, of_close() need the lock and will cause deadlock
    of_new->refcount--;
    if(of_new->refcount == 0){
        lock_release(of_new->file_lock);
        // no one linking to this file, so its safe to be destroy after we released the lock
        of_destroy(of_new);
    }
    curproc->ft->table[newfd] = of_old;
    of_old -> refcount++;
    lock_release(of_old->file_lock);
    *retval = newfd;
    return 0;
}

int __getcwd(char *buf, size_t buflen, int *retval){
    struct uio *uio;
    struct iovec *iov;
    off_t pos = 0;
    size_t *actual_len;
    int result;

    iov = kmalloc(sizeof(struct iovec));
    if (iov == NULL) {
        *retval = -1;
        return ENOSPC;
    }

    uio = kmalloc(sizeof(struct uio));
    if (uio == NULL) {
        kfree(iov);
        *retval = -1;
        return ENOSPC;
    }

    // struct iovec *, struct uio *, userptr_t *ubuf, size_t len, off_t pos, enum uio_rw rw
    uio_user_init(iov, uio, (userptr_t)buf, buflen, pos, UIO_READ);

    // struct uio *uio
    result = vfs_getcwd(uio);
    if (result) {
        kfree(iov);
        kfree(uio);
        *retval = -1;
        return result;
    }

    actual_len = kmalloc(sizeof(size_t));
    if (actual_len == NULL) {
        kfree(iov);
        kfree(uio);
        *retval = -1;
        return ENOSPC;
    }
    
    *retval = *actual_len;
    kfree(iov);
    kfree(uio);
    kfree(actual_len);
    return 0;
}
