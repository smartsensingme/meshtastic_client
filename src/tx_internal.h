/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MC_TX_INTERNAL_H
#define MC_TX_INTERNAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>

/* Internal bounded TX, one owner thread + UART ISR. No application callback in ISR.
 * Slots remain owned until completion is reaped, bounding the completion backlog.
 */
struct mc_tx_result {
	uint32_t packet_id;    /**< Id de aplicação, zero para controle. */
	size_t accepted_bytes; /**< Bytes FIFO com header, não wire idle. */
	int status;	       /**< Zero completo ou errno negativo. */
};
struct mc_tx_stats {
	uint32_t enqueued; /**< Aceitos, saturante desde init. */
	uint32_t drained;  /**< FIFO completa, saturante. */
	uint32_t failed;   /**< Abortados, saturante. */
	uint32_t rejected; /**< Recusados por admissão, saturante. */
	uint32_t bytes;	   /**< Bytes FIFO, saturante. */
	uint32_t pending;  /**< Slots incluindo completados não reapados. */
};
void mc_tx_init(const struct device *device, void (*wake)(void));
/* Copies payload plus 94c3 header. Optional zero padding is admitted atomically
 * with the request. Returns -EAGAIN if capacity cannot hold the entire batch.
 */
int mc_tx_write(const uint8_t *payload, size_t length, uint32_t packet_id, bool resync);
/* ISR only, after uart_irq_update. One bounded FIFO call, no spin/poll loops. */
void mc_tx_irq(void);
/* Owner only: accepted bytes mean FIFO ownership transfer, not wire idle/ACK. */
bool mc_tx_take(struct mc_tx_result *out);
void mc_tx_abort(int status);
int mc_tx_error(int64_t now_ms);
int64_t mc_tx_deadline(void);
void mc_tx_snapshot(struct mc_tx_stats *out);
#endif
