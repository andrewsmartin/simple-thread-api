#include <slack/std.h>
#include <slack/list.h>

#include "queue.h"

Queue *queue_create()
{
    return list_create(NULL);
}

void enqueue(Queue *q, int n)
{
    list_append_int(q, n);
}

int dequeue(Queue *q)
{
    return list_shift_int(q);
}

void queue_release(Queue *q)
{
    list_release(q);
}

int queue_size(Queue *q)
{
    return list_length(q);
}

int queue_front(Queue *q)
{
    return list_item_int(q, 0);
}

int queue_back(Queue *q)
{
    return list_item_int(q, list_length(q) - 1);
}

int queue_empty(Queue *q)
{
    return list_empty(q);
}
