#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "queue.h"
#include "thread.h"

static void busy_loop();

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

    runthreads();

	return 0;
}

static void busy_loop()
{
    int i = 0;
    while (i < 1000000) i++;
    thread_exit();
}
