/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <meshtastic_client/client.h>
#include <rt_log/log.h>

RT_LOG_MODULE_REGISTER(mesh_minimal);
static const char *const LOG_TAG = "MESH_EXAMPLE";

/** Callback interno chamado por event do cliente na thread main.
 * Só consome campos emprestados durante a chamada, sem novo comando ao rádio.
 */
static void on_radio(const struct mc_event *event)
{
	switch (event->type) {
	case MC_SYNCED:
		RT_LOGI(LOG_TAG, "Radio synced local=!%08x", event->node);
		break;
	case MC_DISCONNECTED:
		RT_LOGW(LOG_TAG, "Radio disconnected");
		break;
	case MC_RECEIVED:
		RT_LOGI(LOG_TAG, "RX from=!%08x port=%u bytes=%u", event->node, event->port,
			(unsigned)event->length);
		break;
	case MC_ERROR:
		RT_LOGE(LOG_TAG, "Radio error=%d", event->status);
		break;
	default:
		break;
	}
}

/** Entrada externa Zephyr; único dono do cliente, sem sensor/CONC/reporter/USB.
 * O alias UART pertence ao consumidor; sem transmissão periódica de aplicação.
 */
int main(void)
{
	int rc = mc_init(DEVICE_DT_GET(DT_ALIAS(meshtastic_uart)), on_radio);
	if (rc) {
		RT_LOGE(LOG_TAG, "UART init=%d", rc);
		return rc;
	}

	for (;;) {
		mc_poll();
		mc_wait(INT32_MAX);
	}
	return 0;
}
