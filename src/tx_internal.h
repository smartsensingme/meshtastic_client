/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MC_TX_INTERNAL_H
#define MC_TX_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

/** Resultado interno copiado por valor; slot só é liberado ao reap pelo dono. */
struct mc_tx_result {
	uint32_t packet_id;    /**< Id de aplicação; zero para controle/padding. */
	size_t accepted_bytes; /**< Bytes aceitos pela FIFO, com header; não wire idle. */
	int status;	       /**< Zero=FIFO completa ou errno negativo. */
};

/** Estatísticas amostradas juntas sob lock; cumulativas/saturantes desde init. */
struct mc_tx_stats {
	uint32_t enqueued; /**< Slots admitidos, inclusive controle/padding. */
	uint32_t drained;  /**< Slots completamente aceitos pela FIFO. */
	uint32_t failed;   /**< Slots incompletos abortados. */
	uint32_t rejected; /**< Admissões recusadas por capacidade/erro latente. */
	uint32_t bytes;	   /**< Bytes aceitos pela FIFO, não entrega ao rádio. */
	uint32_t pending;  /**< Slots ocupados incluindo conclusões não reapadas. */
};

/** Interna, chamada por mc_init com IRQ quiescente e nenhum slot pendente.
 * @param device UART emprestada, exclusiva, válida até próximo init.
 * @param wake Notificação emprestada obrigatória, ISR-safe; cliente usa mc_wake.
 * @return Sem valor; zera fila/contadores sem cancelar hardware em voo.
 */
void mc_tx_init(const struct device *device, void (*wake)(void));

/** Interna, chamada por write_message, uma thread produtora + ISR consumidora.
 * @param payload Protobuf emprestado não NULL; copiado antes de habilitar IRQ.
 * @param length Bytes 1..512, sem framing; header é construído na fila.
 * @param packet_id Id da aplicação ou zero para frame interno.
 * @param resync Admitir padding de 516 zeros e pedido como lote indivisível.
 * @return 0; -ENODEV sem init, -EINVAL argumentos, -EAGAIN lotado ou erro latente.
 * Recusa não publica slot parcial. Copia sob spinlock; nunca espera espaço.
 */
int mc_tx_write(const uint8_t *payload, size_t length, uint32_t packet_id, bool resync);

/** Interna, chamada por irq após uart_irq_update, somente ISR. Um serviço FIFO
 * limitado por chamada. Offset/fila protegidos por lock; wake em conclusão/erro.
 * @return Sem valor. Sem callback de aplicação ou liberação de slot na ISR.
 */
void mc_tx_irq(void);

/** Interna, chamada por reap_tx na thread dona; out obrigatório não NULL.
 * @param out Cópia própria da conclusão, intocada quando não há resultado.
 * @return true retirou/liberou um slot; false fila de conclusões vazia.
 */
bool mc_tx_take(struct mc_tx_result *out);

/** Interna, chamada por recover_transport na thread dona. Desliga TX IRQ,
 * cancela incompletos e preserva resultados até reap, inclusive conclusões antigas.
 * @param status Errno negativo aplicado aos slots incompletos; não rollback remoto.
 * @return Sem valor; não emite evento da aplicação nem retransmite pacotes.
 */
void mc_tx_abort(int status);

/** Interna, chamada por mc_poll; leitura sem mutação sob lock.
 * @param now_ms Uptime monotônico atual em ms, compatível com k_uptime_get.
 * @return 0, erro FIFO latente ou -ETIMEDOUT por falta de progresso.
 */
int mc_tx_error(int64_t now_ms);

/** Interna, chamada por mc_wait, sob lock.
 * @return Próximo timeout absoluto ms ou INT64_MAX sem transmissão pendente.
 */
int64_t mc_tx_deadline(void);

/** Interna, chamada por mc_init/mc_snapshot_read. Copia contadores sob lock.
 * @param out Destino não NULL, por valor, sem alias com estatísticas privadas.
 * @return Sem valor; não modifica fila ou ownership dos frames.
 */
void mc_tx_snapshot(struct mc_tx_stats *out);

#endif
