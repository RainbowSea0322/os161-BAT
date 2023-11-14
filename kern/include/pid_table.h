#ifndef _PIDTABLE_H_
#define _PIDTABLE_H_

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <synch.h>

struct pid_table {
    struct pid *ptable[PID_MAX];
    struct lock *ptable_lock;
} pid_table;

struct pid {
    int curproc_pid;
    int ppid;
	int exit_status;
	bool Exit;
	struct semaphore * EXIT_SEM;
}pid;
//function for pid table
struct pid_table * pt_create();
void pt_destroy(struct pid_table *pt);
#endif