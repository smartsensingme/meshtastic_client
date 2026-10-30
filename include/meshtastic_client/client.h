/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * Cliente singleton da API serial Meshtastic. Todas as APIs, exceto mc_wake,
 * pertencem a uma única thread dona. Não são reentrantes: callbacks não devem
 * chamar init/poll/refresh/send; podem copiar dados e chamar mc_wake.
 * Sem malloc, espera por slot ou callbacks de aplicação na ISR.
 */
#ifndef MESHTASTIC_CLIENT_H
#define MESHTASTIC_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Limite de payload de aplicação em bytes, não inclui framing/Protobuf. */
#define MC_PAYLOAD_MAX 200

/** Porta PRIVATE_APP oficial; outras portas podem ser passadas a mc_send. */
#define MC_PRIVATE_PORT 256

/** Eventos síncronos entregues pela thread dona; campos não aplicáveis são zero. */
enum mc_event_type {
	MC_SYNCED,	   /**< Snapshot completo publicado; node é o identificador local. */
	MC_DISCONNECTED,   /**< Sessão antes saudável perdida; entrega em voo incerta. */
	MC_NODE,	   /**< node/name/short_name observados; nomes não autenticam o nó. */
	MC_QUEUE,	   /**< QueueStatus: packet_id/status/queue_free/queue_max. */
	MC_ROUTE,	   /**< Routing: packet_id=request_id; status=enum Error, zero=ACK. */
	MC_RECEIVED,	   /**< Pacote decodificado pelo rádio; metadados e payload válidos. */
	MC_ERROR,	   /**< status é errno negativo; não implica sempre perda da sessão. */
	MC_CHANNEL,	   /**< channel=0..7, status=enum Channel.Role e name. */
	MC_METADATA,	   /**< name contém a versão do firmware informada pelo rádio. */
	MC_REFRESHING,	   /**< Pedido aceito; sessão/cache anteriores continuam utilizáveis. */
	MC_SNAPSHOT_BEGIN, /**< Inicia replay metadata/canais/nós, seguido de SYNCED. */
	MC_TX_DRAINED,	   /**< packet_id/status/length: FIFO aceitou tudo (0) ou erro (<0).
			    * Não prova wire idle, recepção UART, rádio ou entrega remota. */
};

/** Registro emprestado somente durante o callback; copiar antes de armazenar. */
struct mc_event {
	enum mc_event_type type; /**< Discriminante dos campos válidos. */
	uint32_t node;		 /**< Origem MeshPacket ou nó anunciado, conforme evento. */
	uint32_t packet_id;	 /**< Id do envio; MC_ROUTE usa request_id, não id do ACK. */
	uint32_t port;		 /**< PortNum numérico no pacote recebido. */
	uint32_t channel;	 /**< Índice informado pelo rádio; em PKI não identifica PSK. */
	uint32_t destination;	 /**< MeshPacket.to no recebimento/roteamento. */
	bool pki_encrypted;	 /**< Rádio informa decodificação PKI; não contém chave. */
	uint32_t queue_free;	 /**< Slots livres reportados em MC_QUEUE. */
	uint32_t queue_max;	 /**< Capacidade reportada em MC_QUEUE. */
	int status;		/**< Errno, resultado QueueStatus, Routing.Error ou Channel.Role. */
	const char *name;	/**< String terminada em NUL emprestada, ou NULL. */
	const char *short_name; /**< Nome curto emprestado de MC_NODE, ou NULL. */
	const uint8_t *payload; /**< Bytes emprestados em RX/ROUTE, ou NULL. */
	size_t length;		/**< Bytes do payload em RX/ROUTE; bytes FIFO com header em TX. */
};

/** Callback externo opcional. Chamado internamente por event, na thread dona.
 * Não bloquear: a fila RX continua limitada. Copiar dados para outro consumidor.
 */
typedef void (*mc_callback)(const struct mc_event *event);

/** Inicializar/reinicializar externamente, na thread dona, com UART quiescente.
 * uart deve existir durante toda a sessão, pronta, exclusiva e interrupt-driven;
 * callback pode ser NULL. Substitui o callback IRQ da UART: não compartilhar.
 * Retorna 0 após enfileirar pedido inicial (não após sincronizar), -ENODEV para
 * NULL/dispositivo indisponível, -EBUSY com slots TX inclusive conclusões ainda
 * não reapadas, ou erro do driver/codificação/fila. Erros de validação/registro
 * preservam sessão anterior; erro de envio inicial deixa cliente sem sessão.
 * Reinicialização aceita limpa sessão/cache/TX, preserva contadores RX por boot.
 * Pode emitir MC_REFRESHING antes de retornar. Sem init implícito nas outras APIs.
 */
int mc_init(const struct device *uart, mc_callback callback);

/** Serviço externo na thread dona: reap TX, RX e prazos; emite callbacks.
 * Antes de init é no-op. Drenagem RX depende da carga; não promete WCET fixo.
 * Chamar antes de aguardar; não chamar de dentro de callback nem de ISR.
 */
void mc_poll(void);

/** Sinal externo/interno (irq/TX) seguro em ISR e antes de init; semáforo retém
 * um wake. Não consulta rádio nem altera estado. Init aceito limpa wake anterior.
 */
void mc_wake(void);

/** Espera externa na thread dona, no máximo maximum_ms milissegundos.
 * <=0 retorna imediatamente. Inclui prazos internos, mas não executa poll.
 * Antes de init ainda espera wake/prazo externo. Não usar em ISR/callback.
 */
void mc_wait(int32_t maximum_ms);

/** Pedido externo ou interno (mc_init/recover_transport/mc_poll) de snapshot.
 * Preserva sessão saudável. Retorna 0=aceito, -ENODEV antes de init, -EALREADY
 * se atualizando, -EAGAIN por backoff/fila cheia ou erro de transporte/encoding.
 * Emissão síncrona de MC_REFRESHING apenas se aceito; não aguarda resposta.
 */
int mc_refresh(void);

/** Consulta externa, mesma thread: sessão sincronizada e ainda não invalidada.
 * false antes de init; true não garante identidade/canal aceitos pela aplicação.
 */
bool mc_ready(void);

/** Consulta externa, mesma thread: último node local publicado, zero antes de
 * init/primeira sincronização. Pode manter valor antigo após desconexão: usar ready.
 */
uint32_t mc_local_node(void);

/** Envio externo unicast na thread dona. Copia payload antes de retornar.
 * destination não pode ser zero, broadcast UINT32_MAX ou nó local; channel 0..7;
 * port é PortNum numérico, validado pelo rádio; payload não NULL, length 1..200;
 * packet_id é saída obrigatória, alterada somente em sucesso. Não retém ponteiros.
 * Retorna -ENOTCONN sem sessão (inclusive antes de init), -EINVAL por argumentos,
 * -EAGAIN por fila cheia, -EMSGSIZE por encoding, ou falha TX. 0 significa apenas
 * aceitação local. MC_TX_DRAINED vem de mc_poll, nunca desta função.
 * ACK/hop são opções de pacote do Kconfig; não há retransmissão da aplicação.
 */
int mc_send(uint32_t destination, uint8_t channel, uint32_t port, const uint8_t *payload,
	    size_t length, uint32_t *packet_id);

#ifdef __cplusplus
}
#endif

#endif
