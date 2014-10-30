all: thread

thread: main.o thread.o queue.o
	gcc main.o thread.o queue.o -o thread -lslack 
	
main.o: thread.h main.c
	gcc -c main.c -DHAVE_PTHREAD_RWLOCK=1 -Wall
	
thread.o: thread.c thread.h queue.h
	gcc -c thread.c -DHAVE_PTHREAD_RWLOCK=1 -Wall
	
queue.o: queue.c queue.h
	gcc -c queue.c -DHAVE_PTHREAD_RWLOCK=1 -Wall
	
clean:
	rm -rf *o thread
