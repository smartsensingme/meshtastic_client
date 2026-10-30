/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MC_FRAME_H
#define MC_FRAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maior payload Protobuf serial em bytes; header usa quatro bytes adicionais. */
#define MC_FRAME_MAX 512

/** Parser por valor, sem alocação. Inicializar com zeros; um dono por instância.
 * Dados completos sobrevivem até próximos bytes; reset por atribuição de zero.
 */
struct mc_frame {
	uint8_t data[MC_FRAME_MAX]; /**< Payload acumulado, sem header. */
	size_t length;		    /**< Comprimento anunciado, big-endian no fio. */
	size_t used;		    /**< Bytes já armazenados, 0..length para frame válido. */
	unsigned state;		    /**< Interno: 0 sync94, 1 syncc3, 2 lenHi, 3 lenLo, 4 dados. */
};

/** Uso externo ou interno por mc_poll, sem espera/lock: consumir um byte.
 * f não NULL e inicializado; b é byte bruto. 1=frame completo (inclusive vazio),
 * 0=incompleto/ruído, -1=comprimento >512 e parser reiniciado. Sem checksum próprio.
 * Timeout pertence ao chamador; cliente reinicia frame parcial após 2000 ms.
 */
int mc_frame_byte(struct mc_frame *f, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif
