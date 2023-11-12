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
#include <pid_table.h>

int fork(struct trapframe *tf, int *retval){
    struct proc *child_proc;
    struct trapframe *trapframe_copy;
    struct thread *kthread;

    //create proc with pid, file table already created here
    child_proc = proc_create_runprogram("child_proc");
    
    if (child_proc == NULL) {
        return ENPROC;
    }

    array_add(curproc->children_proc, child_proc, NULL);

    //copy address_space
    result = as_copy(curproc->p_addrspace, &child_proc->p_addrspace);
    if (result) {
        proc_destroy(child_proc);
        return result;
    }

    //copy trap frame
    trapframe_copy = kmalloc(sizeof(struct trapframe));
    if (trapframe_copy == NULL) {
        proc_destroy(child_proc);
        return ENOMEM;
    }

    memcpy(trapframe_copy, tf, sizeof(struct trapframe));
    
    //copy file_table content
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

    unsigned long data2 = 0;
    // const char *name, struct proc *proc, void (*entrypoint)(void *data1, unsigned long data2), void *data1, unsigned long data2
    result = thread_fork("child thread", child_proc, child_entry_point, trapframe_copy, data2);
    if(result){
        kfree(trapframe_copy);
        proc_destroy(child);
        return result;
    }

    //return
    *retval = child_proc->pid;
    return 0;
}

void child_entry_point(void *trapframe, unsigned long data2) {
    (void) data2;
    enter_forked_process((struct trapframe *) trapframe);
}


int execv(const char *program, char **args){
    
}

int waitpid(int pid, userptr_t status, int options, int *retval){
    if (options != 0) {
        return EINVAL;
    }
    int child_index = pid - 1;

    struct pid* child_pid = get_struct_pid_by_pid(int pid);

    // check existence
    if(child_pid == NULL || pid < 1 || pid > PID_MAX){
        return ESRCH;
    }

    // check current process is actually its parent
    if (child_pid->ppid != curproc->pid) {
        return ECHILD;
    }

    // child already exit
    if(child_pid->EXIT) {
        *status = child_pid->exit_status;
        *retval = child_pid->pid;
        return 0;
    }
    // TODO: change cv to semaphore
    cv_wait(child_pid->EXIT_CV);

    *status = child_pid->exit_status;
    *retval = child_pid->pid;
    return 0;
}

int _exit(int exitcode){
    // TODO: replace pt with proc.c helper function
    int curIndex = curproc->pid - 1;
    while(curproc->children_proc->num > 0){
        struct proc *child = array_get(curproc->children_proc, 0);
        int childIndex = child->pid - 1;
        lock_acquire(pt->ptable_lock);
        if(pt->ptable[childIndex]->EXIT == true){
            proc_destroy(child);
        }else{
            pt->ptable[childIndex]->ppid = -1;
        }
        array_remove(curproc->children_proc, 0);
    }
    pt->ptable[curIndex]->EXIT = true;
    pt->ptable[curIndex]->exit_status = exitcode;
    // TODO change to semaphore
    cv_signal(pt->ptable[curIndex]->EXIT_CV);
    lock_release(pt->ptable_lock);

    // TODO thread exit
}

int getpid(int *retval){
    *retval = curproc->pid;
    return 0;
}