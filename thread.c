#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>

#include "thread.h"
#include "queue.h"

#define MAIN_TID -1

static int current_tid; /* Current thread id used by thread scheduler. */
static int tid_count = 0;
static int quantum_ns = 1000; /* Is this a good initial value? I dunno */

/* This library at the moment supports 100 threads. */
static thread_control_block *cb_table[100];

/* Initialize the current active (main) context. */
static ucontext_t uctx_main;

static Queue *run_queue;

static struct itimerval tval;

/* Utility function prototypes. */
void switch_thread();
void context_swap();

int thread_init()
{
    /* Initialize the run queue. */
    run_queue = queue_create();
}

int thread_create(char *threadname, void (*threadfunc)(), int stacksize)
{
    thread_control_block *cb = malloc(sizeof(thread_control_block));
    strncpy(cb->thread_name, threadname, THREAD_NAME_LEN);
    cb->thread_name[THREAD_NAME_LEN - 1] = '\0';
    
    /* Initialize ucontext_t */
    if (getcontext(&cb->context) == -1)
        printf("Error getting context...what the hell do i do now?\n");
    
    /* Allocate some stack space. */
    void *func_stack = malloc(stacksize);
    /* Create the thread context. */
    cb->context.uc_stack.ss_sp = func_stack;
    cb->context.uc_stack.ss_size = stacksize;
    cb->context.uc_link = &uctx_main;
    cb->func = threadfunc;
    makecontext(&cb->context, cb->func, 0);
    
    /* Set the state of the thread to RUNNABLE. */
    cb->state = RUNNABLE;
    
    /* Add this control block to the table. */
    cb_table[tid_count] = cb;
    
    /* Insert thread in the run queue. Apparently this is dependent on the system design (may not need to). */
    enqueue(run_queue, tid_count);
    
    tid_count++;
}

void thread_state()
{
    int i;
    
    printf("Thread Name\tState\tRunning Time\n");
    
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
        
        printf("%s\t%s\t%.2f\n", cb.thread_name, state, 4.2);
    }
}

void thread_exit()
{
    /* Remove the thread from the run queue. 
	   How the fuck do I get the thread id? OH SHIT IT'S STORED BY THE SCHEDULER!! */
       
    /* If the thread is running then it shouldn't be in the run queue...by they way I've set it up. 
       No need to pop. Also, don't remove from the table. */
    
    /* Set the state to EXIT. */
    thread_control_block *cb = cb_table[current_tid];
    cb->state = EXIT;
    
    /* Perform a context swap back to the scheduler. */
    context_swap();
}

void run_threads()
{
    /* For now I'm setting this up so that no context switch is performed here...seems 
       difficult to manage timing issues. I don't like that every thread switch will
       have to have this check but it works for now. */
    current_tid = MAIN;
    
    /* Configure and start the thread scheduler. */
    sigset(SIGALRM, switch_thread);
    set_quantum(100);
}

/* TODO: the assignment instructions say that n should be
   in nanoseconds...I should follow up by asking prof. I found
   the header code and suseconds_t (the type of tv_usec) appears 
   to just be an alias for an unsigned long. See
   http://www.sde.cs.titech.ac.jp/~gondow/dwarf2-xml/HTML-rxref/app/gcc-3.3.2/lib/gcc-lib/sparc-sun-solaris2.8/3.3.2/include/sys/types.h.html */
void set_quantum(int n)
{
    /* Set the timer value. */
    tval.it_interval.tv_sec = 0;
    tval.it_interval.tv_usec = n;
    tval.it_value.tv_sec = 0;
    tval.it_value.tv_usec = n;
    
    setitimer(ITIMER_REAL, &tval, NULL);
}

/*********************/
/* UTILITY FUNCTIONS */
/*********************/

/* A round-robin thread scheduler. */
void switch_thread()
{
    /* Pop thread from the run queue and insert current thread into back of queue. */
    if (queue_size(run_queue) <= 0) return; /* No threads left in run queue. */
    int tid = dequeue(run_queue);
    
    if (current_tid != MAIN)
    {
        /* Change current thread's state to RUNNABLE. */   
        thread_control_block *cb_curr = cb_table[current_tid];
        cb_curr->state = RUNNABLE;
        enqueue(run_queue, current_tid);
    }
    
    /* Change next thread's state to RUNNING. */
    thread_control_block *cb = cb_table[tid];
    cb->state = RUNNING;
    current_tid = tid;
    
    /* We are ready to perform a context switch. */
    context_swap();
}

/* Wrapper function to perform a context switch and handle any errors. */
void context_swap()
{
    ucontext_t *uctx;
    if (getcontext(uctx) == -1)
        printf("Error getting context...what the hell do i do now?\n");
    if (swapcontext(uctx, uctx->uc_link) == -1)
        printf("Couldn't swap context, the fuck??\n");
}
