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
    for (i = 0; i < 10; i++)
    {
        char buffer[20];
        sprintf(buffer, "Thread %d", i);
        thread_create(buffer, &busy_loop, SIGSTKSZ);
    }
    sem = create_semaphore(9); 
    runthreads();
    printf("Threads exited; printing state.\n");
    thread_state();
    
	return 0;
}

void busy_loop()
{
    int i = 0;
    while (i < 100000000) {
        semaphore_wait(sem);
        i++;
        semaphore_signal(sem);
    }
    thread_exit();
}
