/* SPDX-License-Identifier: Apache-2.0 */
/* Standalone lifecycle regression: no sensor, concentrator, support or USB source. */
#include "../src/client.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int64_t test_now = 1000, test_waited;
void (*test_on_wait)(void);
struct device test_usb = {true}, test_radio = {true};
static void (*uart_callbacks[2])(const struct device *, void *);
static unsigned char input[8192], output[8192];
static size_t in_used, in_pos, out_used;
static uint32_t random_value = 100;

static int hw_error, registration_error;
static bool tx_irq_enabled;
static bool tx_auto_pump = true;

static int tx_fifo_chunk = 7;
static int tx_fill_error;
int uart_irq_tx_ready(const struct device *d)
{
	(void)d;
	return tx_irq_enabled ? tx_fifo_chunk : 0;
}
int uart_fifo_fill(const struct device *d, const unsigned char *b, int n)
{
	assert(d == &test_radio);
	if (tx_fill_error) {
		int rc = tx_fill_error;
		tx_fill_error = 0;
		return rc;
	}
	int take = MIN(n, tx_fifo_chunk);
	assert(out_used + (size_t)take <= sizeof(output));
	memcpy(output + out_used, b, take);
	out_used += take;
	return take;
}
void uart_irq_tx_disable(const struct device *d)
{
	(void)d;
	tx_irq_enabled = false;
}
void uart_irq_tx_enable(const struct device *d)
{
	assert(d == &test_radio);
	tx_irq_enabled = true;
	/* Immediate interrupts exercise publication-before-enable, without wire time. */
	unsigned limit = 0;
	while (tx_auto_pump && tx_irq_enabled) {
		assert(++limit < 1000);
		uart_callbacks[0](d, 0);
	}
}
int test_printk(const char *fmt, ...)
{
	(void)fmt;
	return 0;
}
uint32_t sys_rand32_get(void)
{
	return random_value++;
}
int uart_err_check(const struct device *d)
{
	(void)d;
	int e = hw_error;
	hw_error = 0;
	return e;
}
int uart_irq_update(const struct device *d)
{
	(void)d;
	return 1;
}
int uart_irq_rx_ready(const struct device *d)
{
	(void)d;
	return in_pos < in_used;
}
int uart_fifo_read(const struct device *d, unsigned char *b, int n)
{
	(void)d;
	int i = 0;
	while (i < n && in_pos < in_used)
		b[i++] = input[in_pos++];
	return i;
}
int uart_irq_callback_user_data_set(const struct device *d,
				    void (*fn)(const struct device *, void *), void *u)
{
	(void)u;
	if (registration_error)
		return registration_error;
	uart_callbacks[d == &test_usb] = fn;
	return 0;
}
void uart_irq_rx_enable(const struct device *d)
{
	(void)d;
}
void uart_irq_rx_disable(const struct device *d)
{
	(void)d;
}

static unsigned events;
static unsigned pause_events, lost_events, received_events;
static int pause_status;
static unsigned extension_events;
static uint8_t extension_payload[160];
static void callback(const struct mc_event *e)
{
    if (e->type == MC_LOCAL_EXTENSION) {
        assert(e->length <= sizeof(extension_payload));
        memcpy(extension_payload, e->payload, e->length);
        ++extension_events;
    }
	++events;
	if (e->type == MC_PAUSED) {
		++pause_events;
		pause_status = e->status;
	}
	if (e->type == MC_DISCONNECTED)
		++lost_events;
	if (e->type == MC_RECEIVED)
		++received_events;
}

/** Fixture interna: entrega um FromRadio oficial ao callback IRQ e ao poll real. */
static void feed(meshtastic_FromRadio *message)
{
	uint8_t bytes[MC_FRAME_MAX + 4];
	pb_ostream_t stream = pb_ostream_from_buffer(bytes + 4, MC_FRAME_MAX);
	assert(pb_encode(&stream, meshtastic_FromRadio_fields, message));
	bytes[0] = 0x94;
	bytes[1] = 0xc3;
	bytes[2] = stream.bytes_written >> 8;
	bytes[3] = stream.bytes_written;
	memcpy(input, bytes, stream.bytes_written + 4);
	in_pos = 0;
	in_used = stream.bytes_written + 4;
	uart_callbacks[0](&test_radio, NULL);
	mc_poll();
}

/** Fixture interna: configuração mínima de um nó arbitrário, sem identidade da aplicação. */
static void sync_node(uint32_t node)
{
	meshtastic_FromRadio message = meshtastic_FromRadio_init_zero;
	message.which_payload_variant = meshtastic_FromRadio_my_info_tag;
	message.my_info.my_node_num = node;
	feed(&message);

	message = (meshtastic_FromRadio)meshtastic_FromRadio_init_zero;
	message.which_payload_variant = meshtastic_FromRadio_metadata_tag;
	strcpy(message.metadata.firmware_version, "fixture");
	feed(&message);

	message = (meshtastic_FromRadio)meshtastic_FromRadio_init_zero;
	message.which_payload_variant = meshtastic_FromRadio_config_tag;
	message.config.which_payload_variant = meshtastic_Config_lora_tag;
	feed(&message);

	message = (meshtastic_FromRadio)meshtastic_FromRadio_init_zero;
	message.which_payload_variant = meshtastic_FromRadio_channel_tag;
	message.channel.index = 0;
	feed(&message);

	message = (meshtastic_FromRadio)meshtastic_FromRadio_init_zero;
	message.which_payload_variant = meshtastic_FromRadio_node_info_tag;
	message.node_info.num = node;
	message.node_info.has_user = true;
	strcpy(message.node_info.user.long_name, "arbitrary-node");
	feed(&message);

	message = (meshtastic_FromRadio)meshtastic_FromRadio_init_zero;
	message.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
	message.config_complete_id = nonce + 1;
	feed(&message);
	assert(refreshing);
	message.config_complete_id = nonce;
	feed(&message);
	assert(mc_ready() && mc_local_node() == node);
}

int main(void)
{
	struct mc_snapshot before, after;
	uint32_t id = 123;
	uint8_t payload = 42;
	struct device unavailable = {false};

	/* No transport access, implicit init or callback before a successful init. */
	mc_poll();
	assert(!mc_ready() && !mc_local_node());
	assert(mc_refresh() == -ENODEV);
	assert(mc_pause() == -ENODEV && mc_resume() == -ENODEV);
	assert(mc_session_get_state() == MC_SESSION_UNINITIALIZED);
	assert(mc_send(2, 0, 256, &payload, 1, &id) == -ENOTCONN && id == 123);
	assert(mc_snapshot_read(NULL) == -EINVAL);
	assert(!mc_snapshot_read(&before) && !before.radio.initialized);
	mc_wake();
	mc_wait(1000);
	assert(test_waited == 0);
	mc_wait(0);
	assert(mc_init(NULL, callback) == -ENODEV);
	assert(mc_init(&unavailable, callback) == -ENODEV);
	registration_error = -ENOTSUP;
	assert(mc_init(&test_radio, callback) == -ENOTSUP);
	mc_poll();
	assert(mc_refresh() == -ENODEV && !radio && !events && !out_used);

	/* Retry after failed init succeeds; pending completions forbid reinit. */
	registration_error = 0;
	assert(!mc_init(&test_radio, callback));
	assert(events == 1 && refreshing && !mc_ready());
	assert(mc_init(&test_radio, callback) == -EBUSY);
	mc_poll();
	assert(!mc_snapshot_read(&before));
	assert(before.uart.tx_enqueued == 1 && !before.uart.tx_pending);

	/* Failed reinit leaves session, callback and counters intact. */
	uint32_t saved_nonce = nonce;
	registration_error = -EIO;
	assert(mc_init(&test_radio, NULL) == -EIO);
	assert(radio == &test_radio && notify == callback && nonce == saved_nonce);
	assert(!mc_snapshot_read(&after));
	assert(after.refreshing && after.uart.tx_enqueued == before.uart.tx_enqueued);

	/* RX observations are boot-cumulative; TX/session observations reset on init. */
	atomic_set(&rx_bytes, 99);
	rx_frames = 7;
	diagnostic.client_errors = 3;
	registration_error = 0;
	out_used = 0;
	assert(!mc_init(&test_radio, NULL));
	mc_poll();
	assert(!mc_snapshot_read(&after));
	assert(after.uart.bytes == 99 && after.uart.frames == 7);
	assert(after.uart.tx_enqueued == 1 && after.radio.client_errors == 0);
	assert(!after.radio.initialized && !after.radio.synced && !after.radio.generation);
	assert(!after.snapshot_completed_at_ms && after.refreshing);

	/* Arbitrary node and port: readiness never depends on sensor names/roles. */
	sync_node(0xabcdef);
	assert(!mc_send(0x123456, 0, 42, &payload, 1, &id));
	mc_poll();
	assert(!mc_snapshot_read(&before));
	out_used = 0;
	assert(!mc_refresh() && mc_ready());
	assert(!mc_snapshot_read(&after) && after.refreshing);
	assert(after.radio.generation == before.radio.generation);
	sync_node(0xabcdef);
	assert(!mc_snapshot_read(&after));
	assert(after.radio.generation == before.radio.generation + 1);

	/* Reboot while UART TX is still pending explicitly loses the session. */
	tx_auto_pump = false;
	assert(!mc_send(0x123456, 0, 42, &payload, 1, &id));
	meshtastic_FromRadio message = meshtastic_FromRadio_init_zero;
	message.which_payload_variant = meshtastic_FromRadio_rebooted_tag;
	message.rebooted = true;
	feed(&message);
	assert(!mc_ready());
	assert(!mc_snapshot_read(&after) && after.uart.tx_failed >= 1);
	tx_auto_pump = true;
	while (tx_irq_enabled) {
		uart_callbacks[0](&test_radio, NULL);
	}
	mc_poll();
	sync_node(0xabcdef);

	/* No UART response: sleep to deadline, invalidate, then recover with backoff. */
	test_now = last_rx + CONFIG_MESHTASTIC_LIVENESS_TIMEOUT_MS;
	out_used = 0;
	mc_poll();
	assert(!mc_ready());
	sync_node(0xabcdef);

	/* Intentional pause releases the official API without masking TX/ACK policy. */
	mc_poll();
	notify = callback;
	assert(mc_resume() == -EALREADY);
	assert(!mc_refresh());
	assert(mc_pause() == -EBUSY);
	sync_node(0xabcdef);
	assert(!mc_send(0x123456, 0, 42, &payload, 1, &id));
	assert(mc_pause() == -EBUSY); /* FIFO completion must first be reaped. */
	mc_poll();
	unsigned lost_before = lost_events;
	/* A partially received frame prevents closing a healthy session. */
	input[0] = 0x94;
	in_pos = 0;
	in_used = 1;
	uart_callbacks[0](&test_radio, NULL);
	assert(mc_pause() == -EBUSY);
	mc_poll();
	assert(mc_pause() == -EBUSY && mc_ready());
	test_now += 2000;
	mc_poll();
	uint32_t generation_before = diagnostic.generation;
	out_used = 0;
	tx_auto_pump = false;
	assert(!mc_pause());
	assert(mc_session_get_state() == MC_SESSION_PAUSING && !mc_ready());
	assert(mc_pause() == -EALREADY && mc_resume() == -EBUSY);
	assert(mc_refresh() == -EBUSY);
	assert(mc_send(0x123456, 0, 42, &payload, 1, &id) == -ENOTCONN);
	assert(!mc_snapshot_read(&after) && after.maintenance_paused && !after.radio.synced);
	assert(!pause_events && lost_events == lost_before);
	while (tx_irq_enabled)
		uart_callbacks[0](&test_radio, NULL);
	assert(!pause_events); /* No callback from ISR. */
	mc_poll();
	assert(mc_session_get_state() == MC_SESSION_PAUSED && pause_events == 1);
	assert(!pause_status && lost_events == lost_before);
	meshtastic_ToRadio control = meshtastic_ToRadio_init_zero;
	assert(out_used >= 4 && output[0] == 0x94 && output[1] == 0xc3);
	pb_istream_t wire = pb_istream_from_buffer(output + 4, out_used - 4);
	assert(pb_decode(&wire, meshtastic_ToRadio_fields, &control));
	assert(control.which_payload_variant == meshtastic_ToRadio_disconnect_tag &&
	       control.disconnect);

	/* Fifteen minutes of silence has no keepalive, reconnect or obsolete wait deadline. */
	out_used = 0;
	test_now += 900000;
	mc_poll();
	k_sem_reset(&wake_sem);
	mc_wait(900000);
	assert(test_waited == 900000 && !out_used && !refreshing && !recovery_pending);
	assert(diagnostic.generation == generation_before && mc_local_node() == 0xabcdef);
	message = (meshtastic_FromRadio)meshtastic_FromRadio_init_zero;
	message.which_payload_variant = meshtastic_FromRadio_packet_tag;
	message.packet.from = 0x123456;
	message.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
	message.packet.decoded.portnum = 42;
	unsigned received_before = received_events;
	feed(&message);
	assert(received_events == received_before + 1 && !out_used);
	message.which_payload_variant = meshtastic_FromRadio_rebooted_tag;
	message.rebooted = true;
	feed(&message);
	assert(!out_used && mc_session_get_state() == MC_SESSION_PAUSED);
	assert(!recovery_pending && !refreshing);
	hw_error = 1;
	uart_callbacks[0](&test_radio, NULL);
	mc_poll();
	assert(!out_used && mc_session_get_state() == MC_SESSION_PAUSED);

	/* Caller performs physical wake; resume itself sends only official synchronization. */
	tx_auto_pump = true;
	assert(!mc_resume() && !mc_ready());
	assert(mc_session_get_state() == MC_SESSION_RUNNING && refreshing);
	assert(!mc_snapshot_read(&after) && !after.maintenance_paused);
	mc_poll();
	sync_node(0xabcdef);
	assert(diagnostic.generation == generation_before + 1);

	/* Failed disconnect remains paused and reports one failure; no automatic UART traffic. */
	mc_poll();
	out_used = 0;
	tx_auto_pump = false;
	assert(!mc_pause());
	test_now += CONFIG_MESHTASTIC_TX_TIMEOUT_MS;
	mc_poll();
	assert(pause_events == 2 && pause_status == -ETIMEDOUT);
	assert(mc_session_get_state() == MC_SESSION_PAUSED && !recovery_pending);
	test_now += 900000;
	mc_poll();
	assert(!out_used && pause_events == 2);
	tx_auto_pump = true;
	assert(!mc_resume());
	mc_poll();
	sync_node(0xabcdef);

#if CONFIG_MESHTASTIC_LOCAL_EXTENSION
    uint8_t local_bytes[] = {'R','B','V','1',1,2,3,4};
    unsigned before_rx = received_events;
    uint8_t extension_frame[200];
    pb_ostream_t local_stream = pb_ostream_from_buffer(extension_frame + 4, sizeof(extension_frame) - 4);
    assert(pb_encode_tag(&local_stream, PB_WT_STRING, 99));
    assert(pb_encode_string(&local_stream, local_bytes, sizeof(local_bytes)));
    extension_frame[0]=0x94; extension_frame[1]=0xc3;
    extension_frame[2]=local_stream.bytes_written >> 8; extension_frame[3]=local_stream.bytes_written;
    memcpy(input,extension_frame,local_stream.bytes_written+4);
    in_pos=0; in_used=local_stream.bytes_written+4;
    uart_callbacks[0](&test_radio,NULL); mc_poll();
    assert(extension_events==1 && !memcmp(extension_payload,local_bytes,sizeof(local_bytes)));
    assert(received_events==before_rx); /* Local extension is never an RF event. */
    out_used=0;
    assert(mc_local_extension_send(NULL,1)==-EINVAL);
    assert(mc_local_extension_send(local_bytes,161)==-EINVAL);
    assert(!mc_local_extension_send(local_bytes,sizeof(local_bytes)));
    mc_poll();
    assert(out_used >= 4 && output[0]==0x94 && output[1]==0xc3);
#else
    assert(mc_local_extension_send(NULL,0)==-ENOTSUP);
#endif

	/* Burst overflow is bounded and reported, never treated as valid snapshot. */
	memset(input, 0, 5000);
	in_pos = 0;
	in_used = 5000;
	uart_callbacks[0](&test_radio, NULL);
	out_used = 0;
	mc_poll();
	assert(!mc_ready());
	assert(!mc_snapshot_read(&after));
	assert(after.uart.dropped_bytes == 5000 - 4096);

	puts("PASS: standalone lifecycle, pause/resume, official disconnect, 15-min silent wait, "
	     "RX/reboot/fault while paused, pause timeout, refresh, TX and overflow recovery");
	return 0;
}
