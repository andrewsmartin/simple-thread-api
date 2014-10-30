#ifndef __QUEUE_H
#define __QUEUE_H

#include <slack/std.h>
#include <slack/list.h>

typedef List Queue;

Queue *queue_create();
void enqueue(Queue*, int);
int dequeue(Queue*);
void queue_release(Queue*);
int queue_size(Queue*);

#endif
