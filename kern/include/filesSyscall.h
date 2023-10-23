#ifndef _FILESSYSCALL_H_
#define _FILESSYSCALL_H_
#include <cdefs.h>
#include <kern/seek.h>

    int open(const char *filename, int flags, mode_t mode, int *retval);

    ssize_t read(int fd, void *buf, size_t buflen, int *retval);

    ssize_t write(int fd, const void *buf, size_t nbytes, int *retval);

    int close(int fd, int *retval);

    off_t lseek(int fd, off_t pos, int whence, int *retval);

    int chdir(const char *pathname, int *retval);
    
    int dup2(int oldfd, int newfd, int *retval);

    int __getcwd(char *buf, size_t buflen, int *retval);

#endif