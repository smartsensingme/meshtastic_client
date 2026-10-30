/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>

#include <pb_decode.h>
#include <pb_encode.h>

#include "diagnostics_internal.h"
#include "meshtastic/mesh.pb.h"
#include "tx_internal.h"
#include <meshtastic_client/client.h>
#include <meshtastic_client/frame.h>
#include <rt_log/log.h>

RT_LOG_MODULE_REGISTER(meshtastic_client);

static const char *const LOG_TAG = "MESH";

#define MC_TRACE(...)                                                                              \
	do {                                                                                       \
		if (IS_ENABLED(CONFIG_MESHTASTIC_PACKET_TRACE))                                    \
			RT_LOGD(LOG_TAG, __VA_ARGS__);                                             \
	} while (0)
#if CONFIG_MESHTASTIC_LIVENESS_TIMEOUT_MS <= CONFIG_MESHTASTIC_HEARTBEAT_INTERVAL_MS
#error "Liveness timeout must exceed the heartbeat interval"
#endif
static struct mc_diag_snapshot diagnostic;
static struct mc_diag_snapshot staging;
RING_BUF_DECLARE(rx_ring, 4096);
K_SEM_DEFINE(wake_sem, 0, 1);
static const struct device *radio;
static mc_callback notify;
static struct mc_frame frame;
static meshtastic_FromRadio incoming;
static meshtastic_ToRadio outgoing;
static uint8_t tx[MC_FRAME_MAX];
static atomic_t rx_fault;
static atomic_t rx_bytes;
static atomic_t rx_dropped;
static atomic_t rx_uart_errors;
static uint32_t rx_frames;
static uint32_t rx_decode_errors;
static uint32_t rx_packets;
static uint32_t rx_encrypted;
static uint32_t rx_data;
static uint32_t rx_routes;
static bool ready;
static bool refreshing;
static bool needs_resync;
static bool recovery_pending;
static enum mc_session_state session_state;
static uint32_t nonce;
static uint32_t local_node;
static uint32_t retry_ms;
static int64_t last_rx;
static int64_t last_request;
static int64_t last_byte;
static int64_t heartbeat_due;
static int64_t refresh_due;
static int64_t snapshot_completed_at;
static int64_t next_sync_attempt;

/** Interna: fault, disconnect, reap_tx, mc_refresh, mc_resume, node_event, commit_snapshot e
 * process. Entrega registro emprestado ao callback opcional, sem ISR; não reentrante. */
static void event(struct mc_event *e)
{
	if (notify)
		notify(e);
}

/** Interna: process/mc_poll. Conta erro uint32 com wrap, guarda errno e emite MC_ERROR via event;
 * não desconecta por si só. */
static void fault(int code)
{
	diagnostic.client_errors++;
	diagnostic.last_error = code;
	struct mc_event e = {.type = MC_ERROR, .status = code};
	event(&e);
}

/** Pública e interna (irq/TX): sinaliza semáforo retido, inclusive em ISR, sem tráfego. */
void mc_wake(void)
{
	k_sem_give(&wake_sem);
}

/** Interna: recover_transport/commit_snapshot. Invalida sessão; emite uma perda somente se antes
 * pronta. Mantém cache e último local_node. */
static void disconnect(void)
{
	bool had_session = ready;

	ready = false;
	diagnostic.synced = false;
	if (had_session) {
		struct mc_event e = {.type = MC_DISCONNECTED};
		event(&e);
	}
}

/** Interna: callback do driver UART registrado por mc_init. Drena RX em blocos limitados, registra
 * overflow/erros e serve mc_tx_irq; só acorda dono, nunca callback de aplicação. */
static void irq(const struct device *dev, void *unused)
{
	uint8_t buffer[64];
	(void)unused;

	/* Capture transport faults before draining the bounded RX ring. */
	int errors = uart_err_check(dev);
	bool rx_activity = errors > 0;
	if (errors > 0) {
		atomic_or(&rx_uart_errors, errors);
		atomic_set(&rx_fault, 1);
	}
	uart_irq_update(dev);
	while (uart_irq_rx_ready(dev)) {
		int count = uart_fifo_read(dev, buffer, sizeof(buffer));
		if (count <= 0)
			break;
		rx_activity = true;
		atomic_add(&rx_bytes, count);
		uint32_t stored = ring_buf_put(&rx_ring, buffer, count);
		if (stored != (uint32_t)count) {
			atomic_add(&rx_dropped, count - stored);
			atomic_set(&rx_fault, 1);
		}
	}

	/* TX service and wake are independent of whether RX carried bytes. */
	mc_tx_irq();
	if (rx_activity)
		mc_wake();
}

/** Interna: start_snapshot/mc_send/mc_poll. Codifica outgoing em buffer privado e chama
 * mc_tx_write, que copia; propaga -EMSGSIZE/erro de admissão sem espera. */
static int write_message(uint32_t packet_id, bool resync)
{
	pb_ostream_t stream = pb_ostream_from_buffer(tx, sizeof(tx));
	if (!pb_encode(&stream, meshtastic_ToRadio_fields, &outgoing))
		return -EMSGSIZE;
	return mc_tx_write(tx, stream.bytes_written, packet_id, resync);
}

/** Interna: recover_transport/mc_poll. Consome no máximo depth conclusões; emite MC_TX_DRAINED
 * para ids de aplicação e MC_PAUSED para disconnect intencional, fora da ISR. */
static void reap_tx(void)
{
	for (size_t i = 0; i < CONFIG_MESHTASTIC_TX_QUEUE_DEPTH; ++i) {
		struct mc_tx_result result;
		if (!mc_tx_take(&result))
			break;
		/* Pausa só admite um controle numa fila previamente vazia. Resultado
		 * mede FIFO, não stop bit no fio nem aceitação pelo Meshtastic. */
		if (session_state == MC_SESSION_PAUSING && !result.packet_id) {
			session_state = MC_SESSION_PAUSED;
			struct mc_event e = {.type = MC_PAUSED, .status = result.status};
			event(&e);
		}
		if (result.packet_id) {
			struct mc_event e = {.type = MC_TX_DRAINED,
					     .packet_id = result.packet_id,
					     .status = result.status,
					     .length = result.accepted_bytes};
			event(&e);
		}
	}
}

/** Interna: mc_refresh/mc_poll. Escolhe nonce não reservado, codifica/admite antes de substituir
 * staging; falha preserva nonce/cache/prazos anteriores. */
static int start_snapshot(void)
{
	/* Encode/admit first. Queue refusal must not replace nonce or healthy staging. */
	uint32_t candidate = sys_rand32_get();
	while (!candidate || candidate == 69420 || candidate == 69421)
		++candidate;
	outgoing = (meshtastic_ToRadio)meshtastic_ToRadio_init_zero;
	outgoing.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
	outgoing.want_config_id = candidate;
	int rc = write_message(0, needs_resync);
	if (rc)
		return rc;

	/* Admission succeeded: publish the new epoch and its deadline together. */
	memset(&staging, 0, sizeof(staging));
	staging.initialized = true;
	staging.generation = diagnostic.generation + 1;
	refreshing = true;
	recovery_pending = false;
	needs_resync = false;
	nonce = candidate;
	last_request = k_uptime_get();
	next_sync_attempt = last_request + retry_ms;
	return 0;
}

/** Pública e interna: mc_init/recover_transport/mc_poll. Valida sessão/backoff, inicia staging por
 * start_snapshot e emite MC_REFRESHING apenas se aceito. */
int mc_refresh(void)
{
	if (!radio)
		return -ENODEV;
	if (session_state != MC_SESSION_RUNNING)
		return -EBUSY;
	if (refreshing)
		return -EALREADY;
	if (recovery_pending && k_uptime_get() < next_sync_attempt)
		return -EAGAIN;
	if (!recovery_pending)
		retry_ms = CONFIG_MESHTASTIC_SYNC_TIMEOUT_MS;
	int rc = start_snapshot();
	if (!rc) {
		struct mc_event e = {.type = MC_REFRESHING};
		event(&e);
	}
	return rc;
}

/** Pública, sem chamadores internos. Valida quiescência local, admite disconnect
 * e publica pausa sem erro de sessão; reap_tx entrega conclusão FIFO pelo poll. */
int mc_pause(void)
{
	if (!radio)
		return -ENODEV;
	if (session_state != MC_SESSION_RUNNING)
		return -EALREADY;
	if (!ready)
		return -ENOTCONN;

	struct mc_tx_stats pending;
	mc_tx_snapshot(&pending);
	if (refreshing || pending.pending || frame.state || ring_buf_size_get(&rx_ring) ||
	    atomic_get(&rx_fault))
		return -EBUSY;

	/* Admitir antes de substituir a sessão: recusa não perde estado saudável. */
	outgoing = (meshtastic_ToRadio)meshtastic_ToRadio_init_zero;
	outgoing.which_payload_variant = meshtastic_ToRadio_disconnect_tag;
	outgoing.disconnect = true;
	int rc = write_message(0, false);
	if (rc)
		return rc;

	session_state = MC_SESSION_PAUSING;
	ready = false;
	diagnostic.synced = false;
	recovery_pending = false;
	mc_wake();
	return 0;
}

/** Pública, sem chamadores internos. Reabre por start_snapshot sem wake físico;
 * falha de admissão preserva a pausa e sucesso rearma serviço normal. */
int mc_resume(void)
{
	if (!radio)
		return -ENODEV;
	if (session_state == MC_SESSION_PAUSING)
		return -EBUSY;
	if (session_state != MC_SESSION_PAUSED)
		return -EALREADY;

	retry_ms = CONFIG_MESHTASTIC_SYNC_TIMEOUT_MS;
	int rc = start_snapshot();
	if (rc)
		return rc;

	session_state = MC_SESSION_RUNNING;
	struct mc_event e = {.type = MC_REFRESHING};
	event(&e);
	mc_wake();
	return 0;
}

/** Pública, sem chamadores internos. Consulta estado local sem UART ou relógio. */
enum mc_session_state mc_session_get_state(void)
{
	return radio ? session_state : MC_SESSION_UNINITIALIZED;
}

/** Interna: process/mc_poll. Invalida sessão, aborta/reap TX e planeja ressincronização limitada;
 * sem retransmitir aplicação ou presumir rollback remoto. */
static void recover_transport(int status, bool delayed)
{
	bool was_ready = ready;

	/* Cancelar a época falha preserva conclusões; padding no próximo sync
	 * esgota um parser remoto parcial, sem retransmitir aplicação. */
	disconnect();
	mc_tx_abort(status);
	reap_tx();
	refreshing = false;
	needs_resync = true;
	/* RX/reboot/falha ainda são observados durante pausa, sem reacordar o
	 * periférico por sync automática nem sair da pausa silenciosamente. */
	if (session_state != MC_SESSION_RUNNING) {
		recovery_pending = false;
		return;
	}
	recovery_pending = true;
	if (was_ready)
		retry_ms = CONFIG_MESHTASTIC_SYNC_TIMEOUT_MS;
	next_sync_attempt = k_uptime_get() + (delayed ? retry_ms : 0);
	if (delayed)
		retry_ms = MIN(retry_ms * 2, CONFIG_MESHTASTIC_RETRY_MAX_MS);
	else
		(void)mc_refresh();
}

/** Interna: commit_snapshot/process. Empresta nomes somente na chamada de event, na thread dona. */
static void node_event(uint32_t id, const char *name, const char *short_name)
{
	struct mc_event e = {.type = MC_NODE, .node = id, .name = name, .short_name = short_name};
	event(&e);
}

/** Interna: process. Exige nó local, usuário local, algum canal, firmware e LoRa; não exige todas
 * as seções opcionais nem todo o inventário da rede. */
static bool snapshot_complete(void)
{
	bool own_user = false;
	bool channel_seen = false;
	for (size_t i = 0; i < staging.node_count; i++)
		if (staging.nodes[i].id == staging.local_node && staging.nodes[i].has_user)
			own_user = true;
	for (size_t i = 0; i < 8; i++)
		if (staging.channels[i].received)
			channel_seen = true;
	return staging.local_node && own_user && channel_seen && staging.firmware[0] &&
	       staging.lora_received;
}

/** Interna: process. Publica staging por valor, preserva erros/último RX, rearma prazos e
 * reapresenta metadata/canais/nós antes de MC_SYNCED. */
static void commit_snapshot(void)
{
	bool changed = ready && local_node != staging.local_node;
	if (changed)
		disconnect();

	/* Publish a value copy while retaining session-level error observations. */
	uint32_t errors = diagnostic.client_errors;
	int32_t last_error = diagnostic.last_error;
	int64_t last_receive = diagnostic.last_rx_ms;
	diagnostic = staging;
	diagnostic.client_errors = errors;
	diagnostic.last_error = last_error;
	diagnostic.last_rx_ms = last_receive;
	diagnostic.synced = true;
	local_node = diagnostic.local_node;
	ready = true;
	refreshing = false;
	snapshot_completed_at = k_uptime_get();
	retry_ms = CONFIG_MESHTASTIC_SYNC_TIMEOUT_MS;
	heartbeat_due = k_uptime_get() + CONFIG_MESHTASTIC_HEARTBEAT_INTERVAL_MS;
	refresh_due = k_uptime_get() + CONFIG_MESHTASTIC_REFRESH_INTERVAL_MS;

	/* Replay the committed generation so application policy can rebuild locally. */
	struct mc_event e = {.type = MC_SNAPSHOT_BEGIN};
	event(&e);
	e = (struct mc_event){.type = MC_METADATA, .name = diagnostic.firmware};
	event(&e);
	for (size_t i = 0; i < 8; i++) {
		const struct mc_diag_channel *c = &diagnostic.channels[i];
		if (c->received) {
			e = (struct mc_event){
			    .type = MC_CHANNEL, .channel = i, .status = c->role, .name = c->name};
			event(&e);
		}
	}
	for (size_t i = 0; i < diagnostic.node_count; i++) {
		const struct mc_diag_node *n = &diagnostic.nodes[i];
		if (n->has_user)
			node_event(n->id, n->long_name, n->short_name);
	}
	e = (struct mc_event){.type = MC_SYNCED, .node = local_node};
	event(&e);
}

/** Interna: mc_poll, um frame completo por chamada. Decodifica Protobuf, atualiza cache e despacha
 * eventos; distingue QueueStatus, Routing e reboot. Buffers só valem no callback. */
static void process(void)
{
	pb_istream_t stream = pb_istream_from_buffer(frame.data, frame.length);
	incoming = (meshtastic_FromRadio)meshtastic_FromRadio_init_zero;
	++rx_frames;
	if (!pb_decode(&stream, meshtastic_FromRadio_fields, &incoming)) {
		++rx_decode_errors;
		RT_LOGW(LOG_TAG, "UART_DECODE_FAIL frame_bytes=%u reason=%s",
			(unsigned)frame.length, PB_GET_ERROR(&stream));
		fault(-EBADMSG);
		return;
	}

	/* Only successfully decoded messages extend liveness. */
	last_rx = k_uptime_get();
	diagnostic.last_rx_ms = last_rx;
	mc_diag_observe(refreshing ? &staging : &diagnostic, &incoming, last_rx);

	/* Dispatch transport/protocol observations, never application payload policy. */
	struct mc_event e = {0};
	switch (incoming.which_payload_variant) {
	case meshtastic_FromRadio_config_complete_id_tag:
		if (refreshing && incoming.config_complete_id == nonce) {
			if (snapshot_complete())
				commit_snapshot();
			else {
				RT_LOGW(LOG_TAG,
					"RADIO snapshot incomplete; keeping previous session");
				fault(-ENODATA);
			}
		}
		break;
	case meshtastic_FromRadio_node_info_tag:
		if (!refreshing && incoming.node_info.has_user)
			node_event(incoming.node_info.num, incoming.node_info.user.long_name,
				   incoming.node_info.user.short_name);
		break;
	case meshtastic_FromRadio_channel_tag:
		if (!refreshing) {
			e.type = MC_CHANNEL;
			e.channel = incoming.channel.index;
			e.status = incoming.channel.role;
			e.name =
			    incoming.channel.has_settings ? incoming.channel.settings.name : "";
			event(&e);
		}
		break;
	case meshtastic_FromRadio_metadata_tag:
		if (!refreshing) {
			e.type = MC_METADATA;
			e.name = incoming.metadata.firmware_version;
			event(&e);
		}
		break;
	case meshtastic_FromRadio_queueStatus_tag:
		e.type = MC_QUEUE;
		e.packet_id = incoming.queueStatus.mesh_packet_id;
		e.status = incoming.queueStatus.res;
		e.queue_free = incoming.queueStatus.free;
		e.queue_max = incoming.queueStatus.maxlen;
		event(&e);
		break;
	case meshtastic_FromRadio_rebooted_tag:
		if (incoming.rebooted) {
			recover_transport(-ECONNRESET, false);
		}
		break;
	case meshtastic_FromRadio_packet_tag: {
		const meshtastic_MeshPacket *p = &incoming.packet;
		++rx_packets;
		MC_TRACE("MESH_RX id=%08x from=!%08x to=!%08x channel=%u variant=%u rssi=%d "
			 "via_mqtt=%d pki=%d",
			 p->id, p->from, p->to, p->channel, (unsigned)p->which_payload_variant,
			 p->rx_rssi, p->via_mqtt, p->pki_encrypted);
		if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
			++rx_encrypted;
			MC_TRACE("MESH_SKIP id=%08x reason=not_decoded", p->id);
			break;
		}
		++rx_data;
		MC_TRACE("MESH_DATA id=%08x port=%u bytes=%u request_id=%08x want_response=%d",
			 p->id, p->decoded.portnum, (unsigned)p->decoded.payload.size,
			 p->decoded.request_id, p->decoded.want_response);
		e.pki_encrypted = p->pki_encrypted;
		e.node = p->from;
		e.destination = p->to;
		e.packet_id = p->id;
		e.port = p->decoded.portnum;
		e.channel = p->channel;
		e.payload = p->decoded.payload.bytes;
		e.length = p->decoded.payload.size;
		if (p->decoded.portnum == meshtastic_PortNum_NODEINFO_APP && p->from &&
		    p->from != local_node) {
			meshtastic_User user = meshtastic_User_init_zero;
			pb_istream_t ns = pb_istream_from_buffer(e.payload, e.length);
			if (pb_decode(&ns, meshtastic_User_fields, &user)) {
				/* Existing node metadata is preserved; NODEINFO only replaces the
				 * user. */
				mc_diag_user(&diagnostic, p->from, &user);
				if (refreshing)
					mc_diag_user(&staging, p->from, &user);
				node_event(p->from, user.long_name, user.short_name);
			} else
				fault(-EBADMSG);
		}
		if (p->decoded.portnum == meshtastic_PortNum_ROUTING_APP) {
			++rx_routes;
			meshtastic_Routing routing = meshtastic_Routing_init_zero;
			pb_istream_t rs = pb_istream_from_buffer(e.payload, e.length);
			if (!pb_decode(&rs, meshtastic_Routing_fields, &routing)) {
				fault(-EBADMSG);
				break;
			}
			if (routing.which_variant != meshtastic_Routing_error_reason_tag)
				break;
			MC_TRACE("MESH_ROUTE packet_id=%08x request_id=%08x from=!%08x to=!%08x "
				 "status=%d",
				 p->id, p->decoded.request_id, p->from, p->to,
				 routing.error_reason);
			e.type = MC_ROUTE;
			e.packet_id = p->decoded.request_id;
			e.status = routing.error_reason;
			event(&e);
		} else {
			e.type = MC_RECEIVED;
			event(&e);
		}
		break;
	}
	default:
		break;
	}
}

/** Pública, sem chamadores internos. Valida UART/fila, registra IRQ antes de substituir sessão e
 * inicializa pedido. Ver contrato de rollback/contadores em client.h. */
int mc_init(const struct device *uart, mc_callback callback)
{
	if (!uart || !device_is_ready(uart))
		return -ENODEV;
	if (radio) {
		struct mc_tx_stats pending;
		mc_tx_snapshot(&pending);
		if (pending.pending)
			return -EBUSY;
		uart_irq_rx_disable(radio);
		uart_irq_tx_disable(radio);
	}
	/* Register before replacing the current session. A refused registration
	 * leaves the old session usable, including its RX interrupt. */
	int rc = uart_irq_callback_user_data_set(uart, irq, NULL);
	if (rc) {
		if (radio)
			uart_irq_rx_enable(radio);
		return rc;
	}

	/* Registration accepted: retire the previous local epoch. */
	radio = uart;
	session_state = MC_SESSION_RUNNING;
	notify = callback;
	ready = false;
	refreshing = false;
	local_node = 0;
	needs_resync = false;
	recovery_pending = false;
	retry_ms = CONFIG_MESHTASTIC_SYNC_TIMEOUT_MS;
	uart_irq_rx_disable(radio);
	uart_irq_tx_disable(radio);
	mc_tx_init(radio, mc_wake);
	snapshot_completed_at = 0;
	diagnostic = (struct mc_diag_snapshot){0};
	frame = (struct mc_frame){0};
	ring_buf_reset(&rx_ring);
	atomic_clear(&rx_fault);
	k_sem_reset(&wake_sem);

	/* Start reception and admit the initial request, without waiting for radio. */
	uart_irq_rx_enable(radio);
	last_rx = k_uptime_get();
	rc = mc_refresh();
	if (rc) {
		uart_irq_rx_disable(radio);
		uart_irq_tx_disable(radio);
		radio = NULL;
		notify = NULL;
		refreshing = false;
	}
	return rc;
}

/** Pública, sem chamadores internos. Fases: falhas/conclusões TX; erro/parser RX; mensagens;
 * liveness, recuperação, refresh e heartbeat. Antes de init não faz I/O. */
void mc_poll(void)
{
	if (!radio)
		return;

	/* Reap/repair TX before consuming RX for this owner iteration. */
	int64_t now = k_uptime_get();
	int tx_failure = mc_tx_error(now);
	if (tx_failure) {
		recover_transport(tx_failure, true);
		fault(tx_failure);
	}
	reap_tx();

	/* Faults invalidate ring contents; a partial parser has a separate deadline. */
	if (atomic_set(&rx_fault, 0)) {
		RT_LOGW(LOG_TAG,
			"UART_RX_FAULT uart_error_mask=%u dropped_bytes=%u buffered_bytes=%u",
			(unsigned)atomic_get(&rx_uart_errors), (unsigned)atomic_get(&rx_dropped),
			ring_buf_size_get(&rx_ring));
		uart_irq_rx_disable(radio);
		ring_buf_reset(&rx_ring);
		frame = (struct mc_frame){0};
		uart_irq_rx_enable(radio);
		recover_transport(-EOVERFLOW, false);
		fault(-EOVERFLOW);
	}
	if (frame.state && now - last_byte >= 2000) {
		frame = (struct mc_frame){0};
		fault(-ETIMEDOUT);
	}

	/* Decode in the owner, keeping callbacks and nanopb out of the ISR. */
	uint8_t byte;
	while (ring_buf_get(&rx_ring, &byte, 1)) {
		last_byte = k_uptime_get();
		int rc = mc_frame_byte(&frame, byte);
		if (rc == 1)
			process();
		else if (rc < 0)
			fault(-EMSGSIZE);
	}
	/* Maintenance uses existing deadlines, no periodic polling worker. */
	if (session_state != MC_SESSION_RUNNING)
		return;
	now = k_uptime_get();
	if (ready && now - last_rx >= CONFIG_MESHTASTIC_LIVENESS_TIMEOUT_MS) {
		recover_transport(-ENOTCONN, false);
	} else if (recovery_pending && now >= next_sync_attempt) {
		int rc = mc_refresh();
		if (rc)
			next_sync_attempt = now + retry_ms;
	} else if (refreshing && now >= next_sync_attempt) {
		fault(-ETIMEDOUT);
		uint32_t previous = retry_ms;
		retry_ms = MIN(retry_ms * 2, CONFIG_MESHTASTIC_RETRY_MAX_MS);
		if (start_snapshot()) {
			retry_ms = previous;
			next_sync_attempt = now + retry_ms;
		}
	} else if (ready && !refreshing && (CONFIG_MESHTASTIC_REFRESH_INTERVAL_MS > 0) &&
		   now >= refresh_due) {
		if (mc_refresh())
			refresh_due = now + CONFIG_MESHTASTIC_SYNC_TIMEOUT_MS;
	} else if (ready && !refreshing && now >= heartbeat_due) {
		outgoing = (meshtastic_ToRadio)meshtastic_ToRadio_init_zero;
		outgoing.which_payload_variant = meshtastic_ToRadio_heartbeat_tag;
		outgoing.heartbeat.nonce = 0;
		int rc = write_message(0, false);
		heartbeat_due = now + (rc ? CONFIG_MESHTASTIC_SYNC_TIMEOUT_MS
					  : CONFIG_MESHTASTIC_HEARTBEAT_INTERVAL_MS);
	}
}

/** Pública, sem chamadores internos. Combina prazo externo e deadlines internos e dorme em
 * semáforo; não executa protocolo nem polling periódico. */
void mc_wait(int32_t maximum_ms)
{
	if (maximum_ms <= 0)
		return;
	int64_t now = k_uptime_get();
	int64_t due = now + maximum_ms;
	if (frame.state)
		due = MIN(due, last_byte + 2000);
	due = MIN(due, mc_tx_deadline());
	if (refreshing || recovery_pending)
		due = MIN(due, next_sync_attempt);
	if (ready) {
		due = MIN(due, last_rx + CONFIG_MESHTASTIC_LIVENESS_TIMEOUT_MS);
		if (!refreshing) {
			due = MIN(due, heartbeat_due);
			if (CONFIG_MESHTASTIC_REFRESH_INTERVAL_MS)
				due = MIN(due, refresh_due);
		}
	}
	k_sem_take(&wake_sem, K_MSEC(MAX(0, due - now)));
}

/** Pública, sem chamadores internos. Consulta local sem I/O; false não invalida necessariamente os
 * dados históricos do cache. */
bool mc_ready(void)
{
	return ready;
}

/** Pública, sem chamadores internos. Último id publicado, não prova sessão atual; consultar
 * mc_ready. */
uint32_t mc_local_node(void)
{
	return local_node;
}

/** Pública, sem chamadores internos. Valida, monta pacote e copia/enfileira via write_message;
 * publica packet_id só após aceitação. Sem espera, ACK tracking ou retransmissão. */
int mc_send(uint32_t destination, uint8_t channel, uint32_t port, const uint8_t *payload,
	    size_t length, uint32_t *packet_id)
{
	if (!ready)
		return -ENOTCONN;
	if (!destination || destination == UINT32_MAX || destination == local_node || channel > 7 ||
	    !payload || !length || length > MC_PAYLOAD_MAX || !packet_id)
		return -EINVAL;

	/* Prepare the packet in private storage; enqueue performs its own copy. */
	outgoing = (meshtastic_ToRadio)meshtastic_ToRadio_init_zero;
	outgoing.which_payload_variant = meshtastic_ToRadio_packet_tag;
	meshtastic_MeshPacket *p = &outgoing.packet;
	p->to = destination;
	p->channel = channel;
	p->hop_limit = CONFIG_MESHTASTIC_HOP_LIMIT;
	p->want_ack = IS_ENABLED(CONFIG_MESHTASTIC_WANT_ACK);
	p->id = sys_rand32_get();
	if (!p->id)
		p->id = 1;
	p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
	p->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
	p->decoded.portnum = port;
	p->decoded.payload.size = length;
	memcpy(p->decoded.payload.bytes, payload, length);
	uint32_t candidate_id = p->id;
	int rc = write_message(candidate_id, false);
	if (rc)
		return rc;

	/* Acceptance is local only; completion and routing are later events. */
	*packet_id = candidate_id;
	MC_TRACE(
	    "MESH_TX id=%08x from_local=!%08x to=!%08x channel=%u port=%u bytes=%u want_ack=%d",
	    p->id, local_node, p->to, p->channel, p->decoded.portnum, (unsigned)length,
	    p->want_ack);
	return 0;
}

/** Pública, sem chamadores internos. Copia cache e contadores; TX sob lock, RX amostrado
 * individualmente. Sem ponteiros, UART ou apresentação. */
int mc_snapshot_read(struct mc_snapshot *snapshot)
{
	if (!snapshot)
		return -EINVAL;

	/* Cache/owner counters first; ISR values are independent observations. */
	snapshot->maintenance_paused = radio && session_state != MC_SESSION_RUNNING;
	snapshot->radio = diagnostic;
	snapshot->uart = (struct mc_uart_snapshot){
	    .bytes = (uint32_t)atomic_get(&rx_bytes),
	    .frames = rx_frames,
	    .decode_errors = rx_decode_errors,
	    .packets = rx_packets,
	    .decoded = rx_data,
	    .not_decoded = rx_encrypted,
	    .routing = rx_routes,
	    .dropped_bytes = (uint32_t)atomic_get(&rx_dropped),
	    .uart_error_mask = (uint32_t)atomic_get(&rx_uart_errors),
	};

	/* TX snapshot is coherent under its own lock, without changing ownership. */
	struct mc_tx_stats tx_stats;
	mc_tx_snapshot(&tx_stats);
	snapshot->uart.tx_enqueued = tx_stats.enqueued;
	snapshot->uart.tx_drained = tx_stats.drained;
	snapshot->uart.tx_failed = tx_stats.failed;
	snapshot->uart.tx_rejected = tx_stats.rejected;
	snapshot->uart.tx_bytes = tx_stats.bytes;
	snapshot->uart.tx_pending = tx_stats.pending;
	snapshot->refreshing = refreshing;
	snapshot->captured_at_ms = k_uptime_get();
	snapshot->snapshot_completed_at_ms = snapshot_completed_at;
	return 0;
}
