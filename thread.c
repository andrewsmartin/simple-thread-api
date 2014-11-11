#define _XOPEN_SOURCE 500
#include <signal.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>

#include "thread.h"
#include "queue.h"

#define MAX_THREADS 1000
#define MAX_SEM 100

static int current_tid; /* Current thread id used by thread scheduler. */
static int num_threads = 0; /* Number of created threads */
static int num_sem = 0;   /* The number of active semaphores. */
static thread_control_block **cb_table;
static sem_t **sem_table;
static ucontext_t main_context, scheduler_context;
static Queue *run_queue;
static struct itimerval tval;
static struct timespec prev;
struct timespec proc_clock;

/* Utility function prototypes. */
static void schedule_threads();
static void switch_thread();
static void context_swap(ucontext_t *active, ucontext_t *other);
static int get_available_semaphore_slot();

int thread_init()
{
    /* Allocate space MAX_THREADS threads. */
    cb_table = calloc(MAX_THREADS, sizeof(thread_control_block*));
    sem_table = calloc(MAX_SEM, sizeof(sem_t*));
    run_queue = queue_create();
    set_quantum_size(10000);
    return 0;
}

int thread_create(char *threadname, void (*threadfunc)(), int stacksize)
{
    if (num_threads >= MAX_THREADS) {
        printf("Error: Maximum number of threads in use; not creating new thread.\n");
        return -1;
    }

    thread_control_block *cb = malloc(sizeof(thread_control_block));
    
    /* Initialize ucontext_t */
    if (getcontext(&cb->context) == -1)
    {
        printf("CRITICAL: Could not get active context. Aborting thread_create().\n");
        free(cb);
        return -2;
    }
    
    strncpy(cb->thread_name, threadname, THREAD_NAME_LEN);
    cb->thread_name[THREAD_NAME_LEN - 1] = '\0';
    cb->elapsed_ms = 0;
    
    /* Create the thread context. */
    cb->context.uc_stack.ss_sp = malloc(stacksize);
    cb->context.uc_stack.ss_size = stacksize;
    cb->context.uc_link = &scheduler_context;
    cb->state = RUNNABLE;
    makecontext(&cb->context, threadfunc, 0);
    
    int tid_index = num_threads;
    cb_table[tid_index] = cb;
    
    enqueue(run_queue, tid_index);
    num_threads++;
    
    return tid_index;
}

void thread_exit()
{ 
    /* Set the state to EXIT. */
    thread_control_block *cb = cb_table[current_tid];
    cb->state = EXIT;
    /* Do not free the control block yet. */
}

void runthreads()
{   
    /* Configure and start the thread scheduler. */
    getcontext(&scheduler_context);
    scheduler_context.uc_stack.ss_sp = malloc(SIGSTKSZ);
    scheduler_context.uc_stack.ss_size = SIGSTKSZ;
    scheduler_context.uc_link = &main_context;
    sigaddset(&scheduler_context.uc_sigmask, SIGALRM);
    makecontext(&scheduler_context, &schedule_threads, 0); 
    sigset(SIGALRM, &switch_thread);
    /* Switch control to the thread scheduler. */
    context_swap(&main_context, &scheduler_context);
}

void set_quantum_size(int n)
{
    if (n < 500) {
        printf("Error: Value smaller than minimum quantum size (500). Ignoring.\n");
        return;
    }
    /* Set the timer value. */
    int sec = n / 1000000;
    int usec = n % 1000000;
    tval.it_interval.tv_sec = sec;
    tval.it_interval.tv_usec = usec;
    tval.it_value.tv_sec = sec;
    tval.it_value.tv_usec = usec;
}

int create_semaphore(int value)
{
    int sem_index = get_available_semaphore_slot();
    if (sem_index == -1) {
        printf("Error: maximum number of semaphores already in use.\n");
        return -1;
    }
    /* Create a new semaphore. */
    num_sem++;
    sem_t *sem = malloc(sizeof(sem_t));
    sem->init = value;
    sem->count = value;
    sem->wait_queue = queue_create();
    /* Insert the semaphore in the first empty slot in the table. */
    sem_table[sem_index] = sem;
    return sem_index;
}

void semaphore_wait(int semaphore)
{
    if (semaphore >= num_sem) return; /* This semaphore does not exist... */
    
    /* Block SIGALRM so this thread is not interrupted while operating on semaphore data. */
    thread_control_block *cb = cb_table[current_tid];
    sigaddset(&cb->context.uc_sigmask, SIGALRM);
    sigprocmask(SIG_BLOCK, &cb->context.uc_sigmask, NULL);
    
    sem_t *sem = sem_table[semaphore];
    sem->count--;
    
    if (sem->count < 0)
    {
        printf("Thread %d waiting for semaphore...\n", current_tid);
        /* The calling thread needs to be put in a waiting queue. */
        cb->state = BLOCKED;
        enqueue(sem->wait_queue, current_tid);
        /* Return control to the scheduler. */
        context_swap(&cb->context, &scheduler_context);
    }
    else {
        printf("Thread %d acquired semaphore [count=%d].\n", current_tid, sem->count);
        sigprocmask(SIG_UNBLOCK, &cb->context.uc_sigmask, NULL);
    }
}

void semaphore_signal(int semaphore)
{
    if (semaphore >= num_sem) return; /* This semaphore does not exist... */
    
    /* Block SIGALRM so this thread is not interrupted while operating on semaphore data. */
    thread_control_block *cb = cb_table[current_tid];
    sigaddset(&cb->context.uc_sigmask, SIGALRM);
    sigprocmask(SIG_BLOCK, &cb->context.uc_sigmask, NULL);
    
    sem_t *sem = sem_table[semaphore];
    sem->count++;
    printf("Thread %d signalled semaphore [count=%d]\n", current_tid, sem->count);
    if (sem->count <= 0)    /* i.e. If there are threads waiting on this semaphore...*/
    {
        /* No threads are waiting on this semaphore; nothing to do. Should not happen in principle. */
        if (queue_size(sem->wait_queue) <= 0) return;
        
        /* Pop a blocked thread from the wait queue of this semaphore. Re-enable signaling asap. */
        int tid = dequeue(sem->wait_queue);
        printf("Thread %d woken up.\n", tid);
        /* The signalling thread is now finished operating on semaphore internals. Can unblock SIGALRM. */
        sigprocmask(SIG_UNBLOCK, &cb->context.uc_sigmask, NULL);
        
        thread_control_block *cb_w = cb_table[tid];
        cb_w->state = RUNNABLE;
        sigprocmask(SIG_UNBLOCK, &cb_w->context.uc_sigmask, NULL);
        /* We must make sure that the waiting thread is put back onto the run queue before
           unblocking the signal, or else this thread will never be re-scheduled! */
        enqueue(run_queue, tid);
    }
    else {
        sigprocmask(SIG_UNBLOCK, &cb->context.uc_sigmask, NULL);
    }
}

void destroy_semaphore(int semaphore)
{
    if (semaphore >= num_sem) {
        printf("ERROR: Semaphore with id %d does not exist\n.", semaphore);
        return;
    }
    
    sem_t *sem = sem_table[semaphore];
    
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
    printf("\nThread Name\tState\t\tElapsed Time (ms)\n");
    int i;
    for (i = 0; i < num_threads; i++)
    {
        char state[20];
        thread_control_block *cb = cb_table[i];
        if (cb == NULL) continue;
        
        switch (cb->state)
        {
            case RUNNABLE:
                strcpy(state, "RUNNABLE");
                break;
            case RUNNING:
                strcpy(state, "RUNNING\t");
                break;
            case BLOCKED:
                strcpy(state, "BLOCKED\t");
                break;
            case EXIT:
                strcpy(state, "EXIT\t");
                break;
            default:
                strcpy(state, "ERROR-UNKNOWN");
        }
        
        printf("%s\t%s\t%.3f\n", cb->thread_name, state, cb->elapsed_ms);
    }
    printf("\n");
}

/*********************/
/* UTILITY FUNCTIONS */
/*********************/

/* Simple FCFS thread scheduler. */
void schedule_threads()
{   
    sigprocmask(SIG_BLOCK, &scheduler_context.uc_sigmask, NULL);
    setitimer(ITIMER_REAL, &tval, NULL);
    /* While there are still threads in the run queue... */
    while (queue_size(run_queue) > 0)
    {
        /* Get ready to switch context to next thread in queue. */
        current_tid = dequeue(run_queue);
        printf("Current tid: %d\n", current_tid);
        thread_control_block *cb = cb_table[current_tid];
        cb->state = RUNNING;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &prev);
        context_swap(&scheduler_context, &cb->context);
    }
    /* Turn off the timer before giving control back to main. */
    tval.it_interval.tv_sec = 0;
    tval.it_interval.tv_usec = 0;
    tval.it_value.tv_sec = 0;
    tval.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &tval, 0);
    printf("All threads finished.\n");
}

/* A handler to SIGALRM that moves the current thread to the back of the run queue. */
static void switch_thread()
{   
    thread_control_block *cb_curr = cb_table[current_tid];
    
    if (cb_curr->state == RUNNING) {
        cb_curr->state = RUNNABLE;
        enqueue(run_queue, current_tid);
    }
    
    struct timespec tp;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tp);
    cb_curr->elapsed_ms += tp.tv_sec * 1000.0 + tp.tv_nsec / 1000000.0 - (prev.tv_sec * 1000.0 + prev.tv_nsec / 1000000.0);
    context_swap(&cb_curr->context, &scheduler_context);
}

/* Wrapper function to perform a context switch and handle any errors. */
void context_swap(ucontext_t *active, ucontext_t *other)
{
    if (swapcontext(active, other) == -1)
        printf("ERROR: Error switching context.\n");
}

static int get_available_semaphore_slot()
{
    int i;
    for (i = 0; i < MAX_SEM; i++)
    {
        if (sem_table[i] == NULL) return i;
    }
    /* No more space left... */
    return -1;
}
