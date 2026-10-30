/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <limits.h>
#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

#include "tx_internal.h"
#include <meshtastic_client/frame.h>

/** Bytes de payload máximo mais header serial; capacidade de cada slot privado. */
#define TX_FRAME_BYTES (MC_FRAME_MAX + 4)

/* Um slot pertence à fila desde a cópia até mc_tx_take, mesmo após FIFO completa. */
struct tx_slot {
	uint8_t bytes[TX_FRAME_BYTES]; /* Frame serial ou padding de recuperação. */
	size_t length;		       /* Bytes válidos no slot. */
	size_t offset;		       /* Bytes já aceitos pelo driver. */
	uint32_t packet_id;	       /* Zero para controle, id para aplicação. */
	int status;		       /* Resultado retido até reap. */
};

static struct tx_slot slots[CONFIG_MESHTASTIC_TX_QUEUE_DEPTH];
static struct k_spinlock lock;
static const struct device *uart;
static void (*signal_owner)(void);
static size_t head;
static size_t count;
static size_t done;
static int error_code;
static int64_t last_progress;
static struct mc_tx_stats stats;

/** Interna: put/mc_tx_write/mc_tx_irq/mc_tx_abort. Soma saturante uint32 para estatísticas TX; lock
 * já deve estar adquirido. */
static void add(uint32_t *value, uint32_t amount)
{
	*value = UINT32_MAX - *value < amount ? UINT32_MAX : *value + amount;
}

/** Interna: mc_init. Reinicia fila/estatísticas com IRQ quiescente e sem slots; não reutilizar
 * buffers em voo. */
void mc_tx_init(const struct device *device, void (*wake)(void))
{
	uart = device;
	signal_owner = wake;
	done = 0;
	count = 0;
	head = 0;
	error_code = 0;
	last_progress = 0;
	memset(&stats, 0, sizeof(stats));
}

/** Interna: mc_tx_write com lock adquirido. Copia header/payload ou padding antes de publicar
 * count; chamador garante capacidade. */
static void put(const uint8_t *payload, size_t length, uint32_t packet_id, bool padding)
{
	struct tx_slot *slot = &slots[(head + count) % CONFIG_MESHTASTIC_TX_QUEUE_DEPTH];
	slot->offset = 0;
	slot->status = 0;
	slot->packet_id = packet_id;
	if (padding) {
		memset(slot->bytes, 0, sizeof(slot->bytes));
		slot->length = sizeof(slot->bytes);
	} else {
		slot->bytes[0] = 0x94;
		slot->bytes[1] = 0xc3;
		slot->bytes[2] = length >> 8;
		slot->bytes[3] = length & 255;
		memcpy(slot->bytes + 4, payload, length);
		slot->length = length + 4;
	}
	++count;
	add(&stats.enqueued, 1);
}

/** Interna: write_message. Valida, admite lote indivisível sob spinlock, copia frames e habilita
 * IRQ fora do lock por compatibilidade runtime-PM. Nunca espera slot. */
int mc_tx_write(const uint8_t *payload, size_t length, uint32_t packet_id, bool resync)
{
	if (!uart)
		return -ENODEV;
	if (!payload || !length || length > MC_FRAME_MAX)
		return -EINVAL;

	k_spinlock_key_t key = k_spin_lock(&lock);
	size_t needed = resync ? 2 : 1;
	if (error_code || needed > CONFIG_MESHTASTIC_TX_QUEUE_DEPTH - count) {
		int rc = error_code ? error_code : -EAGAIN;
		add(&stats.rejected, 1);
		k_spin_unlock(&lock, key);
		return rc;
	}

	if (count == done)
		last_progress = k_uptime_get();
	if (resync)
		put(NULL, 0, 0, true);
	put(payload, length, packet_id, false);
	k_spin_unlock(&lock, key);

	/* Enable outside the spinlock: driver runtime-PM may need thread context.
	 * An ISR that drained everything before enable only causes one empty IRQ.
	 */
	uart_irq_tx_enable(uart);
	return 0;
}

/** Interna: irq. Um serviço FIFO por chamada, preserva offset/ordem, desliga IRQ sem progresso e
 * sinaliza dono em conclusão/erro; sem callback de aplicação. */
void mc_tx_irq(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	if (error_code || done == count) {
		uart_irq_tx_disable(uart);
		k_spin_unlock(&lock, key);
		return;
	}

	int ready = uart_irq_tx_ready(uart);
	if (!ready) {
		k_spin_unlock(&lock, key);
		return;
	}
	struct tx_slot *slot = &slots[(head + done) % CONFIG_MESHTASTIC_TX_QUEUE_DEPTH];
	size_t remaining = slot->length - slot->offset;
	int accepted =
	    ready < 0 ? ready : uart_fifo_fill(uart, slot->bytes + slot->offset, (int)remaining);
	if (accepted <= 0 || (size_t)accepted > remaining) {
		/* Ready but no progress must not leave a level-triggered IRQ spinning.
		 * Owner will abort outstanding frames and schedule a bounded recovery.
		 */
		error_code = accepted < 0 ? accepted : -EIO;
		uart_irq_tx_disable(uart);
		k_spin_unlock(&lock, key);
		signal_owner();
		return;
	}

	slot->offset += accepted;
	add(&stats.bytes, accepted);
	last_progress = k_uptime_get();
	bool completed = slot->offset == slot->length;
	if (completed) {
		++done;
		add(&stats.drained, 1);
		if (done == count)
			uart_irq_tx_disable(uart);
	}
	k_spin_unlock(&lock, key);
	if (completed)
		signal_owner();
}

/** Interna: reap_tx. Retira uma conclusão sob lock e devolve slot; false não modifica out. out deve
 * ser válido. */
bool mc_tx_take(struct mc_tx_result *out)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	if (!done) {
		k_spin_unlock(&lock, key);
		return false;
	}

	const struct tx_slot *slot = &slots[head];
	*out = (struct mc_tx_result){slot->packet_id, slot->offset, slot->status};
	head = (head + 1) % CONFIG_MESHTASTIC_TX_QUEUE_DEPTH;
	--count;
	--done;
	k_spin_unlock(&lock, key);
	return true;
}

/** Interna: recover_transport. Desliga IRQ, converte slots incompletos em resultados de erro,
 * preserva completados e memória até reap. */
void mc_tx_abort(int status)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	uart_irq_tx_disable(uart);
	while (done < count) {
		struct tx_slot *slot = &slots[(head + done) % CONFIG_MESHTASTIC_TX_QUEUE_DEPTH];
		slot->status = status;
		++done;
		add(&stats.failed, 1);
	}
	error_code = 0;
	k_spin_unlock(&lock, key);
}

/** Interna: mc_poll. Consulta erro/deadline sob lock; now_ms é uptime em ms, não altera fila. */
int mc_tx_error(int64_t now_ms)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	int rc = error_code;
	if (!rc && count > done && now_ms - last_progress >= CONFIG_MESHTASTIC_TX_TIMEOUT_MS) {
		rc = -ETIMEDOUT;
	}
	k_spin_unlock(&lock, key);
	return rc;
}

/** Interna: mc_wait. Retorna prazo absoluto ms sem progresso ou INT64_MAX sem trabalho, sob lock;
 * não agenda timer. */
int64_t mc_tx_deadline(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	int64_t due = count > done ? last_progress + CONFIG_MESHTASTIC_TX_TIMEOUT_MS : INT64_MAX;
	k_spin_unlock(&lock, key);
	return due;
}

/** Interna: mc_init/mc_snapshot_read. Copia estatísticas/ocupação sob lock, sem reap ou I/O; out
 * obrigatório. */
void mc_tx_snapshot(struct mc_tx_stats *out)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	*out = stats;
	out->pending = count;
	k_spin_unlock(&lock, key);
}
