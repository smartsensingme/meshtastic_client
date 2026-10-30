#ifndef TEST_KERNEL_H
#define TEST_KERNEL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define IS_ENABLED(x) (x)
#define K_MSEC(x) (x)
extern int64_t test_now, test_waited;
extern void (*test_on_wait)(void);
struct k_sem {
	int count;
};
#define K_SEM_DEFINE(n, i, l) struct k_sem n = {i}
static inline int64_t k_uptime_get(void)
{
	return test_now;
}
static inline void k_msleep(int64_t t)
{
	test_now += t;
}
static inline void k_sem_give(struct k_sem *s)
{
	s->count = 1;
}
static inline void k_sem_reset(struct k_sem *s)
{
	s->count = 0;
}
static inline int k_sem_take(struct k_sem *s, int64_t ms)
{
	test_waited = 0;
	if (s->count) {
		s->count = 0;
		return 0;
	}
	if (test_on_wait) {
		void (*fn)(void) = test_on_wait;
		test_on_wait = 0;
		fn();
	}
	if (s->count) {
		s->count = 0;
		return 0;
	}
	test_waited = ms;
	test_now += ms;
	return -EAGAIN;
}
#endif
