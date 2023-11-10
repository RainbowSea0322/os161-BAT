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
    struct proc *ptable[PID_MAX];
    struct lock *ptable_lock;
} pid_table;

//function for pid table
struct pid_table * pt_create(void);
void pt_destroy(struct pid_table *pt);
#endif