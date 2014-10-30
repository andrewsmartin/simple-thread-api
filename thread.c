#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <ucontext.h>

#include "thread.h"
#include "queue.h"

#define MAX_SEMAPHORES 100

static int current_tid; /* Current thread id used by thread scheduler. */
static int tid_count = 0;   /* Where to insert the next thread in the control block table. */
static int sem_count = 0;   /* The number of active semaphores. */

/* This library at the moment supports 1000 threads and 100 semaphores. */
static thread_control_block *cb_table[MAX_THREADS];
static sem_t *sem_table[MAX_SEMAPHORES] = {NULL};

static ucontext_t main_context;
static Queue *run_queue;
static struct itimerval tval;

/* Utility function prototypes. */
static void switch_thread();
static void context_swap(ucontext_t *active, ucontext_t *other);

int thread_init()
{
    run_queue = queue_create();
    return 0;
}

int thread_create(char *threadname, void (*threadfunc)(), int stacksize)
{
    if (tid_count >= MAX_THREADS) 
    {   
        printf("ERROR: Maximum number of threads exist. Aborting thread_create().\n");
        return -1;
    }

    thread_control_block *cb = malloc(sizeof(thread_control_block));
    
    /* Initialize ucontext_t */
    if (getcontext(&cb->context) == -1)
    {
        printf("CRITICAL: Could not get active context. Aborting thread_create().\n");
        free(cb);
        return -1;
    }
    
    strncpy(cb->thread_name, threadname, THREAD_NAME_LEN);
    cb->thread_name[THREAD_NAME_LEN - 1] = '\0';
    cb->sset = malloc(sizeof(sigset_t));
    sigemptyset(cb->sset);
    cb->oldset = malloc(sizeof(sigset_t));
    cb->elapsed_us = 0;
    
    /* Allocate some stack space. */
    /* TODO: Here I should make sure stacksize is within a good range. */
    void *func_stack = malloc(stacksize);
    /* Create the thread context. */
    cb->context.uc_stack.ss_sp = func_stack;
    cb->context.uc_stack.ss_size = stacksize;
    cb->context.uc_link = &main_context;
    cb->func = threadfunc;
    cb->state = RUNNABLE;
    makecontext(&cb->context, cb->func, 0);
    cb_table[tid_count] = cb;
    
    enqueue(run_queue, tid_count);
    
    return tid_count++;
}

void thread_exit()
{ 
    /* If the thread is running then it shouldn't be in the ready queue...just by they way I've set it up. */
    /* Set the state to EXIT. */
    thread_control_block *cb = cb_table[current_tid];
    printf("[Thread %d] exiting.\n", current_tid);
    cb->state = EXIT;
}

void runthreads()
{
    current_tid = queue_front(run_queue);
    getcontext(&main_context);
    main_context.uc_link = NULL;
    main_context.uc_stack.ss_sp = malloc(SIGSTKSZ);
    main_context.uc_stack.ss_size = SIGSTKSZ;
    makecontext(&main_context, &switch_thread, 0);
    /* Configure and start the thread scheduler. */
    sigset(SIGALRM, &switch_thread);
    set_quantum_size(100000);
    setitimer(ITIMER_REAL, &tval, 0);
}

/* TODO: the assignment instructions say that n should be
   in nanoseconds...I should follow up by asking prof. I found
   the header code and suseconds_t (the type of tv_usec) appears 
   to just be an alias for an unsigned long. See
   http://www.sde.cs.titech.ac.jp/~gondow/dwarf2-xml/HTML-rxref/app/gcc-3.3.2/lib/gcc-lib/sparc-sun-solaris2.8/3.3.2/include/sys/types.h.html */
void set_quantum_size(int n)
{
    /* Set the timer value. */
    tval.it_interval.tv_sec = 0;
    tval.it_interval.tv_usec = n;
    tval.it_value.tv_sec = 0;
    tval.it_value.tv_usec = n;
}

int create_semaphore(int value)
{
    if (sem_count >= MAX_SEMAPHORES) {
        printf("ERROR: Maximum number of active semaphores in use.\n");
        return -1;
    }
    /* Create a new semaphore. */
    sem_count++;
    sem_t *sem = malloc(sizeof(sem_t));
    sem->init = value;
    sem->count = value;
    sem->wait_queue = queue_create();
    /* Insert the semaphore in the first empty slot in the table. */
    int i;
    for (i = 0; sem_table[i] != NULL; i++);
    sem_table[i] = sem;
    return i;
}

void semaphore_wait(int semaphore)
{
    if (semaphore >= sem_count) return; /* This semaphore does not exist... */
    
    /* Block SIGALRM so this thread is not interrupted while operating on semaphore data. */
    thread_control_block *cb = cb_table[current_tid];
    sigaddset(cb->sset, SIGALRM);
    sigprocmask(SIG_BLOCK, cb->sset, cb->oldset);
    
    sem_t *sem = sem_table[semaphore];
    sem->count--;
    
    if (sem->count < 0)
    {
        /* The calling thread needs to be put in a waiting queue. */
        cb->state = BLOCKED;
        enqueue(sem->wait_queue, current_tid);
        /* Return control to the scheduler. */
        context_swap(&cb->context, &main_context);
    }
}

void semaphore_signal(int semaphore)
{
    if (semaphore >= sem_count) return; /* This semaphore does not exist... */
    
    /* Block SIGALRM so this thread is not interrupted while operating on semaphore data. */
    thread_control_block *cb = cb_table[current_tid];
    sigaddset(cb->sset, SIGALRM);
    sigprocmask(SIG_BLOCK, cb->sset, cb->oldset);
    
    sem_t *sem = sem_table[semaphore];
    sem->count++;
    
    if (sem->count <= 0)    /* i.e. If there are threads waiting on this semaphore...*/
    {
        /* No threads are waiting on this semaphore; nothing to do. Should not happen in principle. */
        if (queue_size(sem->wait_queue) <= 0) return;
        
        /* Pop a blocked thread from the wait queue of this semaphore. Re-enable signaling asap. */
        int tid = dequeue(sem->wait_queue);
        
        /* The signalling thread is now finished operating on semaphore internals. Can unblock SIGALRM. */
        sigprocmask(SIG_SETMASK, cb->oldset, NULL);
        
        thread_control_block *cb_w = cb_table[tid];
        cb_w->state = RUNNABLE;
        /* We must make sure that the waiting thread is put back onto the run queue before
           unblocking the signal, or else this thread will never be re-scheduled! */
        enqueue(run_queue, tid);
        
        sigprocmask(SIG_SETMASK, cb_w->oldset, NULL);
    }
}

void destroy_semaphore(int semaphore)
{
    if (semaphore >= MAX_SEMAPHORES) {
        printf("ERROR: Semaphore id %d greater than maximum allowed id\n.", semaphore);
        return;
    }
    
    sem_t *sem = sem_table[semaphore];
    
    if (sem == NULL) {
        printf("ERROR: No semaphore exists with id %d\n.", semaphore);
        return;
    }
    
    /* Check to see if threads are waiting first. */
    if (queue_size(sem->wait_queue) > 0)
    {
        printf("ERROR: Cannot destroy semaphore %d because there are threads waiting on it.\n", semaphore);
        return;
    }
    else
    {
        if (sem->count != sem->init)
            printf("WARNING: One or more threads have waited on semaphore %d but not yet signalled.\n", semaphore);
        queue_release(sem->wait_queue);
        free(sem);
        sem_table[semaphore] = NULL;
    }
}

void thread_state()
{   
    printf("Thread Name\tState\t\tElapsed Time (ms)\n");
    int i;
    for (i = 0; i < 100; i++)
    {
        char state[20];
        thread_control_block cb = *cb_table[i];
        
        switch (cb.state)
        {
            case RUNNABLE:
                strcpy(state, "RUNNABLE");
                break;
            case RUNNING:
                strcpy(state, "RUNNING");
                break;
            case BLOCKED:
                strcpy(state, "BLOCKED");
                break;
            case EXIT:
                strcpy(state, "EXIT");
                break;
            default:
                strcpy(state, "ERROR-UNKNOWN");
        }
        
        printf("%s\t%s\t%.3f\n", cb.thread_name, state, cb.elapsed_us / 1000.0);
    }
}

/*********************/
/* UTILITY FUNCTIONS */
/*********************/

/* A round-robin thread scheduler. */
void switch_thread()
{
    printf("[%d] Interrupted...\n", current_tid);
    /* Pop thread from the run queue and insert current thread into back of queue. */
    int tid = dequeue(run_queue);
    
    thread_control_block *cb_curr = cb_table[current_tid];
    /* Add time delta onto elapsed time of previous thread (should be a close approx? TODO find out. */
    cb_curr->elapsed_us += tval.it_interval.tv_usec;
    
    if (cb_curr->state == RUNNING) {
        cb_curr->state = RUNNABLE;
        enqueue(run_queue, current_tid);
        printf("[%d] Added to run queue.\n", current_tid);
    }
    
    /* Change next thread's state to RUNNING. */
    thread_control_block *cb = cb_table[tid];
    cb->state = RUNNING;
    current_tid = tid;
    printf("[%d] Resuming...\n", current_tid);
    /* We are ready to perform a context switch. */
    context_swap(&main_context, &cb->context);
}

/* Wrapper function to perform a context switch and handle any errors. */
void context_swap(ucontext_t *active, ucontext_t *other)
{
    if (swapcontext(active, other) == -1)
        printf("ERROR: Error switching context.\n");
}
