/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MC_DIAGNOSTICS_INTERNAL_H
#define MC_DIAGNOSTICS_INTERNAL_H

#include <meshtastic_client/snapshot.h>
#include "meshtastic/mesh.pb.h"

/** Interna ao módulo, atualmente usada pelos testes de cache; thread dona.
 * @param snapshot Cache não NULL, geração/erros/último RX preservados conforme fonte.
 * @return Sem valor; zera conteúdo, incrementa geração e marca initialized.
 */
void mc_diag_reset(struct mc_diag_snapshot *snapshot);

/** Interna, chamada por process; copia somente campos permitidos, sem credenciais.
 * @param snapshot Cache/staging não NULL de propriedade da thread dona.
 * @param message Protobuf decodificado emprestado durante chamada, não retido.
 * @param now Uptime monotônico ms da observação.
 * @return Sem valor. Inserções limitadas, sem expulsão; descarte contabilizado.
 */
void mc_diag_observe(struct mc_diag_snapshot *snapshot, const meshtastic_FromRadio *message,
		     int64_t now);

/** Interna, chamada por process para NODEINFO, preservando metadados existentes.
 * @param snapshot Cache não NULL, thread dona.
 * @param id Identificador do nó observado no pacote.
 * @param user Dados emprestados não NULL; nomes copiados com terminação NUL.
 * @return Sem valor; cache cheio rejeita novo id e conta descarte saturante.
 */
void mc_diag_user(struct mc_diag_snapshot *snapshot, uint32_t id, const meshtastic_User *user);

#endif
