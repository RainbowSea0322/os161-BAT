#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <coremap.h>

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 *
 * NOTE: it's been found over the years that students often begin on
 * the VM assignment by copying dumbvm.c and trying to improve it.
 * This is not recommended. dumbvm is (more or less intentionally) not
 * a good design reference. The first recommendation would be: do not
 * look at dumbvm at all. The second recommendation would be: if you
 * do, be sure to review it from the perspective of comparing it to
 * what a VM system is supposed to do, and understanding what corners
 * it's cutting (there are many) and why, and more importantly, how.
 */

/* under dumbvm, always have 72k of user stack */
/* (this must be > 64K so argument blocks of size ARG_MAX will fit) */
#define PAGE_SIZE 4096
#define DUMBVM_STACKPAGES    18//for vm_fault compile

/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

void
vm_bootstrap(void)
{

	paddr_t last_paddr = ram_getsize(); // ram starts at paddr 0
	paddr_t first_free_paddr = ram_getfirstfree();
	// check alignment
	if (first_free_paddr % PAGE_SIZE == 0) {
		cm_paddr = first_free_paddr;
	} else {
		cm_paddr = first_free_paddr - first_free_paddr % PAGE_SIZE + PAGE_SIZE;
	}
	coremap = (struct cm_entry *)PADDR_TO_KVADDR(cm_paddr);


	total_pages = ((last_paddr - cm_paddr) / PAGE_SIZE); // global variable, round down to total number of complete pages automatically

	int cm_pages = (total_pages * sizeof(struct cm_entry) / PAGE_SIZE);
	if (total_pages * sizeof(struct cm_entry) % PAGE_SIZE != 0) {
		cm_pages++;
	}

	for(int i = 0; i < total_pages; i++){
		if (i < cm_pages){
			coremap[i].ALLOCATE = true;
			coremap[i].COREMAP = true;
		} else {
			coremap[i].ALLOCATE = false;
			coremap[i].COREMAP = false;
		}		
	}
	cm_ready = true;
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);

	spinlock_release(&stealmem_lock);
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	// if (npages == 0 || npages > (unsigned)total_pages) {
	// 	return 0;
	// }
	paddr_t pa;
	vaddr_t va;
	if(cm_ready){
		spinlock_acquire(&cm_spinlock);
		unsigned available_pages = 0;
		int start_page = -1;
		for(int i = 0; i < total_pages; i++){
			if(!coremap[i].ALLOCATE){
				if (start_page == -1) {
					start_page = i;
					available_pages = 0;
				}
				available_pages++;
			}else{
				available_pages = 0;
				start_page = -1;
			}
			// got enough available pages, no more searching needed
			if(available_pages == npages){
				break;
			}
		}
		// not enough spaces
		if(available_pages < npages){
			spinlock_release(&cm_spinlock);
			return 0;
		}
		if(available_pages > npages){
			spinlock_release(&cm_spinlock);
			panic("somthing wrong with my searching alogorithm! debug please");
			return 0;
		}
		KASSERT(start_page != -1);
		pa = cm_paddr + start_page * PAGE_SIZE;
		va = PADDR_TO_KVADDR(pa);
		//bzero (void *vblock, size_t len)
		bzero((void *) va, npages * PAGE_SIZE);

		coremap[start_page].num_pages = (int) npages;
		coremap[start_page].page_vaddr = va;
		for(int i = start_page; i < start_page + (int) npages; i++){
			coremap[i].ALLOCATE = true;
		}
		spinlock_release(&cm_spinlock);
		return va;
	}else{
		pa = getppages(npages); // steal ram mem 
		va = PADDR_TO_KVADDR(pa);
		if (pa==0) {
			return 0;
		}
		return va;
	}

	panic("alloc_incorrect");
}

void
free_kpages(vaddr_t addr)
{
	spinlock_acquire(&cm_spinlock);
	for (int i = 0; i < total_pages; i++){
		if(coremap[i].page_vaddr == addr){
			if (coremap[i].COREMAP) {
				panic("can't free core map pages");
			} else {
				for(int j = i; j < i + coremap[i].num_pages; j++){
					coremap[j].ALLOCATE = false;
				}
			}
			break;
		}
	}
	spinlock_release(&cm_spinlock);
}

void
vm_tlbshootdown_all(void)
{
	// for (int i = 0; i < total_pages; i++) {
	// 	if (!coremap[i].COREMAP) {
	// 		struct tlbshootdown *ts;
	// 		vm_tlbshootdown(ts)
	// 	}
	// }
	panic("current vm doesn't support tlbshootdown");
}


void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
}
