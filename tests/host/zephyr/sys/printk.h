#ifndef TEST_PRINTK_H
#define TEST_PRINTK_H
int test_printk(const char *fmt, ...);
#define printk test_printk
#endif
