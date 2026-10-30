/* SPDX-License-Identifier: Apache-2.0 */
#include <meshtastic_client/frame.h>

/** Pública e interna: mc_poll. Máquina de estados byte a byte; header 94 c3 e length big-endian,
 * rejeita >512 e não implementa timeout/checksum. */
int mc_frame_byte(struct mc_frame *f, uint8_t b)
{
	switch (f->state) {
	case 0:
		if (b == 0x94)
			f->state = 1;
		break;
	case 1:
		f->state = b == 0xc3 ? 2 : (b == 0x94 ? 1 : 0);
		break;
	case 2:
		f->length = (size_t)b << 8;
		f->state = 3;
		break;
	case 3:
		f->length |= b;
		f->used = 0;
		if (f->length > MC_FRAME_MAX) {
			f->state = 0;
			return -1;
		}
		if (!f->length) {
			f->state = 0;
			return 1;
		}
		f->state = 4;
		break;
	case 4:
		f->data[f->used++] = b;
		if (f->used == f->length) {
			f->state = 0;
			return 1;
		}
		break;
	default:
		f->state = 0;
		break;
	}
	return 0;
}
