/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MESHTASTIC_SNAPSHOT_H
#define MESHTASTIC_SNAPSHOT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only mirror of information received from the radio. Never stores PSKs. */
struct mc_diag_node {
	uint32_t id;	     /**< Identificador Meshtastic do nó; zero não é nó válido. */
	uint32_t last_heard; /**< Unix seconds reportados pelo rádio; zero pode indicar ausência. */
	bool has_user;	     /**< Nome de usuário recebido. */
	bool via_mqtt;	     /**< Última observação marcada via MQTT. */
	bool hops_known;     /**< hops_away é conhecido. */
	uint32_t hops_away;  /**< Saltos observados; interpretar somente se hops_known. */
	char long_name[40];  /**< Nome longo copiado/truncado com NUL; não autentica. */
	char short_name[5];  /**< Nome curto copiado com NUL. */
};

/** Observação por valor, propriedade do consumidor; validade indicada pelas flags. */
struct mc_diag_channel {
	bool received;	    /**< Canal recebido nesta geração. */
	bool uplink;	    /**< Uplink MQTT anunciado. */
	bool downlink;	    /**< Downlink MQTT anunciado. */
	uint32_t role;	    /**< Enum Channel.Role numérico. */
	uint32_t key_bytes; /**< Tamanho da PSK anunciado; nunca os bytes da chave. */
	char name[12];	    /**< Nome de canal copiado/truncado com NUL. */
};

/** Observação por valor, propriedade do consumidor; validade indicada pelas flags. */
struct mc_diag_snapshot {
	bool initialized; /**< Snapshot publicado (ou staging iniciado internamente); não é mc_init.
			   */
	bool synced;	  /**< Sessão válida; false após perda, mesmo com cache retido. */
	uint32_t generation;	 /**< Geração publicada, incremento uint32 com wrap. */
	uint32_t local_node;	 /**< Identificador local informado pelo rádio. */
	uint32_t hardware_model; /**< Enum HardwareModel. */
	int64_t last_rx_ms;	 /**< Uptime monotônico ms da última mensagem Protobuf válida. */
	uint32_t client_errors;	 /**< Erros do cliente desde init aceito; uint32 com wrap. */
	int32_t last_error;	 /**< Último errno negativo; zero sem erro. */
	char firmware[32];	 /**< Versão copiada/truncada com NUL. */
	size_t node_count;	 /**< Entradas ocupadas, 0..CONFIG_MESHTASTIC_NODE_CACHE_SIZE. */
	uint32_t dropped_node_updates; /**< Atualizações recusadas por cache cheio, saturante. */
	struct mc_diag_node
	    nodes[CONFIG_MESHTASTIC_NODE_CACHE_SIZE]; /**< Cache limitado, sem expulsão; não é
							 inventário completo da rede. */
	struct mc_diag_channel
	    channels[8];	/**< Canais por índice; consultar received antes dos valores. */
	bool device_received;	/**< Seção device recebida. */
	bool position_received; /**< Seção position recebida. */
	bool lora_received;	/**< Seção LoRa recebida. */
	bool serial_received;	/**< Seção serial recebida. */
	bool mqtt_received;	/**< Seção MQTT recebida. */
	uint32_t device_role;	/**< Enum DeviceConfig.Role. */
	uint32_t gps_mode;	/**< Enum PositionConfig.GpsMode. */
	uint32_t region;	/**< Enum LoRaConfig.RegionCode. */
	uint32_t modem_preset;	/**< Enum LoRaConfig.ModemPreset. */
	uint32_t hop_limit;	/**< Hop limit anunciado, normalmente 0..7. */
	uint32_t channel_num;	/**< Canal RF anunciado; zero automático. */
	bool use_preset;	/**< Usa preset em vez de parâmetros manuais. */
	bool tx_enabled;	/**< Transmissão habilitada no rádio. */
	bool override_frequency_valid; /**< Frequência finita entre 0 e 3000 MHz antes da conversão.
					*/
	int32_t tx_power;	       /**< Potência configurada em dBm; não medida. */
	uint32_t override_frequency_khz; /**< Override em kHz arredondado; válido só com flag; zero
					    desativa. */
	bool mqtt_enabled;		 /**< MQTT habilitado, se seção recebida. */
	bool serial_enabled;		 /**< Serial module habilitado. */
	bool serial_echo;		 /**< Echo configurado. */
	bool serial_override_console;	 /**< Override de console configurado. */
	uint32_t serial_mode;		 /**< Enum SerialConfig.Serial_Mode. */
	uint32_t serial_rxd;		 /**< Número GPIO do rádio, não da placa hospedeira. */
	uint32_t serial_txd;		 /**< Número GPIO TX do rádio. */
	uint32_t serial_baud;		 /**< Enum SerialConfig.Serial_Baud, não baud numérico. */
};

/* Cumulative transport observations. ISR byte/error counters are sampled
 * individually; they need not describe the exact same CPU instant as frames.
 */
struct mc_uart_snapshot {
	uint32_t bytes;		  /**< Bytes RX desde boot, uint32 com wrap. */
	uint32_t frames;	  /**< Frames completos desde boot, uint32 com wrap. */
	uint32_t decode_errors;	  /**< Falhas de decode desde boot, uint32 com wrap. */
	uint32_t packets;	  /**< MeshPackets observados desde boot, uint32 com wrap. */
	uint32_t decoded;	  /**< Packets com payload decoded desde boot, uint32 com wrap. */
	uint32_t not_decoded;	  /**< Packets sem decoded desde boot, uint32 com wrap. */
	uint32_t routing;	  /**< Packets ROUTING_APP desde boot, antes de validar payload. */
	uint32_t dropped_bytes;	  /**< Bytes descartados no ring RX desde boot, uint32 com wrap. */
	uint32_t uart_error_mask; /**< OR cumulativo dos bits de erro UART desde boot. */
	/* TX counts are saturating, sampled under TX lock. pending includes completed
	 * slots awaiting owner reaping. bytes counts FIFO acceptance, not delivery.
	 */
	uint32_t
	    tx_enqueued; /**< Frames TX aceitos desde init, saturante, inclui controle/padding. */
	uint32_t tx_drained;  /**< Frames totalmente aceitos na FIFO desde init, saturante. */
	uint32_t tx_failed;   /**< Frames abortados desde init, saturante. */
	uint32_t tx_rejected; /**< Admissões recusadas por fila/erro TX desde init, saturante. */
	uint32_t tx_bytes;    /**< Bytes aceitos na FIFO desde init, saturante; não entrega. */
	uint32_t tx_pending;  /**< Slots ocupados incluindo conclusões por reap, 0..depth. */
};

/* Owning copy: no pointers into client buffers, no keys or raw protobufs. */
struct mc_snapshot {
	struct mc_diag_snapshot radio; /**< Cache de rádio por valor, sem credenciais. */
	struct mc_uart_snapshot uart;  /**< Observações locais de transporte, não entrega LoRa. */
	bool refreshing;	/**< Novo snapshot em curso; radio pode conter geração anterior. */
	int64_t captured_at_ms; /**< Uptime monotônico ms da cópia local. */
	int64_t
	    snapshot_completed_at_ms; /**< Uptime ms do último commit; zero antes do primeiro. */
};
/** Cópia externa pela thread dona de poll/send/refresh, nunca ISR.
 * Pode ser chamada antes de init e por callback do dono; não inicia consulta,
 * logging, alocação, refresh nem altera sessão. Copiar antes de publicar a outra
 * thread. Contadores RX são amostrados individualmente; TX sob lock próprio.
 * Antes do primeiro commit, initialized/synced são falsos; histórico pode ser
 * retido com synced=false. Sem chamadores internos.
 * @param snapshot Destino não NULL e distinto das estruturas internas.
 * @return 0; -EINVAL para NULL, sem mutação. Custo de cópia limitado pelo Kconfig.
 */
int mc_snapshot_read(struct mc_snapshot *snapshot);
#ifdef __cplusplus
}
#endif

#endif
