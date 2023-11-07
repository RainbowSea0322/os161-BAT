#ifndef _PROCSYSCALLS_H_
#define _PROCSYSCALLS_H_
#include <cdefs.h>
#include <kern/seek.h>
#include <limits.h>
#include <addrspace.h>

int fork(struct trapframe *tf, int *retval);
int execv(const char *program, char **args);
int waitpid(int pid, userptr_t status, int options, int *retval);
int _exit(int exitcode);
int getpid(int *retval);

#endif