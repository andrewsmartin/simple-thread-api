#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "thread.h"

void random_sleep();

int main(int argc, char *argv[])
{
    srand(time(NULL));
    thread_init();
    
    /* Spawn a few threads to sleep for a random time interval. */
    int i;
    for (i = 0; i < 10; i++)
    {
        char buffer[20];
        sprintf(buffer, "Thread %d", i);
        thread_create(buffer, &random_sleep, SIGSTKSZ);
    }
    
    run_threads();
    while (1)
    {
        //thread_state();
        sleep(5);
    }
	return 0;
}

void random_sleep()
{
    sleep(rand() % 5);
    //printf("Finished.\n");
    thread_exit();
}
