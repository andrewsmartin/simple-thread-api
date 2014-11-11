#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "queue.h"
#include "thread.h"

static void busy_loop();

int sem;

int main(int argc, char *argv[])
{
    thread_init();

    int i;
    for (i = 0; i < 11; i++)
    {
        char buffer[20];
        sprintf(buffer, "Thread %d", i);
        thread_create(buffer, &busy_loop, SIGSTKSZ);
    }
    sem = create_semaphore(3);
    destroy_semaphore(sem);
    sem = create_semaphore(4);
    set_quantum_size(10000);
    runthreads();
    thread_state();
    
	return 0;
}

void busy_loop()
{
    int i = 0;
    semaphore_wait(sem);
    while (i < 400000000) {
        i++;
    }
    semaphore_signal(sem);
    thread_exit();
}
