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
    int result;

    //create proc with pid, file table already created here
    child_proc = proc_create_runprogram("child_proc");
    
    if (child_proc == NULL) {
        return ENPROC;
    }

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
        proc_destroy(child_proc);
        return result;
    }

    array_add(curproc->children_proc, child_proc, NULL);

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
    char *program_copy = kmalloc(PATH_MAX);
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

        arg_pointers[i] = kmalloc(sizeof(char) * cur_arg_length);
        if (arg_pointers[i] == NULL) {
            kfree(program_copy);
            kfree(arg_pointers);
            return ENOMEM;
        }
        err = copyinstr((userptr_t) args[i], arg_pointers[i], sizeof(char) * cur_arg_length, NULL);
        if (err) {
            kfree(program_copy);
            // free all previous argument contents
            free_arg_pointers(arg_pointers, i + 1);
            return err;
        }

        // increase buffer size by aligned argument length
        int num_stack_blocks;
        if (cur_arg_length % 4 == 0) {
            num_stack_blocks = cur_arg_length;
        } else {
            num_stack_blocks = (cur_arg_length/4 + 1) * 4;
        }

        stack_space_needed += num_stack_blocks;
    }

    // 3. prepare to load program in the current address space by load_elf
    struct vnode *program_vnode;
    err = vfs_open(program_copy, O_RDONLY, 0, &program_vnode);  // char *path, int openflags, mode_t mode(meaningless as we are linux like system), struct vnode **ret
    if (err) {
        free_arg_pointers(arg_pointers, num_arg);
        kfree(program_copy);
        return err;
    }
    kfree(program_copy);
    // prepare address space for load_elf
    struct addrspace *new_as = as_create();
    if (new_as == NULL) {
        free_arg_pointers(arg_pointers, num_arg);
        return ENOMEM;
    }

    struct addrspace *old_as = proc_setas(new_as);
    as_activate();

    vaddr_t pc; // entrypoint of new process
    err = load_elf(program_vnode, &pc);
    if (err) {
        free_arg_pointers(arg_pointers, num_arg);
        // revert address space change
        revert_as(old_as, new_as);
        return err;
    }

    // use as_define_stack() get a USERSTACK pointer
    vaddr_t sp;
    err = as_define_stack(new_as, &sp);
    if (err) {
        free_arg_pointers(arg_pointers, num_arg);
        revert_as(old_as, new_as);
        return err;
    }

    // create array for storing argument locations in user stack
    char **arg_pointers_user = kmalloc(sizeof(char *) * (num_arg + 1));
    if (arg_pointers_user == NULL) {
        free_arg_pointers(arg_pointers, num_arg);
        revert_as(old_as, new_as);;
        return ENOMEM;
    }

    sp -= stack_space_needed;// get enough space for all arguments

    // 4. copy out arguments to user stack
    for (int i = 0; i < num_arg; i++) {
        // copy argument string to user stack
        int cur_arg_length = strlen(arg_pointers[i]) + 1; // + 1 for the NULL terminator at the end
        err = copyoutstr(arg_pointers[i], (userptr_t) sp, cur_arg_length , NULL);
        if (err) {
            free_arg_pointers(arg_pointers, num_arg);
            revert_as(old_as, new_as);
            kfree(arg_pointers_user);
            return err;
        }
        
        // store address of current argument
        arg_pointers_user[i] = (char *) sp;

        // need 4 bytes alignments on stack
        int num_stack_blocks;
        if (cur_arg_length % 4 == 0) {
            num_stack_blocks = cur_arg_length;
        } else {
            num_stack_blocks = (cur_arg_length/4 + 1) * 4;
        }

        sp += num_stack_blocks;
    }
    free_arg_pointers(arg_pointers, num_arg);

    // make sure arg_pointers_user[num_arg] is NULL 
    arg_pointers_user[num_arg] = NULL;

    // 5. set up argument pointers according to the address of each argument on stack

    sp -= stack_space_needed; // back to the top of arguments
    sp -= (num_arg + 1) * (sizeof(char *)); // move sp to top of stack, extra 1 for the NULL terminator

    for (int i = 0; i < num_arg + 1; i++) {
        err = copyout(&(arg_pointers_user[i]), (userptr_t) sp, sizeof(char *));
        if (err) {
            revert_as(old_as, new_as);
            kfree(arg_pointers_user);
            return err;
        }
        sp += sizeof(char *); // move sp down to 
    }
    kfree(arg_pointers_user);

    // move sp back to top for program running
    sp -= (num_arg + 1) * (sizeof(char *));
    // everything was successful, no more need the old address space backup 
    as_destroy(old_as);

    enter_new_process(num_arg, (userptr_t) sp, NULL, sp, pc); // int argc, userptr_t argv, userptr_t env, vaddr_t stack, vaddr_t entry
    return 0;

}

int check_argument_validity_execv(char ** args) {
    int err;

    // check args points to a valid user space
    char **args_check = kmalloc(sizeof(char*));
    if (args_check == NULL) {
        return ENOMEM;
    }
    err = copyin((userptr_t) args, args_check, sizeof(char*));
    if (err) {
        kfree(args_check);
        return err;
    }
    kfree(args_check);

    // check argument number is within boundary
    int num_arg;
    // keep counting up until hit the NULL
    for (num_arg = 0; args[num_arg] != NULL; num_arg++);
    // too many arguments check
    if (num_arg > ARG_MAX) {
        return E2BIG;
    }

    // check every argument pointer lives in a valid user space
    char **arg_pointers_check = kmalloc(sizeof(char*) * num_arg);
    if (arg_pointers_check == NULL) {
        return ENOMEM;
    }
    // check every argument lives in a valid user sapce as well
    char *arg_check = kmalloc(sizeof(char) * ARG_MAX);
    if (arg_check == NULL) {
        kfree(arg_pointers_check);
        return ENOMEM;
    }
    for (int i = 0; i < num_arg; i++) {
        // check argument pointer validity
        err = copyin((userptr_t) &(args[i]), &(arg_pointers_check[i]), sizeof(char*));
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

void free_arg_pointers(char **arg_pointers, int end_index) {
    for(int i = 0; i < end_index; i++){
        kfree(arg_pointers[i]);
    }
    kfree(arg_pointers);
}

void revert_as(struct addrspace *old_as, struct addrspace *new_as) {
    proc_setas(old_as);
    as_activate();
    as_destroy(new_as);
}

int waitpid(int pid, userptr_t status, int options, int *retval){
    if (options != 0) {
        return EINVAL;
    }

    struct pid* child_pid = get_struct_pid_by_pid(pid);
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
    if(child_pid->Exit) {
        if(status != NULL){
            exit_status = child_pid->exit_status;
            result = copyout(&exit_status, status, sizeof(int));
            if (result) {
                return EFAULT;
            }
        }
        *retval = child_pid->curproc_pid;
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

    *retval = child_pid->curproc_pid;
    return 0;
}

int _exit(int exitcode){
    
    // deal with waitpid Scenario 3: Parent exits before the child exits
    while(curproc->children_proc->num > 0){
        lock_acquire(curproc->children_proc_lock);
        struct proc *child = array_get(curproc->children_proc, 0);
        struct pid *child_pid = get_struct_pid_by_pid(child->pid);
        if(child_pid->Exit == true){
            proc_destroy(child);
        }else{
            lock_pid_table();
            child_pid->ppid = -1;
            unlock_pid_table();
        }
        array_remove(curproc->children_proc, 0);
        lock_release(curproc->children_proc_lock);
    }
    struct pid *curpid = get_struct_pid_by_pid(curproc->pid);
    lock_pid_table();
    curpid->Exit = true;
    curpid->exit_status = exitcode;
    V(curpid->EXIT_SEM);
    unlock_pid_table();
    thread_exit();
}

int getpid(int *retval){
    *retval = curproc->pid;
    return 0;
}