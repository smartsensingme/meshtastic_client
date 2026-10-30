/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <string.h>
#include "diagnostics_internal.h"
int main(void)
{
	struct mc_diag_snapshot s = {0};
	meshtastic_FromRadio m = meshtastic_FromRadio_init_zero;
	mc_diag_reset(&s);
	assert(s.generation == 1 && !s.synced && !s.lora_received);
	m.which_payload_variant = meshtastic_FromRadio_channel_tag;
	m.channel.index = 1;
	m.channel.role = 2;
	m.channel.has_settings = true;
	strcpy(m.channel.settings.name, "UEL_DEEL");
	m.channel.settings.psk.size = 32;
	memset(m.channel.settings.psk.bytes, 0xa5, 32);
	mc_diag_observe(&s, &m, 123);
	assert(s.channels[1].received && s.channels[1].key_bytes == 32);
	assert(!strcmp(s.channels[1].name, "UEL_DEEL"));
	/* Cache stores key length only, not key bytes. */
	unsigned char key[32];
	memset(key, 0xa5, sizeof key);
	for (size_t i = 0; i + sizeof key <= sizeof s; i++)
		assert(memcmp((char *)&s + i, key, sizeof key));
	m.channel.index = 99;
	mc_diag_observe(&s, &m, 124);
	m = (meshtastic_FromRadio)meshtastic_FromRadio_init_zero;
	m.which_payload_variant = meshtastic_FromRadio_node_info_tag;
	m.node_info.has_user = true;
	strcpy(m.node_info.user.long_name, "CONC");
	for (unsigned i = 1; i <= CONFIG_MESHTASTIC_NODE_CACHE_SIZE + 1; i++) {
		m.node_info.num = i;
		mc_diag_observe(&s, &m, 200 + i);
	}
	assert(s.node_count == CONFIG_MESHTASTIC_NODE_CACHE_SIZE && s.dropped_node_updates == 1);
	m.node_info.num = 1;
	strcpy(m.node_info.user.long_name, "updated");
	mc_diag_observe(&s, &m, 300);
	assert(!strcmp(s.nodes[0].long_name, "updated"));
	s.client_errors = 7;
	s.last_error = -5;
	mc_diag_reset(&s);
	assert(s.generation == 2 && s.node_count == 0 && !s.channels[1].received);
	assert(s.client_errors == 7 && s.last_error == -5 && s.last_rx_ms == 300);
	return 0;
}
