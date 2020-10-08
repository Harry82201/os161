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
//ropeMappings use hook as index, so the objects in ropeMappings are ropes which have connected stakes 
typedef struct ropeMappings{
	struct lock *lock_rope;
	volatile bool severed;
	volatile int stake;
} ropeMappings;

/* Synchronization primitives */
//this lock the number of unsevered ropes
struct lock *lock_rope_counter;
//lock_rope_counter = lock_create("rope counter");

//index of ropeList is hook N, the object at certain index is the rope
static ropeMappings ropeList[NROPES - 1];

//bool whether main could exit
bool main_exit = false;

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

static
void
dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n");

	/* Implement this function */

	while(ropes_left > 0){
		//randomly generate a hook number
		int currHook = random() % NROPES;
		//select the current rope
		ropeMappings currRope = ropeList[currHook];
		lock_acquire(currRope.lock_rope);
		
		//check if current rope is severed, if true, release the lock, if false, unhook the rope
		if(currRope.severed == true){
			lock_release(currRope.lock_rope);
		}else{
			//one rope is severed
			lock_acquire(lock_rope_counter);
			ropes_left--;
			lock_release(lock_rope_counter);

			currRope.severed = true;
			kprintf("Dandelion severed rope %d\n", currHook);
			lock_release(currRope.lock_rope);
			thread_yield();
		}
		//thread_yield();
	}
	kprintf("Dandelion thread done\n");
	thread_exit();
}

static
void
marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");

	/* Implement this function */
	
	while(ropes_left > 0){
		int currStake = random() % NROPES;
		for(int i = 0; i < NROPES; i++){
			if(currStake == ropeList[i].stake){
				ropeMappings currRope = ropeList[i];
				//flower killer may change stake here, double check later
				lock_acquire(currRope.lock_rope);

				//ensure that flowerkiller does not change stake after setting cuuRope and before lock currRope
				if(currStake == ropeList[i].stake){
					if(currRope.severed == true){
						lock_release(currRope.lock_rope);
					}else{
						lock_acquire(lock_rope_counter);
						ropes_left--;
						lock_release(lock_rope_counter);

						currRope.severed = true;
						kprintf("Marigold severed rope %d from stake %d\n", i, currStake);
						lock_release(currRope.lock_rope);
						thread_yield();
					}
				}else{
					lock_release(currRope.lock_rope);
				}
				
			}
		}
		//thread_yield();
	}
	kprintf("Marigold thread done\n");
	thread_exit();
}

static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Lord FlowerKiller thread starting\n");

	/* Implement this function */
}

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	/* Implement this function */

	//check if all ropes are severed
	while(true){
		if(ropes_left == 0){	
			break;
		} else {
			thread_yield();
		}
	}

	kprintf("Ballon freed and Prince Dandelion escapes!\n");
	kprintf("Ballon thread done\n");
	main_exit = 1;
	thread_exit();

}


// Change this function as necessary
int
airballoon(int nargs, char **args)
{

	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

	//initialize ropeList
	for(i = 0; i < NROPES; i++){
		ropeList[i].lock_rope = lock_create("rope lock");
		ropeList[i].severed = false;
		ropeList[i].stake = i;
	}

	lock_rope_counter = lock_create("rope counter");

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

	while(main_exit == false){
		thread_yield();
	}

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	return 0;
}
