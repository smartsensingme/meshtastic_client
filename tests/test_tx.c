/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <meshtastic_client/frame.h>
#include "../src/tx_internal.h"

int64_t test_now, test_waited;
void (*test_on_wait)(void);
static struct device device = {true};
static bool enabled, auto_irq, no_ready, zero_fill;
static int fifo_error;
static unsigned wakes, fifo_calls;
static unsigned char output[32768];
static size_t used;

static void wake(void)
{
	++wakes;
}
int uart_irq_tx_ready(const struct device *dev)
{
	assert(dev == &device);
	return enabled && !no_ready ? 1 : 0;
}
int uart_fifo_fill(const struct device *dev, const unsigned char *data, int length)
{
	assert(dev == &device);
	++fifo_calls;
	if (fifo_error) {
		int e = fifo_error;
		fifo_error = 0;
		return e;
	}
	if (zero_fill)
		return 0;
	int n = MIN(length, 3);
	assert(used + (size_t)n <= sizeof(output));
	memcpy(output + used, data, n);
	used += n;
	return n;
}
void uart_irq_tx_disable(const struct device *dev)
{
	assert(dev == &device);
	enabled = false;
}
static void pump(void)
{
	unsigned budget = 10000;
	while (enabled && !no_ready) {
		assert(budget--);
		mc_tx_irq();
	}
}
void uart_irq_tx_enable(const struct device *dev)
{
	assert(dev == &device);
	enabled = true;
	if (auto_irq)
		pump();
}
static void reset(void)
{
	enabled = auto_irq = no_ready = zero_fill = false;
	fifo_error = 0;
	used = wakes = fifo_calls = 0;
	test_now = 0;
	mc_tx_init(&device, wake);
}
int main(void)
{
	reset();
	unsigned char bytes[] = {10, 20, 30};
	struct mc_tx_result result;
	struct mc_tx_stats stats;
	assert(mc_tx_write(NULL, 1, 1, false) == -EINVAL);
	assert(mc_tx_write(bytes, MC_FRAME_MAX + 1, 1, false) == -EINVAL);

	for (unsigned i = 0; i < CONFIG_MESHTASTIC_TX_QUEUE_DEPTH; ++i) {
		bytes[0] = 10 + i;
		assert(!mc_tx_write(bytes, sizeof(bytes), i + 1, false));
	}
	memset(bytes, 99, sizeof(bytes));
	assert(!used && !fifo_calls && test_now == 0);
	assert(mc_tx_write(bytes, sizeof(bytes), 99, false) == -EAGAIN);
	mc_tx_irq();
	assert(used == 3 && !mc_tx_take(&result));
	pump();
	assert(!enabled && wakes == CONFIG_MESHTASTIC_TX_QUEUE_DEPTH);
	assert(mc_tx_write(bytes, sizeof(bytes), 99, false) == -EAGAIN);
	for (unsigned i = 0; i < CONFIG_MESHTASTIC_TX_QUEUE_DEPTH; ++i) {
		assert(mc_tx_take(&result) && result.packet_id == i + 1);
		assert(!result.status && result.accepted_bytes == 7);
		const unsigned char expected[] = {0x94, 0xc3, 0, 3, 10 + i, 20, 30};
		assert(!memcmp(output + i * 7, expected, sizeof(expected)));
	}
	assert(!mc_tx_take(&result) && mc_tx_deadline() == INT64_MAX);

	/* No partial admission of padding+request when only one slot remains. */
	for (unsigned i = 1; i < CONFIG_MESHTASTIC_TX_QUEUE_DEPTH; ++i)
		assert(!mc_tx_write(bytes, sizeof(bytes), i, false));
	mc_tx_snapshot(&stats);
	unsigned before = stats.enqueued;
	assert(mc_tx_write(bytes, sizeof(bytes), 0, true) == -EAGAIN);
	mc_tx_snapshot(&stats);
	assert(stats.enqueued == before);
	mc_tx_abort(-ECANCELED);
	for (unsigned i = 1; i < CONFIG_MESHTASTIC_TX_QUEUE_DEPTH; ++i)
		assert(mc_tx_take(&result) && result.status == -ECANCELED &&
		       !result.accepted_bytes);

	/* Partial frame fault, bounded IRQ, and peer framing resynchronization. */
	reset();
	unsigned char payload[] = {1, 2, 3, 4, 5, 6};
	assert(!mc_tx_write(payload, sizeof(payload), 42, false));
	mc_tx_irq();
	mc_tx_irq();
	assert(used == 6);
	fifo_error = -EIO;
	unsigned calls = fifo_calls;
	mc_tx_irq();
	assert(fifo_calls == calls + 1 && !enabled && mc_tx_error(test_now) == -EIO);
	assert(!mc_tx_take(&result));
	mc_tx_abort(-EIO);
	assert(mc_tx_take(&result) && result.status == -EIO && result.accepted_bytes == 6);
	assert(!mc_tx_write(payload, sizeof(payload), 0, true));
	pump();
	struct mc_frame peer = {0};
	unsigned complete_new = 0;
	for (size_t i = 0; i < used; ++i) {
		int rc = mc_frame_byte(&peer, output[i]);
		if (rc == 1 && peer.length == sizeof(payload) &&
		    !memcmp(peer.data, payload, sizeof(payload)))
			++complete_new;
	}
	assert(complete_new == 1);
	assert(mc_tx_take(&result) && !result.status && !result.packet_id);
	assert(result.accepted_bytes == MC_FRAME_MAX + 4);
	assert(mc_tx_take(&result) && !result.status &&
	       result.accepted_bytes == sizeof(payload) + 4);

	reset();
	no_ready = true;
	assert(!mc_tx_write(payload, sizeof(payload), 42, false));
	assert(mc_tx_deadline() == CONFIG_MESHTASTIC_TX_TIMEOUT_MS);
	assert(!mc_tx_error(CONFIG_MESHTASTIC_TX_TIMEOUT_MS - 1));
	assert(mc_tx_error(CONFIG_MESHTASTIC_TX_TIMEOUT_MS) == -ETIMEDOUT);
	mc_tx_abort(-ETIMEDOUT);
	assert(mc_tx_take(&result) && result.status == -ETIMEDOUT);

	reset();
	zero_fill = true;
	assert(!mc_tx_write(payload, sizeof(payload), 42, false));
	mc_tx_irq();
	assert(fifo_calls == 1 && !enabled && mc_tx_error(0) == -EIO);

	/* Completion may race enqueue return, but data was published first. */
	reset();
	auto_irq = true;
	unsigned char max_payload[MC_FRAME_MAX];
	memset(max_payload, 0x33, sizeof(max_payload));
	assert(!mc_tx_write(max_payload, sizeof(max_payload), 99, false));
	assert(mc_tx_take(&result) && result.packet_id == 99 &&
	       result.accepted_bytes == MC_FRAME_MAX + 4);
	assert(output[2] == 2 && output[3] == 0);
	puts("PASS: IRQ TX ownership/FIFO order, partial fill, admission, cancellation, framing "
	     "recovery, timeout and no-progress IRQ");
	return 0;
}
