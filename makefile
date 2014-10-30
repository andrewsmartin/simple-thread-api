all: thread

thread: main.o thread.o queue.o
	gcc main.o thread.o queue.o -o thread -lslack 
	
main.o: main.c
	gcc -c main.c -DHAVE_PTHREAD_RWLOCK=1
	
thread.o: thread.c
	gcc -c thread.c -DHAVE_PTHREAD_RWLOCK=1
	
queue.o: queue.c
	gcc -c queue.c -DHAVE_PTHREAD_RWLOCK=1
	
clean:
	rm -rf *o thread
