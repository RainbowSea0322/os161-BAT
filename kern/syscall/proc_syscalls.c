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
    int err;
    
    err = check_argument_validity_execv(args);
    if (err) {
        return err;
    }

    // 1. copy the program name and argument array pointer
    char *program_copy; = kmalloc(PATH_MAX);
    if (program_copy == NULL) {
        return ENOMEM;
    }
    err = copyinstr((userptr_t) program, program_copy, PATH_MAX, NULL);
    if (err) {
        kfree(program_copy);
        return err;
    }

    // 2. copy all the arguments in args
    // 2.a count the number of arguments by the NULL terminator
    int num_arg;
    for (num_arg = 0; args[num_arg] != NULL; num_arg++){
        // do nothing inside
    };

    // 2.b create a buffer in kernel store the num_arg and all the arguments (except the NULL teminator)
    // 2.b.(1) create the arguments' pointers array
    char **arg_pointers = kmalloc(sizeof(char *) * num_arg);
    if (arg_pointers == NULL) {
        kfree(program_copy);
        return ENOMEM;
    }
    copyin((userptr_t) args, arg_pointers, sizeof(char**));

    int stack_space_needed = 0;
    
    // 2.b.(3) actaully copy the argument pointers and arguments to kernel (modify the pointer to be kernel pointer actually pointing to the kernel stack)
    for (int i = 0; i < num_arg; i++) {
        // copy the argument pointers to arg_pointers[0, num_arg]
        copyin((userptr_t) args + i * sizeof(char *), arg_pointers + i * sizeof(char *), sizeof(char*));

        // copy the actual arguments to arg_array
        // a. create space to store the argument
        int cur_arg_length = strlen(args[i]);
        // add 1 to include the '/0' terminator
        cur_arg_length ++;

        char * arg_pointers[i] = kmalloc(sizeof(char) * cur_arg_length);
        if (arg_pointers[i] == NULL) {
            kfree(program_copy);
            kfree(arg_pointers);
            return ENOMEM;
        }
        err = copyinstr((userptr_t) args[i], arg_pointers[i], sizeof(char));
        if (err) {
            kfree(program_copy);
            kfree(arg_pointers[i])
            kfree(arg_pointers);
            return err;
        }

        // increase buffer size by aligned argument length
        int num_stack_blocks;
        if (cur_arg_length % 4 == 0) {
            num_stack_blocks = cur_arg_length/4;
        } else {
            num_stack_blocks = cur_arg_length/4 + 1;
        }

        stack_space_needed += num_stack_blocks;
    }

    // 3. prepare to load program in the current address space by load_elf
    struct vnode *prog_vnode;
    err = vfs_open(progname, O_RDONLY, 0, &prog_vnode);
    if (err) {
        for(int i = 0; i < num_arg; i++){
            kfree(arg_pointers[i]);
        }
        kfree(arg_pointers);
        kfree(progname);
        return err;
    }
    kfree(progname);

    struct addrspace *cur_addrspace = as_create();
    if (cur_addrspace == NULL) {
        for(int i = 0; i < num_arg; i++){
            kfree(arg_pointers[i]);
        }
        kfree(arg_pointers);
        return ENOMEM;
    }

    struct addrspace *old_addrspace = proc_setas(cur_addrspace);
    as_activate();

    vaddr_t pc;
    err = load_elf(prog_vnode, &pc);
    if (err) {
        for(int i = 0; i < num_arg; i++){
            kfree(arg_pointers[i]);
        }
        kfree(arg_pointers);
        proc_setas(old_addrspace);
        as_activate();
        as_destroy(cur_addrspace);
        return err;
    }

    vaddr_t sp;
    err = as_define_stack(cur_addrspace, &sp);
    if (err) {
        for(int i = 0; i < num_arg; i++){
            kfree(arg_pointers[i]);
        }
        kfree(arg_pointers);
        proc_setas(old_addrspace);
        as_activate();
        as_destroy(cur_addrspace);
        return err;
    }

    // create array for storing argument locations in user stack
    char **arg_loc = kmalloc(sizeof(char *) * (num_arg + 1));
    if (arg_loc == NULL) {
        for(int i = 0; i < num_arg; i++){
            kfree(arg_pointers[i]);
        }
        kfree(arg_pointers);
        proc_setas(old_addrspace);
        as_activate();
        as_destroy(cur_addrspace);
        return ENOMEM;
    }

    sp -= stack_space_needed;// find sp to keep enough space for program

    //copyout to user stack

     for (int i = 0; i < argc; i++) {
        // copy argument string onto user stack from kernel buffer
        int arglen = strlen(arg_pointers[i]) + 1;
        err = copyoutstr(arg_pointers[i], (userptr_t) sp, arglen, NULL);
        if (err) {
            kfree_buf(arg_pointers, argc);
            kfree_cur_addrspace(old_addrspace, cur_addrspace);
            kfree(arg_loc);
            return err;
        }
        
        // store address of current argument
        arg_loc[i] = (char *) sp;

        // leave extra padding so each argument is aligned to 4 bytes
        sp += get_arglen(arglen);
    }


}

int check_argument_validity_execv(char ** args) {
    int err;

    // check args it self is a valid pointer 
    char **args_check = kmalloc(sizeof(char*));
    if (args_check == NULL) {
        return ENOMEM;
    }
    err = copyin((userptr_t) args, arg_pointer, sizeof(char*));
    if (err) {
        kfree(args_check);
        return err;
    }
    kfree(args_check);

    // check argument pointer is valid
    int num_arg;
    // keep counting up until hit the NULL
    for (num_arg = 0; args[num_arg] != NULL; num_arg++);
    // too many arguments check
    if (num_arg > ARG_MAX) {
        return E2BIG;
    }

    char **arg_pointers_check = kmalloc(sizeof(char*) * num_arg);
    if (arg_pointers_check == NULL) {
        return ENOMEM;
    }
    char *arg_check = kmalloc(sizeof(char) * ARG_MAX);
    if (arg_check == NULL) {
        kfree(arg_pointers_check);
        return ENOMEM;
    }
    for (int i = 0; i < num_arg; i++) {
        // check argument pointer validity
        err = copyin((userptr_t) args + sizeof(char *) * i, arg_pointers_check + sizeof(char *) * i, sizeof(char*));
        if (err) {
            kfree(arg_check);
            kfree(arg_pointers_check);
            return err;
        }

        // check argument validity
        err = copyinstr((userptr_t) args[i], arg_check, sizeof(char) * ARG_MAX, NULL);
        if (err) {
            kfree(arg_check);
            kfree(arg_pointers_check);
            return err;
        }
    }

    // all checks passed
    kfree(arg_check);
    kfree(arg_pointers_check);
    return 0;
}

int waitpid(int pid, userptr_t status, int options, int *retval){
    if (options != 0) {
        return EINVAL;
    }
    int child_index = pid - 1;

    struct pid* child_pid = get_struct_pid_by_pid(int pid);

    // check existence
    if(child_pid == NULL || pid < PID_MIN || pid > PID_MAX){
        return ESRCH;
    }

    // check current process is actually its parent
    if (child_pid->ppid != curproc->pid) {
        return ECHILD;
    }

    int exit_status, result;
    // child already exit
    if(child_pid->EXIT) {
        if(status != NULL){
            exit_status = child_pid->exit_status;
            result = copyout(&exit_status, status, sizeof(int));
            if (result) {
                return EFAULT;
            }
        }
        *retval = child_pid->pid;
        return 0;
    }
    P(child_pid->EXIT_SEM);
    if(status != NULL){
        exit_status = child_pid->exit_status;
        result = copyout(&exit_status, status, sizeof(int));
        if (result) {
            return EFAULT;
        }
    }

    lock_acquire(curproc->children_proc_lock);
    for (int i = 0; i < curproc->children_proc->num; i++) {
        struct proc *childProc = array_get(curproc->children_proc, i);
        if (childProc->pid == pid) {
            array_remove(curproc->children_proc, i);
            proc_destroy(childProc);
            break;
        }
    }
    lock_release(curproc->children_proc_lock);
    *retval = child_pid->pid;
    return 0;
}

int _exit(int exitcode){
    int curIndex = curproc->pid - 1;
    struct pid *curpid = get_struct_pid_by_pid(curproc->pid);
    while(curproc->children_proc->num > 0){
        struct proc *child = array_get(curproc->children_proc, 0);
        int childIndex = child->pid - 1;
        struct pid *childpid = get_struct_pid_by_pid(child->pid);
        if(childpid->EXIT == true){
            proc_destroy(child);
        }else{
            childpid->ppid = -1;
        }
        array_remove(curproc->children_proc, 0);
    }
    curpid->EXIT = true;
    curpid->exit_status = exitcode;
    V(curpid->EXIT_SEM);
    thraed_exit();
}

int getpid(int *retval){
    *retval = curproc->pid;
    return 0;
}