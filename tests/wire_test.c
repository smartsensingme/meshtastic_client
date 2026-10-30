/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "meshtastic/mesh.pb.h"
#include <meshtastic_client/frame.h>
#include "fixtures.h"
static unsigned count;
static void consume(struct mc_frame *frame, const unsigned char *data, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		int rc = mc_frame_byte(frame, data[i]);
		if (rc == 1) {
			meshtastic_FromRadio m = meshtastic_FromRadio_init_zero;
			pb_istream_t s = pb_istream_from_buffer(frame->data, frame->length);
			assert(pb_decode(&s, meshtastic_FromRadio_fields, &m));
			if (count == 0)
				assert(m.my_info.my_node_num == 0x3fd7d85c);
			if (count == 1)
				assert(m.config_complete_id == 123456);
			if (count == 2) {
				assert(m.packet.from == 0x1234abcd);
				assert(m.packet.decoded.portnum == 256);
				assert(m.packet.decoded.payload.size == 5);
				assert(!memcmp(m.packet.decoded.payload.bytes, "hello", 5));
			}
			count++;
		}
	}
}
int main(void)
{
	struct mc_frame f = {0};
	/* Noise and repeated start bytes must recover into fragmented frames. */
	const unsigned char noise[] = {0x00, 0x94, 0x94};
	consume(&f, noise, sizeof(noise));
	for (size_t i = 0; i < sizeof(inbound); i++)
		consume(&f, inbound + i, 1);
	assert(count == 3);
	struct mc_frame bad = {0};
	assert(mc_frame_byte(&bad, 0x94) == 0);
	assert(mc_frame_byte(&bad, 0xc3) == 0);
	assert(mc_frame_byte(&bad, 2) == 0);
	assert(mc_frame_byte(&bad, 1) == -1); /* 513 rejected */
	assert(bad.state == 0);
	const unsigned char partial[] = {0x12, 0x80};
	meshtastic_FromRadio in = meshtastic_FromRadio_init_zero;
	pb_istream_t rd = pb_istream_from_buffer(partial, sizeof(partial));
	assert(!pb_decode(&rd, meshtastic_FromRadio_fields, &in));
	meshtastic_ToRadio out = meshtastic_ToRadio_init_zero;
	out.which_payload_variant = meshtastic_ToRadio_packet_tag;
	out.packet.to = 0x1234abcd;
	out.packet.channel = 1;
	out.packet.id = 42;
	out.packet.hop_limit = 1;
	out.packet.want_ack = true;
	out.packet.priority = meshtastic_MeshPacket_Priority_RELIABLE;
	out.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
	out.packet.decoded.portnum = 256;
	out.packet.decoded.payload.size = 5;
	memcpy(out.packet.decoded.payload.bytes, "hello", 5);
	unsigned char encoded[512];
	pb_ostream_t wr = pb_ostream_from_buffer(encoded, sizeof(encoded));
	assert(pb_encode(&wr, meshtastic_ToRadio_fields, &out));
	assert(wr.bytes_written == sizeof(expected_out));
	assert(!memcmp(encoded, expected_out, sizeof(expected_out)));
	puts("PASS: official Python/nanopb vectors, fragmented/noisy framing, length and truncated "
	     "protobuf rejection");
}
