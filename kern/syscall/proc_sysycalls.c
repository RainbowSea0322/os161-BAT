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
#include <array.h>
#include <proc_syscalls.h>

int fork(struct trapframe *tf, int *retval){
    struct proc *child_proc;

    child_proc = proc_create_runprogram("child_proc");
    
    if (child_proc == NULL) {
        return ENPROC;
    }

    array_add(curproc->children, child_proc, NULL);

    //copy address_space
    result = as_copy(curproc->p_addrspace, &child_proc->p_addrspace);

    if (result) {
        proc_destroy(child_proc);
        return result;
    }

    //copy file_table
    lock_acquire(curproc->ft->file_table_lock);
    lock_acquire(child_proc->ft->file_table_lock);
    for(int fd; fd < OPEN_MAX; fd++){
        if(curproc->ft->table[fd] != NULL){
            child_proc->ft->table[fd] = curproc->ft->table[fd];
            of_incref(curproc->ft->table[fd]);
        }
    }
    lock_release(curproc->ft->file_table_lock);
    lock_release(child_proc->ft->file_table_lock);

    //copy trap frame
    struct trapframe *trap_copy = kmalloc(sizeof(struct trapframe));

    if (trap_copy == NULL) {
        kfree(trap_copy);
        proc_destroy(child_proc);
        return ENOMEM;
    }

    memcpy(trap_copy, tf, sizeof(struct trapframe));


}
int execv(const char *program, char **args){

}
int waitpid(int pid, userptr_t status, int options, int *retval){

}
int _exit(int exitcode){

}
int getpid(int *retval){

}