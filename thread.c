#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>

#include "thread.h"
#include "queue.h"

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
    
    /* Set the state of the thread to RUNNING. */
    cb->state = RUNNING;
    
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
	   How the fuck do I get the thread id? I can get the current context...but
       that has no info regarding thread id. 
       
       I need to somehow get the control block because I need to change the state
       inside the control block. Maybe I could do something incredibly hacky by
       calling thread_create with the same name to get the id. */
       
       /* Perform a context swap back to the successor context. */
        ucontext_t *uctx;
        if (getcontext(uctx) == -1)
            printf("Error getting context...what the hell do i do now?\n");
        if (swapcontext(uctx, uctx->uc_link) == -1)
            printf("Couldn't swap context, the fuck??\n");
}

void run_threads()
{
    /* Activate the thread switcher. */
    sigset(SIGALRM, switch_thread);
    tval.it_interval.tv_sec = 0;
    tval.it_interval.tv_usec = 100;
    tval.it_value.tv_sec = 0;
    tval.it_value.tv_usec = 100;
    setitimer(ITIMER_REAL, &tval, 0);
}

/* TODO: the assignment instructions say that n should be
   in nanoseconds...I should follow up by asking prof. I found
   the header code and suseconds_t (the type of tv_usec) appears 
   to just be an alias for an unsigned long. See
   http://www.sde.cs.titech.ac.jp/~gondow/dwarf2-xml/HTML-rxref/app/gcc-3.3.2/lib/gcc-lib/sparc-sun-solaris2.8/3.3.2/include/sys/types.h.html */
void set_quantum(int n)
{
    tval.it_interval.tv_usec = n;
}

/*********************/
/* UTILITY FUNCTIONS */
/*********************/

void switch_thread()
{
    /* Pop a value from the run queue */
    if (queue_size(run_queue) <= 0) return; /* No threads left in run queue. */
    int tid = dequeue(run_queue);
    
    thread_control_block *cb = cb_table[tid];
    /* Perform a context switch. */
    context_swap();
}

void context_swap()
{
    ucontext_t *uctx;
    if (getcontext(uctx) == -1)
        printf("Error getting context...what the hell do i do now?\n");
    if (swapcontext(uctx, uctx->uc_link) == -1)
        printf("Couldn't swap context, the fuck??\n");
}
