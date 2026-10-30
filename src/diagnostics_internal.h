/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MC_DIAGNOSTICS_INTERNAL_H
#define MC_DIAGNOSTICS_INTERNAL_H
#include "meshtastic/mesh.pb.h"
#include <meshtastic_client/snapshot.h>
/* Helpers internos, thread dona, ponteiros válidos; não retêm dados emprestados.
 * reset preserva erros/último RX; observe/user copiam campos para cache limitado. */
void mc_diag_reset(struct mc_diag_snapshot *snapshot);
void mc_diag_observe(struct mc_diag_snapshot *snapshot, const meshtastic_FromRadio *message,
		     int64_t now);
void mc_diag_user(struct mc_diag_snapshot *snapshot, uint32_t id, const meshtastic_User *user);
#endif
