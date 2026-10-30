#ifndef TEST_RING_H
#define TEST_RING_H
#include <stdint.h>
#include <stddef.h>
struct ring_buf {
	unsigned char *buf;
	size_t cap, head, tail, used;
};
#define RING_BUF_DECLARE(n, s)                                                                     \
	static unsigned char n##_storage[s];                                                       \
	struct ring_buf n = {n##_storage, s, 0, 0, 0}
static inline void ring_buf_reset(struct ring_buf *r)
{
	r->head = r->tail = r->used = 0;
}
static inline uint32_t ring_buf_put(struct ring_buf *r, const unsigned char *b, uint32_t n)
{
	uint32_t i = 0;
	while (i < n && r->used < r->cap) {
		r->buf[r->tail] = b[i++];
		r->tail = (r->tail + 1) % r->cap;
		r->used++;
	}
	return i;
}
static inline uint32_t ring_buf_get(struct ring_buf *r, unsigned char *b, uint32_t n)
{
	uint32_t i = 0;
	while (i < n && r->used) {
		b[i++] = r->buf[r->head];
		r->head = (r->head + 1) % r->cap;
		r->used--;
	}
	return i;
}
static inline uint32_t ring_buf_size_get(struct ring_buf *r)
{
	return r->used;
}
#endif
