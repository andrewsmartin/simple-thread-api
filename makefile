CFLAGS = -DHAVE_PTHREAD_RWLOCK=1 -Wall
OBJS = main.o thread.o queue.o

thread: ${OBJS}
	gcc ${OBJS} -o thread -lslack 
	
main.o: thread.h main.c
	gcc -c main.c ${CFLAGS}
	
thread.o: thread.c thread.h queue.h
	gcc -c thread.c ${CFLAGS}
	
queue.o: queue.c queue.h
	gcc -c queue.c ${CFLAGS}
	
clean:
	rm -f ${OBJS} thread
