#include <types.h>
#include <spinlock.h>
#include <machine/vm.h>
#include <addrspace.h>

bool cm_ready = false;
struct cm_entry *coremap;
paddr_t cm_paddr;

static struct spinlock cm_spinlock = SPINLOCK_INITIALIZER;

int total_pages;

struct cm_entry {
    vaddr_t page_vaddr;
    int num_pages;
    bool ALLOCATE;
    bool COREMAP;
} cm_entry;

