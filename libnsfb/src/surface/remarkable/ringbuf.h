#ifndef RM_RING_BUF_H
#define RM_RING_BUF_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

typedef struct ring_buf_s {
	void *buffer;
	size_t capacity, elem_size;
	size_t head, tail, used;
	pthread_mutex_t mutex;
	pthread_cond_t ready;
} ring_buf_t;

bool ring_buf_init(ring_buf_t *buf, size_t capacity, size_t elem_size);
void ring_buf_free(ring_buf_t *buf);
bool ring_buf_write(ring_buf_t *buf, const void *item);
bool ring_buf_wait(ring_buf_t *buf, void *item, const struct timespec *timeout);

#endif
