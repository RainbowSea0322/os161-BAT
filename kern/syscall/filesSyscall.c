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
//open filetable not implenment yet

int open(const char *filename, int flags, mode_t mode, int *retval){
        
}

ssize_t read(int fd, void *buf, size_t buflen, int *retval){

}

ssize_t write(int fd, const void *buf, size_t nbytes, int *retval){

}

int close(int fd, int *retval){

}

off_t lseek(int fd, off_t pos, int whence, int *retval){

}

int chdir(const char *pathname, int *retval){

}

int dup2(int oldfd, int newfd, int *retval){

}

int __getcwd(char *buf, size_t buflen, int *retval){

}
