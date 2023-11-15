#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <pid_table.h>
#include <vnode.h>
#include <synch.h>

struct pid_table *pt_create(){
    struct pid_table *pt = kmalloc(sizeof(pid_table));

	pt->ptable_lock  = lock_create("pid_table_lock");
	if (pt->ptable_lock == NULL) {
        kfree(pt);
		panic("failed to create pid table lock\n");
	}
    for (int i = 0; i < PID_MAX; i++) {
        pt->ptable[i] = NULL;
    }

    return pt;    
}
void pt_destroy(struct pid_table *pt){
    if (pt == NULL) {
        return;
    }

    lock_destroy(pt->ptable_lock);
    for (int i = 0; i < PID_MAX; i++) {
        pt->ptable[i] = NULL;
    }
}