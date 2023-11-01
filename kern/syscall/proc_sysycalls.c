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
#include <proc_syscalls.h>

pid_t fork(struct trapframe *tf, int *retval){

}
int execv(const char *program, char **args){

}
int waitpid(int pid, userptr_t status, int options, int *retval){

}
int _exit(int exitcode){

}
int getpid(int *retval){

}