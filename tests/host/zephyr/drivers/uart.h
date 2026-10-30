#include <zephyr/device.h>
int uart_err_check(const struct device *d);
int uart_irq_update(const struct device *d);
int uart_irq_rx_ready(const struct device *d);
int uart_fifo_read(const struct device *d, unsigned char *b, int n);
void uart_poll_out(const struct device *d, unsigned char b);
int uart_irq_callback_user_data_set(const struct device *d,
				    void (*fn)(const struct device *, void *), void *u);
void uart_irq_rx_enable(const struct device *d);
void uart_irq_rx_disable(const struct device *d);

int uart_irq_tx_ready(const struct device *d);
int uart_fifo_fill(const struct device *d, const unsigned char *b, int n);
void uart_irq_tx_enable(const struct device *d);
void uart_irq_tx_disable(const struct device *d);
