/* SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <string.h>

#include "diagnostics_internal.h"

/** Interna: node_update/mc_diag_observe/mc_diag_user. Copia string NUL para destino de tamanho >0,
 * truncando e terminando sempre. */
static void copy_text(char *to, size_t size, const char *from)
{
	size_t len = strlen(from);
	if (len >= size)
		len = size - 1;

	memcpy(to, from, len);
	to[len] = '\0';
}

/** Interna ao módulo, usada por testes de cache. Zera geração de conteúdo, preserva erros/último RX
 * e incrementa geração uint32 com wrap; não faz I/O. */
void mc_diag_reset(struct mc_diag_snapshot *s)
{
	uint32_t generation = s->generation + 1, errors = s->client_errors;
	int32_t last_error = s->last_error;
	int64_t last_rx = s->last_rx_ms;

	/* Clear contents while preserving cumulative error observations. */
	memset(s, 0, sizeof(*s));
	s->initialized = true;
	s->generation = generation;
	s->client_errors = errors;
	s->last_error = last_error;
	s->last_rx_ms = last_rx;
}

/** Interna: mc_diag_observe. Atualiza nó conhecido ou insere até capacidade; sem expulsão, conta
 * descarte saturante quando cheio. */
static void node_update(struct mc_diag_snapshot *s, const meshtastic_NodeInfo *node)
{
	if (!node->num)
		return;
	size_t i;
	for (i = 0; i < s->node_count; i++)
		if (s->nodes[i].id == node->num)
			break;
	if (i == s->node_count) {
		if (i == CONFIG_MESHTASTIC_NODE_CACHE_SIZE) {
			if (s->dropped_node_updates != UINT32_MAX)
				++s->dropped_node_updates;
			return;
		}
		++s->node_count;
	}

	/* Publish only observed metadata; absent User preserves cached identity. */
	struct mc_diag_node *out = &s->nodes[i];
	out->id = node->num;
	out->last_heard = node->last_heard;
	out->via_mqtt = node->via_mqtt;
	out->hops_known = node->has_hops_away;
	out->hops_away = node->hops_away;
	if (node->has_user) {
		out->has_user = true;
		copy_text(out->long_name, sizeof(out->long_name), node->user.long_name);
		copy_text(out->short_name, sizeof(out->short_name), node->user.short_name);
	}
}

/** Interna: process. Observa apenas campos permitidos do Protobuf, sem copiar credenciais; valida
 * frequência finita/faixa antes de converter MHz para kHz. */
void mc_diag_observe(struct mc_diag_snapshot *s, const meshtastic_FromRadio *m, int64_t now)
{
	s->last_rx_ms = now;
	switch (m->which_payload_variant) {
	case meshtastic_FromRadio_my_info_tag:
		s->local_node = m->my_info.my_node_num;
		break;
	case meshtastic_FromRadio_node_info_tag:
		node_update(s, &m->node_info);
		break;
	case meshtastic_FromRadio_metadata_tag:
		copy_text(s->firmware, sizeof(s->firmware), m->metadata.firmware_version);
		s->hardware_model = m->metadata.hw_model;
		break;
	case meshtastic_FromRadio_channel_tag: {
		int index = m->channel.index;
		if (index < 0 || index >= 8)
			break;
		struct mc_diag_channel *out = &s->channels[index];
		*out = (struct mc_diag_channel){.received = true, .role = m->channel.role};
		if (m->channel.has_settings) {
			copy_text(out->name, sizeof(out->name), m->channel.settings.name);
			out->key_bytes = m->channel.settings.psk.size;
			out->uplink = m->channel.settings.uplink_enabled;
			out->downlink = m->channel.settings.downlink_enabled;
		}
		break;
	}
	case meshtastic_FromRadio_config_tag:
		switch (m->config.which_payload_variant) {
		case meshtastic_Config_device_tag:
			s->device_received = true;
			s->device_role = m->config.payload_variant.device.role;
			break;
		case meshtastic_Config_position_tag:
			s->position_received = true;
			s->gps_mode = m->config.payload_variant.position.gps_mode;
			break;
		case meshtastic_Config_lora_tag: {
			const meshtastic_Config_LoRaConfig *l = &m->config.payload_variant.lora;
			s->lora_received = true;
			s->region = l->region;
			s->modem_preset = l->modem_preset;
			s->use_preset = l->use_preset;
			s->tx_enabled = l->tx_enabled;
			s->tx_power = l->tx_power;
			s->hop_limit = l->hop_limit;
			s->channel_num = l->channel_num;
			s->override_frequency_valid = isfinite(l->override_frequency) &&
						      l->override_frequency >= 0 &&
						      l->override_frequency <= 3000;
			s->override_frequency_khz =
			    s->override_frequency_valid
				? (uint32_t)(l->override_frequency * 1000.0f + 0.5f)
				: 0;
			break;
		}
		default:
			break;
		}
		break;
	case meshtastic_FromRadio_moduleConfig_tag:
		switch (m->moduleConfig.which_payload_variant) {
		case meshtastic_ModuleConfig_mqtt_tag:
			s->mqtt_received = true;
			s->mqtt_enabled = m->moduleConfig.payload_variant.mqtt.enabled;
			break;
		case meshtastic_ModuleConfig_serial_tag: {
			const meshtastic_ModuleConfig_SerialConfig *c =
			    &m->moduleConfig.payload_variant.serial;
			s->serial_received = true;
			s->serial_enabled = c->enabled;
			s->serial_echo = c->echo;
			s->serial_rxd = c->rxd;
			s->serial_txd = c->txd;
			s->serial_baud = c->baud;
			s->serial_mode = c->mode;
			s->serial_override_console = c->override_console_serial_port;
			break;
		}
		default:
			break;
		}
		break;
	default:
		break;
	}
}

/** Interna: process (NODEINFO). Substitui somente usuário, preservando metadados existentes;
 * inserção limitada, sem alocação. */
void mc_diag_user(struct mc_diag_snapshot *s, uint32_t id, const meshtastic_User *user)
{
	size_t i;
	for (i = 0; i < s->node_count; i++)
		if (s->nodes[i].id == id)
			break;
	if (i == s->node_count) {
		if (i == CONFIG_MESHTASTIC_NODE_CACHE_SIZE) {
			if (s->dropped_node_updates != UINT32_MAX)
				++s->dropped_node_updates;
			return;
		}
		s->nodes[i] = (struct mc_diag_node){.id = id};
		++s->node_count;
	}

	/* NODEINFO replaces User only, preserving last-heard/hops metadata. */
	struct mc_diag_node *n = &s->nodes[i];
	n->has_user = true;
	copy_text(n->long_name, sizeof(n->long_name), user->long_name);
	copy_text(n->short_name, sizeof(n->short_name), user->short_name);
}
