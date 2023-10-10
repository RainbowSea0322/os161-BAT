/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static int ropes_left = NROPES;

/* Data structures for rope mappings */
struct rope {
	struct lock *rlock;			// protects status of rope
	bool state; 				// false == binded; true == sever for check is severed or not 
};

struct stake {
	struct lock *slock;			// protects ropeIndex that stake connects to
	int index;					// index of rope that is attached to this stake
};

struct hook {
	int index;					// index of rope that is attached to this hook
};

static struct lock *rLLock; // lock for ropes_left
static struct cv *bFcv; // track the balloon ready or not 
static struct semaphore *finish;	// tracks completion status of all threads

/* Implement this! */

/* Synchronization primitives */

/* Implement this! */

static struct rope ropes[NROPES];
static struct stake stakes[NROPES];
static struct hook hooks[NROPES];

/* initialize the ropes and build the lock, cv, and semaphore*/
static void initialize(){
	for (int i = 0; i < NROPES; i++){
		ropes[i].state = false;
		ropes[i].rlock = lock_create("rlock");
		KASSERT(ropes[i].rlock != NULL);

		stakes[i].index = i;
		stakes[i].slock = lock_create("slock");
		KASSERT(stakes[i].slock != NULL);

		hooks[i].index = i;
	}
	
	rLLock = lock_create("rLLock");
	KASSERT(rLLock != NULL);
	bFcv = cv_create("bFcv");
	KASSERT(bFcv != NULL);
	finish = sem_create("finish", 0);
	KASSERT(finish != NULL);
}

/*clean up the memory*/
static void memoryFree(){
	for (int i = 0; i < NROPES; i++) {
		lock_destroy(ropes[i].rlock);
		ropes[i].rlock = NULL;
		lock_destroy(stakes[i].slock);
		stakes[i].slock = NULL;
	}
	lock_destroy(rLLock);
	cv_destroy(bFcv);
	sem_destroy(finish);
}

/*check the number of ropes and determine the balloon is ready*/
static void balloonReady(){
	lock_acquire(rLLock);
	ropes_left--;
	if(ropes_left == 0){
		cv_broadcast(bFcv, rLLock);
	}
	lock_release(rLLock);
}
/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

/* In dandelion, he only read the hook with hook index and get the rope in that hook with rope index(rope index is same with hook index)
* Then he check the rlock to check it is free or not if lock is free acquire the lock and mark it servered and move on.
*/


static
void
dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	
	kprintf("Dandelion thread starting\n");
	while(ropes_left > 0){// random find a hook and check the rope and check the rope severed or not, is not, severe it
		int hookIndex = random() % NROPES;
		int ropeIndex = hookIndex;
		lock_acquire(ropes[ropeIndex].rlock);
		if(ropes[ropeIndex].state == false){
			ropes[ropeIndex].state = true;
			kprintf("Dandelion severed rope %d\n", ropeIndex);
			balloonReady();
			thread_yield();
		}

		lock_release(ropes[ropeIndex].rlock);
	}
	kprintf("Dandelion thread done\n");
	V(finish);
	
}

/* In marigold, she random pick a stake and check the rope, if the rope lock is free and state of that rope is not sever(false), sever it
*/
static
void
marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");
	
	while(ropes_left > 0){//random select the stake and determine rope and check the rope severed or not, is not, severe it
		int stakeIndex = random() % NROPES;
		lock_acquire(stakes[stakeIndex].slock);
		int ropeIndex = stakes[stakeIndex].index;
		lock_acquire(ropes[ropeIndex].rlock);
		if(ropes[ropeIndex].state == false){
			ropes[ropeIndex].state = true;
			kprintf("Marigold severed rope %d from stake %d\n", ropeIndex, stakeIndex);
			balloonReady();
			thread_yield();
		}
		lock_release(ropes[ropeIndex].rlock);
		lock_release(stakes[stakeIndex].slock);
	}
	kprintf("Marigold thread done\n");
	V(finish);
}

/* Lord FlowerKiller randomly selects two different stakes and swaps the ropes attached to them, ensuring neither rope has already been severed.
*/
static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Lord FlowerKiller thread starting\n");
	while(ropes_left > 1){
		int stakeKIndex = random() % NROPES;
		int stakePIndex = random() % NROPES;

		if(stakeKIndex == stakePIndex){
			continue;
		}
		//determin the order 
		if(stakeKIndex < stakePIndex){
			lock_acquire(stakes[stakeKIndex].slock);
			lock_acquire(stakes[stakePIndex].slock);
		}else{
			lock_acquire(stakes[stakePIndex].slock);
			lock_acquire(stakes[stakeKIndex].slock);
		}

		int ropeKIndex = stakes[stakeKIndex].index;
		int ropePIndex = stakes[stakePIndex].index;

		lock_acquire(ropes[ropeKIndex].rlock);
		lock_acquire(ropes[ropePIndex].rlock);
		
		//check the state of those ropes, if any rope got sever, do nothing
		if(ropes[ropeKIndex].state == true || ropes[ropePIndex].state == true){
			lock_release(ropes[ropeKIndex].rlock);
			lock_release(ropes[ropePIndex].rlock);

			lock_release(stakes[stakeKIndex].slock);
			lock_release(stakes[stakePIndex].slock);

			continue;
		}else{
			stakes[stakeKIndex].index = ropePIndex;
			stakes[stakePIndex].index = ropeKIndex;
			lock_release(ropes[ropeKIndex].rlock);
			lock_release(ropes[ropePIndex].rlock);

			lock_release(stakes[stakeKIndex].slock);
			lock_release(stakes[stakePIndex].slock);

			kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\nLord FlowerKiller switched rope %d from stake %d to stake %d\n", ropeKIndex, stakeKIndex, stakePIndex, ropePIndex, stakePIndex, stakeKIndex);

			thread_yield();
		}
	}
	kprintf("Lord FlowerKiller thread done\n");
	V(finish);
	
}

/*
The balloon thread waits until all ropes have been severed and then announces that Prince Dandelion has escaped.*/
static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	lock_acquire(rLLock);//hold the rope_left lock and tell the balloon free to wait
	while (ropes_left > 0) {
		cv_wait(bFcv, rLLock);
		lock_release(rLLock);
	}

	kprintf("Balloon freed and Prince Dandelion escapes!\n");
	kprintf("Balloon thread done\n");
	V(finish);
}


// Change this function as necessary
int
airballoon(int nargs, char **args)
{

	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

	initialize();

	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

	// wait until all thread finish
	for (int i = 0; i < (N_LORD_FLOWERKILLER + 3); i++) {
		P(finish);
	}
	kprintf("Main thread done\n");

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	memoryFree();
	ropes_left = NROPES;// reset the rope left to avoid the multiple run bug
	return 0;
}
