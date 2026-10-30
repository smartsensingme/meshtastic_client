#ifndef TEST_ATOMIC_H
#define TEST_ATOMIC_H
typedef int atomic_t;
static inline int atomic_set(atomic_t *a, int v)
{
	int old = *a;
	*a = v;
	return old;
}
static inline void atomic_clear(atomic_t *a)
{
	*a = 0;
}
static inline int atomic_get(atomic_t *a)
{
	return *a;
}
static inline void atomic_add(atomic_t *a, int v)
{
	*a += v;
}
static inline void atomic_or(atomic_t *a, int v)
{
	*a |= v;
}
#endif
