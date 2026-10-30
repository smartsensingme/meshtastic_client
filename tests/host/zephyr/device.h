#ifndef TEST_DEVICE_H
#define TEST_DEVICE_H
#include <stdbool.h>
struct device {
	bool ready;
};
extern struct device test_usb;
#define DT_CHOSEN(x) 0
#define DT_ALIAS(x) 0
#define DEVICE_DT_GET(x) (&test_usb)
static inline bool device_is_ready(const struct device *d)
{
	return d && d->ready;
}
#endif
