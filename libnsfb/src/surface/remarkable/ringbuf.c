#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "ringbuf.h"
#include "log.h"

bool ring_buf_init(ring_buf_t *buf, size_t capacity, size_t elem_size)
{
	memset(buf, 0, sizeof(*buf));
	if (capacity == 0 || elem_size == 0 || capacity > SIZE_MAX / elem_size)
		return false;
	buf->buffer = malloc(capacity * elem_size);
	if (buf->buffer == NULL) return false;
	buf->capacity = capacity;
	buf->elem_size = elem_size;
	if (pthread_mutex_init(&buf->mutex, NULL) != 0) goto fail;
	if (pthread_cond_init(&buf->ready, NULL) != 0) {
		pthread_mutex_destroy(&buf->mutex);
		goto fail;
	}
	return true;
fail:
	free(buf->buffer);
	return false;
}

void ring_buf_free(ring_buf_t *buf)
{
	pthread_cond_destroy(&buf->ready);
	pthread_mutex_destroy(&buf->mutex);
	free(buf->buffer);
}

bool ring_buf_write(ring_buf_t *buf, const void *item)
{
	pthread_mutex_lock(&buf->mutex);
	if (buf->used == buf->capacity) {
		/* A slow redraw must not overwrite an unread button release. */
		if (buf->capacity > SIZE_MAX / buf->elem_size / 2) goto fail;
		void *larger = malloc(buf->capacity * 2 * buf->elem_size);
		if (larger == NULL) goto fail;
		for (size_t i = 0; i < buf->used; i++) {
			memcpy((char *)larger + i * buf->elem_size,
			       (char *)buf->buffer + ((buf->tail + i) % buf->capacity) * buf->elem_size,
			       buf->elem_size);
		}
		free(buf->buffer);
		buf->buffer = larger;
		buf->tail = 0;
		buf->head = buf->used;
		buf->capacity *= 2;
	}
	memcpy((char *)buf->buffer + buf->head * buf->elem_size, item, buf->elem_size);
	buf->head = (buf->head + 1) % buf->capacity;
	buf->used++;
	pthread_cond_signal(&buf->ready);
	pthread_mutex_unlock(&buf->mutex);
	return true;
fail:
	pthread_mutex_unlock(&buf->mutex);
	ERROR_LOG("input: cannot grow event queue");
	return false;
}

bool ring_buf_wait(ring_buf_t *buf, void *item, const struct timespec *timeout)
{
	pthread_mutex_lock(&buf->mutex);
	while (buf->used == 0) {
		if (pthread_cond_timedwait(&buf->ready, &buf->mutex, timeout) != 0 &&
		    buf->used == 0) {
			pthread_mutex_unlock(&buf->mutex);
			return false;
		}
	}
	memcpy(item, (char *)buf->buffer + buf->tail * buf->elem_size, buf->elem_size);
	buf->tail = (buf->tail + 1) % buf->capacity;
	buf->used--;
	pthread_mutex_unlock(&buf->mutex);
	return true;
}
