#ifndef __THREAD_H
#define __THREAD_H

#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>

#include "queue.h"

#define THREAD_NAME_LEN 128

typedef unsigned int uint;

typedef enum {RUNNABLE, RUNNING, BLOCKED, EXIT} t_state;

typedef struct _thread_control_block
{
    ucontext_t context;
    char thread_name[THREAD_NAME_LEN];
    t_state state;
    double elapsed_ms;
} thread_control_block;

typedef struct sem
{
    int count, init;
    Queue *wait_queue;
} sem_t;

int thread_init();
int thread_create(char *threadname, void (*runfunc)(), int stacksize);
void thread_exit();
void runthreads();
void set_quantum_size(int quantum);
int create_semaphore(int value);
void semaphore_wait(int semaphore);
void semaphore_signal(int semaphore);
void destroy_semaphore(int semaphore);
void thread_state();

#endif
