// scheduler.cc 
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would 
//	end up calling FindNextToRun(), and that would put us in an 
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------

int cmpPriority(Thread* a, Thread* b){
    return a->getPriority() > b->getPriority();
}

int cmpPredict(Thread*a, Thread* b){
    return a->getPredict() / 2 + a->getLastTime() / 2 > b->getPredict() / 2 + b->getLastTime() / 2;
}

Scheduler::Scheduler()
{ 
    L1 = new SortedList<Thread *>(cmpPredict);
    L2 = new SortedList<Thread *>(cmpPriority);
    L3 = new List<Thread *>;
    toBeDestroyed = NULL;
} 

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{ 
    delete L1;
    delete L2;
    delete L3;
} 

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void
Scheduler::ReadyToRun (Thread *thread)
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());
	//cout << "Putting thread on ready list: " << thread->getName() << endl ;
    thread->setStatus(READY);
    thread->setAgingCount(kernel->stats->totalTicks);

    if (thread->getPriority() < 50) {
        printf("Tick %d: Thread %d is inserted into queue L3\n", kernel->stats->totalTicks, thread->getID());
        L3->Append(thread);
    } else if (thread->getPriority() < 100) {
        printf("Tick %d: Thread %d is inserted into queue L2\n", kernel->stats->totalTicks, thread->getID());
        L2->Insert(thread);
    } else if (thread->getPriority() < 150) {
        printf("Tick %d: Thread %d is inserted into queue L1\n", kernel->stats->totalTicks, thread->getID());
        L1->Insert(thread);
 		if(kernel->currentThread->getPriority() > 100 && cmpPredict(thread, kernel->currentThread))
			kernel->interrupt->YieldOnReturn();
   }
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

void Scheduler::aging(List<Thread *> *list){
    ListIterator<Thread*> *iter = new ListIterator<Thread*>((List<Thread*>*)list);
    for( ; iter->IsDone() != true; iter->Next()){
        Thread* now = iter->Item();
        if(kernel->stats->totalTicks - now->getAgingCount() > 1500){
            now->setAgingCount(now->getAgingCount() + 1500);
            now->setPriority(now->getPriority() + 10);
            if(now->getPriority() > 149) now->setPriority(149);
            printf("Tick %d: Thread %d changes its priority from %d to %d\n", kernel->stats->totalTicks, now->getID(), now->getPriority()-10, now->getPriority());
            list->Remove(now);
            if(now->getPriority() > 99){
                if(list != L1){
                    printf("Tick %d: Thread %d is removed from queue L2\n", kernel->stats->totalTicks, now->getID());
                    printf("Tick %d: Thread %d is inserted into queue L1\n", kernel->stats->totalTicks, now->getID());
                }
                L1->Insert(now);
            } else if(now->getPriority() > 49){
                 if(list != L2){
                    printf("Tick %d: Thread %d is removed from queue L3\n", kernel->stats->totalTicks, now->getID());
                    printf("Tick %d: Thread %d is inserted into queue L2\n", kernel->stats->totalTicks, now->getID());
                }
                L2->Insert(now);
            } else {
                L3->Append(now);
            }
        }
    }
}

Thread *
Scheduler::FindNextToRun ()
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    
    aging(L1);
    aging(L2);
    aging(L3);

    Thread* thread;

    if (!L1->IsEmpty()) {
        kernel->alarm->setRoundRobin(false);
        thread = L1->RemoveFront();
        printf("Tick %d: Thread %d is removed from queue L1\n", kernel->stats->totalTicks, thread->getID());
        thread->setPredict(thread->getPredict() / 2 + thread->getLastTime() / 2);
        thread->setLastTime(0);
        return thread;
    } else if (!L2->IsEmpty()) {
        kernel->alarm->setRoundRobin(false);
        thread = L2->RemoveFront();
        printf("Tick %d: Thread %d is removed from queue L2\n", kernel->stats->totalTicks, thread->getID());
        thread->setLastTime(0);
        return thread;
    } else if (!L3->IsEmpty()) {
        kernel->alarm->setRoundRobin(true);
        thread = L3->RemoveFront();
        printf("Tick %d: Thread %d is removed from queue L3\n", kernel->stats->totalTicks, thread->getID());
        thread->setLastTime(0);
        return thread;
    } else return NULL;
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void
Scheduler::Run (Thread *nextThread, bool finishing)
{
    Thread *oldThread = kernel->currentThread;
    
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) {	// mark that we need to delete current thread
         ASSERT(toBeDestroyed == NULL);
	 toBeDestroyed = oldThread;
    }
    
    if (oldThread->space != NULL) {	// if this thread is a user program,
        oldThread->SaveUserState(); 	// save the user's CPU registers
	oldThread->space->SaveState();
    }
    
    oldThread->CheckOverflow();		    // check if the old thread
					    // had an undetected stack overflow

    kernel->currentThread = nextThread;  // switch to the next thread
    nextThread->setStatus(RUNNING);      // nextThread is now running
    
    DEBUG(dbgThread, "Switching from: " << oldThread->getName() << " to: " << nextThread->getName());

    printf("Tick %d: Thread %d is now selected for execution\n", kernel->stats->totalTicks, nextThread->getID());
    printf("Tick %d: Thread %d is replaced, and it has executed %d ticks\n", kernel->stats->totalTicks, oldThread->getID(), oldThread->getLastTime());    
    // This is a machine-dependent assembly language routine defined 
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    SWITCH(oldThread, nextThread);

    // we're back, running oldThread
      
    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed();		// check if thread we were running
					// before this one has finished
					// and needs to be cleaned up
    
    if (oldThread->space != NULL) {	    // if there is an address space
        oldThread->RestoreUserState();     // to restore, do it.
	oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void
Scheduler::CheckToBeDestroyed()
{
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
	toBeDestroyed = NULL;
    }
}
 
//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void
Scheduler::Print()
{
    cout << "Ready list contents:\n";
    //readyList->Apply(ThreadPrint);
}