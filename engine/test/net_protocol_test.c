#include "fff.h"
#include "unity.h"

#include "../src/error.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/net_protocol.c" // NOLINT(bugprone-suspicious-include)
#include "../src/strv.c"         // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

#include <string.h>

/* Comfortably larger than the biggest message this suite encodes (a full
 * InputState packet, ~173 bytes) without being a stand-in for
 * NET_MAX_PACKET_SIZE -- this file never touches net.h. */
#define NET_PROTOCOL_TEST_BUFFER_SIZE 256

void setUp(void) {}
void tearDown(void) {}

/* ---- Packet header ---- */

void test_packet_header_round_trip(void)
{
    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    PacketWriter writer = packet_writer_make(buffer, sizeof(buffer));
    PacketHeader header = {.version = PROTOCOL_VERSION, .type = MSG_ACK, .length = 8, .seq = 12345};
    memcpy(header.magic, PROTOCOL_MAGIC, PROTOCOL_MAGIC_LEN);
    TEST_ASSERT_TRUE(packet_header_encode(&writer, header));

    PacketReader reader = packet_reader_make(buffer, writer.cursor);
    PacketHeader decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(packet_header_decode(&reader, &decoded, &err));
    TEST_ASSERT_EQUAL_MEMORY(header.magic, decoded.magic, PROTOCOL_MAGIC_LEN);
    TEST_ASSERT_EQUAL_UINT8(header.version, decoded.version);
    TEST_ASSERT_EQUAL_UINT8(header.type, decoded.type);
    TEST_ASSERT_EQUAL_UINT16(header.length, decoded.length);
    TEST_ASSERT_EQUAL_UINT32(header.seq, decoded.seq);
}

void test_packet_header_decode_rejects_bad_magic(void)
{
    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    PacketWriter writer = packet_writer_make(buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(packet_writer_write_bytes(&writer, (const uint8_t *)"XXXX", PROTOCOL_MAGIC_LEN));
    TEST_ASSERT_TRUE(packet_writer_write_u8(&writer, PROTOCOL_VERSION));
    TEST_ASSERT_TRUE(packet_writer_write_u8(&writer, MSG_ACK));
    TEST_ASSERT_TRUE(packet_writer_write_u16(&writer, 0));
    TEST_ASSERT_TRUE(packet_writer_write_u32(&writer, 0));

    PacketReader reader = packet_reader_make(buffer, writer.cursor);
    PacketHeader decoded;
    ErrorState err = {0};
    TEST_ASSERT_FALSE(packet_header_decode(&reader, &decoded, &err));
    TEST_ASSERT_NOT_NULL(error_get(&err));
}

void test_packet_header_decode_rejects_wrong_version(void)
{
    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    PacketWriter writer = packet_writer_make(buffer, sizeof(buffer));
    PacketHeader header = {.version = (uint8_t)(PROTOCOL_VERSION + 1), .type = MSG_ACK, .length = 0, .seq = 0};
    memcpy(header.magic, PROTOCOL_MAGIC, PROTOCOL_MAGIC_LEN);
    TEST_ASSERT_TRUE(packet_header_encode(&writer, header));

    PacketReader reader = packet_reader_make(buffer, writer.cursor);
    PacketHeader decoded;
    ErrorState err = {0};
    TEST_ASSERT_FALSE(packet_header_decode(&reader, &decoded, &err));
    TEST_ASSERT_NOT_NULL(error_get(&err));
}

void test_packet_header_decode_rejects_truncated_buffer(void)
{
    uint8_t buffer[PACKET_HEADER_SIZE - 1];
    memset(buffer, 0, sizeof(buffer));
    PacketReader reader = packet_reader_make(buffer, sizeof(buffer));
    PacketHeader decoded;
    ErrorState err = {0};
    TEST_ASSERT_FALSE(packet_header_decode(&reader, &decoded, &err));
    TEST_ASSERT_NOT_NULL(error_get(&err));
}

/* ---- Attr record ---- */

void test_attr_record_round_trip_int(void)
{
    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    PacketWriter writer = packet_writer_make(buffer, sizeof(buffer));
    AttrRecord record = {.entity_id = 7, .name = strv_from_cstr("health"), .type = ATTR_INT, .value = {.i = -42}};
    TEST_ASSERT_TRUE(protocol_encode_attr_record(&writer, record));

    PacketReader reader = packet_reader_make(buffer, writer.cursor);
    AttrRecord decoded;
    TEST_ASSERT_TRUE(protocol_decode_attr_record(&reader, &decoded));
    TEST_ASSERT_EQUAL_INT32(record.entity_id, decoded.entity_id);
    TEST_ASSERT_TRUE(strv_eq(record.name, decoded.name));
    TEST_ASSERT_EQUAL_INT(ATTR_INT, decoded.type);
    TEST_ASSERT_EQUAL_INT32(record.value.i, decoded.value.i);
}

void test_attr_record_round_trip_float_bit_exact(void)
{
    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    PacketWriter writer = packet_writer_make(buffer, sizeof(buffer));
    float original = 1.0F / 3.0F; /* many mantissa bits -- epsilon compare would hide a bug here */
    AttrRecord record = {.entity_id = 1, .name = strv_from_cstr("speed"), .type = ATTR_FLOAT, .value = {.f = original}};
    TEST_ASSERT_TRUE(protocol_encode_attr_record(&writer, record));

    PacketReader reader = packet_reader_make(buffer, writer.cursor);
    AttrRecord decoded;
    TEST_ASSERT_TRUE(protocol_decode_attr_record(&reader, &decoded));
    TEST_ASSERT_EQUAL_INT(ATTR_FLOAT, decoded.type);

    uint32_t original_bits = 0;
    uint32_t decoded_bits = 0;
    memcpy(&original_bits, &original, sizeof(original_bits));
    memcpy(&decoded_bits, &decoded.value.f, sizeof(decoded_bits));
    TEST_ASSERT_EQUAL_UINT32(original_bits, decoded_bits);
}

void test_attr_record_round_trip_bool(void)
{
    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    PacketWriter writer = packet_writer_make(buffer, sizeof(buffer));
    AttrRecord record = {.entity_id = 2, .name = strv_from_cstr("is_locked"), .type = ATTR_BOOL, .value = {.b = true}};
    TEST_ASSERT_TRUE(protocol_encode_attr_record(&writer, record));

    PacketReader reader = packet_reader_make(buffer, writer.cursor);
    AttrRecord decoded;
    TEST_ASSERT_TRUE(protocol_decode_attr_record(&reader, &decoded));
    TEST_ASSERT_EQUAL_INT(ATTR_BOOL, decoded.type);
    TEST_ASSERT_TRUE(decoded.value.b);
}

void test_attr_record_round_trip_string(void)
{
    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    PacketWriter writer = packet_writer_make(buffer, sizeof(buffer));
    AttrRecord record = {.entity_id = 3,
                         .name = strv_from_cstr("display_name"),
                         .type = ATTR_STRING,
                         .value = {.str = strv_from_cstr("Golden Chest")}};
    TEST_ASSERT_TRUE(protocol_encode_attr_record(&writer, record));

    PacketReader reader = packet_reader_make(buffer, writer.cursor);
    AttrRecord decoded;
    TEST_ASSERT_TRUE(protocol_decode_attr_record(&reader, &decoded));
    TEST_ASSERT_EQUAL_INT(ATTR_STRING, decoded.type);
    TEST_ASSERT_TRUE(strv_eq(record.value.str, decoded.value.str));
}

void test_protocol_decode_attr_record_rejects_truncated_string(void)
{
    uint8_t buffer[6] = {0}; /* i32 entity_id (4) + u16 string length (2), no string bytes follow */
    PacketWriter writer = packet_writer_make(buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(packet_writer_write_i32(&writer, 42));
    TEST_ASSERT_TRUE(packet_writer_write_u16(&writer, 100)); /* claims 100 bytes that were never written */

    PacketReader reader = packet_reader_make(buffer, sizeof(buffer));
    AttrRecord decoded;
    TEST_ASSERT_FALSE(protocol_decode_attr_record(&reader, &decoded));
}

/* ---- MSG_SNAPSHOT / MSG_DELTA (shared attr-list shape) ---- */

static void make_sample_attr_records(AttrRecord out_records[3])
{
    out_records[0] =
        (AttrRecord){.entity_id = 1, .name = strv_from_cstr("health"), .type = ATTR_INT, .value = {.i = 80}};
    out_records[1] =
        (AttrRecord){.entity_id = 1, .name = strv_from_cstr("speed"), .type = ATTR_FLOAT, .value = {.f = 2.5F}};
    out_records[2] = (AttrRecord){.entity_id = 2,
                                  .name = strv_from_cstr("name"),
                                  .type = ATTR_STRING,
                                  .value = {.str = strv_from_cstr("Skeleton")}};
}

void test_protocol_snapshot_packet_round_trip(void)
{
    AttrRecord records[3];
    make_sample_attr_records(records);

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_snapshot_packet(buffer, sizeof(buffer), 1, records, 3, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_SNAPSHOT, decoded.header.type);

    AttrRecord out_records[3];
    size_t out_count = 0;
    TEST_ASSERT_TRUE(protocol_decode_attr_list(&decoded.reader, out_records, 3, &out_count));
    TEST_ASSERT_EQUAL_size_t(3, out_count);
    for (size_t index = 0; index < 3; index++) {
        TEST_ASSERT_EQUAL_INT32(records[index].entity_id, out_records[index].entity_id);
        TEST_ASSERT_TRUE(strv_eq(records[index].name, out_records[index].name));
        TEST_ASSERT_EQUAL_INT(records[index].type, out_records[index].type);
    }
}

void test_protocol_delta_packet_round_trip(void)
{
    AttrRecord records[3];
    make_sample_attr_records(records);

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_delta_packet(buffer, sizeof(buffer), 2, records, 3, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_DELTA, decoded.header.type);
    TEST_ASSERT_EQUAL_UINT32(2, decoded.header.seq);

    AttrRecord out_records[3];
    size_t out_count = 0;
    TEST_ASSERT_TRUE(protocol_decode_attr_list(&decoded.reader, out_records, 3, &out_count));
    TEST_ASSERT_EQUAL_size_t(3, out_count);
}

void test_protocol_decode_attr_list_rejects_when_cap_too_small(void)
{
    AttrRecord records[3];
    make_sample_attr_records(records);

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    PacketWriter writer = packet_writer_make(buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(protocol_encode_attr_list(&writer, records, 3));

    PacketReader reader = packet_reader_make(buffer, writer.cursor);
    AttrRecord out_records[2];
    size_t out_count = 0;
    TEST_ASSERT_FALSE(protocol_decode_attr_list(&reader, out_records, 2, &out_count));
}

/* ---- MSG_INPUT ---- */

void test_protocol_input_packet_round_trip(void)
{
    InputState input = {0};
    input.key_down[0] = 0x0102030405060708ULL;
    input.key_pressed[1] = 0xA0B0C0D0E0F00010ULL;
    input.gp_connected = true;
    input.gp_button_down = 0x00000005U;
    input.gp_button_pressed = 0x00000001U;
    input.gp_axis[0] = 0.75F;
    input.gp_axis[1] = -1.0F;

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_input_packet(buffer, sizeof(buffer), 9, &input, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_INPUT, decoded.header.type);
    TEST_ASSERT_EQUAL_UINT32(9, decoded.header.seq);

    InputState out = {0};
    TEST_ASSERT_TRUE(protocol_decode_input(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_UINT64(input.key_down[0], out.key_down[0]);
    TEST_ASSERT_EQUAL_UINT64(input.key_pressed[1], out.key_pressed[1]);
    TEST_ASSERT_TRUE(out.gp_connected);
    TEST_ASSERT_EQUAL_UINT32(input.gp_button_down, out.gp_button_down);
    TEST_ASSERT_EQUAL_UINT32(input.gp_button_pressed, out.gp_button_pressed);
    TEST_ASSERT_EQUAL_FLOAT(input.gp_axis[0], out.gp_axis[0]);
    TEST_ASSERT_EQUAL_FLOAT(input.gp_axis[1], out.gp_axis[1]);
}

void test_protocol_decode_input_rejects_truncated_buffer(void)
{
    uint8_t buffer[4] = {0}; /* far short of the bytes a full InputState needs */
    PacketReader reader = packet_reader_make(buffer, sizeof(buffer));
    InputState out;
    TEST_ASSERT_FALSE(protocol_decode_input(&reader, &out));
}

/* ---- MSG_EVENT ---- */

void test_protocol_event_packet_round_trip(void)
{
    EventRecord event = {.event_type = 4, .entity_id = 11, .argument = strv_from_cstr("chest_key")};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_event_packet(buffer, sizeof(buffer), 3, event, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_EVENT, decoded.header.type);

    EventRecord out;
    TEST_ASSERT_TRUE(protocol_decode_event(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_INT32(event.event_type, out.event_type);
    TEST_ASSERT_EQUAL_INT32(event.entity_id, out.entity_id);
    TEST_ASSERT_TRUE(strv_eq(event.argument, out.argument));
}

/* ---- MSG_JOIN ---- */

void test_protocol_join_packet_round_trip(void)
{
    JoinMessage message = {.gamedata_hash = 0x0123456789ABCDEFULL, .client_name = strv_from_cstr("Player Two")};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_join_packet(buffer, sizeof(buffer), 1, message, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_JOIN, decoded.header.type);

    JoinMessage out;
    TEST_ASSERT_TRUE(protocol_decode_join(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_UINT64(message.gamedata_hash, out.gamedata_hash);
    TEST_ASSERT_TRUE(strv_eq(message.client_name, out.client_name));
}

/* ---- MSG_BEACON ---- */

void test_protocol_beacon_packet_round_trip(void)
{
    BeaconMessage message = {.host_name = strv_from_cstr("Tubbles' Game"), .listen_port = 40000};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_beacon_packet(buffer, sizeof(buffer), 0, message, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_BEACON, decoded.header.type);

    BeaconMessage out;
    TEST_ASSERT_TRUE(protocol_decode_beacon(&decoded.reader, &out));
    TEST_ASSERT_TRUE(strv_eq(message.host_name, out.host_name));
    TEST_ASSERT_EQUAL_UINT16(message.listen_port, out.listen_port);
}

/* ---- MSG_ACK ---- */

void test_protocol_ack_packet_round_trip(void)
{
    AckMessage message = {.ack_seq = 500, .ack_bitfield = 0xA5A5A5A5U};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_ack_packet(buffer, sizeof(buffer), 6, message, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_ACK, decoded.header.type);

    AckMessage out;
    TEST_ASSERT_TRUE(protocol_decode_ack(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_UINT32(message.ack_seq, out.ack_seq);
    TEST_ASSERT_EQUAL_UINT32(message.ack_bitfield, out.ack_bitfield);
}

/* ---- MSG_JOIN_ACCEPT ---- */

void test_protocol_join_accept_packet_round_trip(void)
{
    /* op_seq_baseline (S8.7c) deliberately non-trivial: a mid-session join
     * where the host already stamped ops must carry the current counter,
     * not a hardcoded 0/1. */
    JoinAcceptMessage message = {.player_id = 3, .op_seq_baseline = 47};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_join_accept_packet(buffer, sizeof(buffer), 2, message, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_JOIN_ACCEPT, decoded.header.type);

    JoinAcceptMessage out;
    TEST_ASSERT_TRUE(protocol_decode_join_accept(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_INT32(message.player_id, out.player_id);
    TEST_ASSERT_EQUAL_UINT32(message.op_seq_baseline, out.op_seq_baseline);
}

/* ---- MSG_OP ---- */

void test_protocol_op_move_packet_round_trip(void)
{
    EditorOp operation = {.kind = EDITOR_OP_MOVE_ENTITY,
                          .level_name = strv_from_cstr("overworld"),
                          .entity_id = 12,
                          .author_player_id = 3,
                          .op_seq = 77,
                          .move_x = -4.5F,
                          .move_y = -128.25F};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 5, &operation, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_OP, decoded.header.type);

    EditorOp out;
    TEST_ASSERT_TRUE(protocol_decode_op(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_INT(EDITOR_OP_MOVE_ENTITY, out.kind);
    TEST_ASSERT_TRUE(strv_eq(operation.level_name, out.level_name));
    TEST_ASSERT_EQUAL_INT32(operation.entity_id, out.entity_id);
    TEST_ASSERT_EQUAL_INT32(operation.author_player_id, out.author_player_id);
    TEST_ASSERT_EQUAL_UINT32(operation.op_seq, out.op_seq);
    TEST_ASSERT_EQUAL_FLOAT(operation.move_x, out.move_x);
    TEST_ASSERT_EQUAL_FLOAT(operation.move_y, out.move_y);
}

void test_protocol_op_set_attr_string_packet_round_trip(void)
{
    /* attr.entity_id is deliberately different from operation.entity_id: the
     * encoder overwrites it from the op header, so the decoded record must
     * mirror operation.entity_id (42), not the 999 set here. */
    EditorOp operation = {.kind = EDITOR_OP_SET_ATTR,
                          .level_name = strv_from_cstr("dungeon"),
                          .entity_id = 42,
                          .author_player_id = 1,
                          .op_seq = 8,
                          .attr = {.entity_id = 999,
                                   .name = strv_from_cstr("display_name"),
                                   .type = ATTR_STRING,
                                   .value = {.str = strv_from_cstr("Golden Chest")}}};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 4, &operation, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_OP, decoded.header.type);

    EditorOp out;
    TEST_ASSERT_TRUE(protocol_decode_op(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_INT(EDITOR_OP_SET_ATTR, out.kind);
    TEST_ASSERT_TRUE(strv_eq(operation.attr.name, out.attr.name));
    TEST_ASSERT_EQUAL_INT(ATTR_STRING, out.attr.type);
    TEST_ASSERT_TRUE(strv_eq(operation.attr.value.str, out.attr.value.str));
    TEST_ASSERT_EQUAL_INT32(operation.entity_id, out.attr.entity_id);
}

void test_protocol_op_set_attr_float_packet_round_trip(void)
{
    /* Same mirror-invariant check as the string variant, with a
     * float-valued record so the AttrRecord float path is exercised. */
    EditorOp operation = {
        .kind = EDITOR_OP_SET_ATTR,
        .level_name = strv_from_cstr("dungeon"),
        .entity_id = 7,
        .author_player_id = 2,
        .op_seq = 3,
        .attr = {.entity_id = 999, .name = strv_from_cstr("speed"), .type = ATTR_FLOAT, .value = {.f = -2.5F}}};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 4, &operation, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));

    EditorOp out;
    TEST_ASSERT_TRUE(protocol_decode_op(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_INT(EDITOR_OP_SET_ATTR, out.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_FLOAT, out.attr.type);
    TEST_ASSERT_EQUAL_FLOAT(operation.attr.value.f, out.attr.value.f);
    TEST_ASSERT_EQUAL_INT32(operation.entity_id, out.attr.entity_id);
}

void test_protocol_op_delete_packet_round_trip(void)
{
    EditorOp operation = {.kind = EDITOR_OP_DELETE_ENTITY,
                          .level_name = strv_from_cstr("dungeon"),
                          .entity_id = 15,
                          .author_player_id = 4,
                          .op_seq = 22};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 6, &operation, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));

    EditorOp out;
    TEST_ASSERT_TRUE(protocol_decode_op(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_INT(EDITOR_OP_DELETE_ENTITY, out.kind);
    TEST_ASSERT_TRUE(strv_eq(operation.level_name, out.level_name));
    TEST_ASSERT_EQUAL_INT32(operation.entity_id, out.entity_id);
    TEST_ASSERT_EQUAL_INT32(operation.author_player_id, out.author_player_id);
    TEST_ASSERT_EQUAL_UINT32(operation.op_seq, out.op_seq);
}

void test_protocol_op_lock_acquire_packet_round_trip(void)
{
    /* S8.7d1: a lock kind is a header-only payload, exactly like DELETE. One
     * round-trip covering a lock kind proves the appended enumerators encode
     * and decode; the corrupt-kind test below still exercises the moved
     * validity bound via magic byte 200. */
    EditorOp operation = {.kind = EDITOR_OP_LOCK_ACQUIRE,
                          .level_name = strv_from_cstr("overworld"),
                          .entity_id = 9,
                          .author_player_id = 2,
                          .op_seq = 33};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 7, &operation, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_OP, decoded.header.type);

    EditorOp out;
    TEST_ASSERT_TRUE(protocol_decode_op(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_INT(EDITOR_OP_LOCK_ACQUIRE, out.kind);
    TEST_ASSERT_TRUE(strv_eq(operation.level_name, out.level_name));
    TEST_ASSERT_EQUAL_INT32(operation.entity_id, out.entity_id);
    TEST_ASSERT_EQUAL_INT32(operation.author_player_id, out.author_player_id);
    TEST_ASSERT_EQUAL_UINT32(operation.op_seq, out.op_seq);
}

void test_protocol_op_place_packet_round_trip(void)
{
    /* S8.7f3a: PLACE carries a length-prefixed blueprint name plus the spawn
     * position in the move fields; entity_id here plays the echo role (the
     * host-assigned root id) -- a request would carry -1 through the same
     * header field. */
    EditorOp operation = {.kind = EDITOR_OP_PLACE_ENTITY,
                          .level_name = strv_from_cstr("overworld"),
                          .entity_id = 41,
                          .author_player_id = 3,
                          .op_seq = 12,
                          .move_x = 96.5F,
                          .move_y = -20.25F,
                          .blueprint_name = strv_from_cstr("chest")};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 8, &operation, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_OP, decoded.header.type);

    EditorOp out;
    TEST_ASSERT_TRUE(protocol_decode_op(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_INT(EDITOR_OP_PLACE_ENTITY, out.kind);
    TEST_ASSERT_TRUE(strv_eq(operation.level_name, out.level_name));
    TEST_ASSERT_TRUE(strv_eq(operation.blueprint_name, out.blueprint_name));
    TEST_ASSERT_EQUAL_INT32(operation.entity_id, out.entity_id);
    TEST_ASSERT_EQUAL_INT32(operation.author_player_id, out.author_player_id);
    TEST_ASSERT_EQUAL_UINT32(operation.op_seq, out.op_seq);
    TEST_ASSERT_EQUAL_FLOAT(operation.move_x, out.move_x);
    TEST_ASSERT_EQUAL_FLOAT(operation.move_y, out.move_y);
}

void test_protocol_decode_op_rejects_kind_past_remove_attr(void)
{
    /* Exactly one past the new validity bound (EDITOR_OP_REMOVE_ATTR, S8.7f3b)
     * must still be rejected -- the magic-200 unknown-kind test above can't
     * catch an off-by-one at the bound itself. */
    EditorOp operation = {.kind = EDITOR_OP_DELETE_ENTITY,
                          .level_name = strv_from_cstr("dungeon"),
                          .entity_id = 5,
                          .author_player_id = 2,
                          .op_seq = 9};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 1, &operation, &total_len));

    buffer[PACKET_HEADER_SIZE] = (uint8_t)(EDITOR_OP_REMOVE_ATTR + 1);

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    EditorOp out;
    TEST_ASSERT_FALSE(protocol_decode_op(&decoded.reader, &out));
}

void test_protocol_op_remove_attr_packet_round_trip(void)
{
    /* S8.7f3b: REMOVE_ATTR carries a single length-prefixed attr_name string;
     * no type/value, unlike SET_ATTR's full record. */
    EditorOp operation = {.kind = EDITOR_OP_REMOVE_ATTR,
                          .level_name = strv_from_cstr("dungeon"),
                          .entity_id = 42,
                          .author_player_id = 1,
                          .op_seq = 8,
                          .attr_name = strv_from_cstr("collision_w")};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 4, &operation, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_OP, decoded.header.type);

    EditorOp out;
    TEST_ASSERT_TRUE(protocol_decode_op(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_INT(EDITOR_OP_REMOVE_ATTR, out.kind);
    TEST_ASSERT_TRUE(strv_eq(operation.level_name, out.level_name));
    TEST_ASSERT_TRUE(strv_eq(operation.attr_name, out.attr_name));
    TEST_ASSERT_EQUAL_INT32(operation.entity_id, out.entity_id);
    TEST_ASSERT_EQUAL_INT32(operation.author_player_id, out.author_player_id);
    TEST_ASSERT_EQUAL_UINT32(operation.op_seq, out.op_seq);
}

void test_protocol_decode_op_remove_attr_rejects_truncated_buffer(void)
{
    /* Fail-closed contract: one byte short of the encoder's output must fail
     * mid-payload (the attr_name string read) without reading out of bounds. */
    EditorOp operation = {.kind = EDITOR_OP_REMOVE_ATTR,
                          .level_name = strv_from_cstr("dungeon"),
                          .entity_id = 42,
                          .author_player_id = 1,
                          .op_seq = 0,
                          .attr_name = strv_from_cstr("collision_w")};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 1, &operation, &total_len));

    size_t payload_len = total_len - PACKET_HEADER_SIZE;
    PacketReader reader = packet_reader_make(buffer + PACKET_HEADER_SIZE, payload_len - 1);
    EditorOp out;
    TEST_ASSERT_FALSE(protocol_decode_op(&reader, &out));
}

void test_protocol_decode_op_place_rejects_truncated_buffer(void)
{
    /* Same fail-closed contract as the SET_ATTR truncation test: one byte
     * short of the encoder's output must fail mid-payload (the last f32
     * read) without ever reading out of bounds. */
    EditorOp operation = {.kind = EDITOR_OP_PLACE_ENTITY,
                          .level_name = strv_from_cstr("overworld"),
                          .entity_id = -1,
                          .author_player_id = 2,
                          .op_seq = 0,
                          .move_x = 10.0F,
                          .move_y = 20.0F,
                          .blueprint_name = strv_from_cstr("chest")};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 1, &operation, &total_len));

    size_t payload_len = total_len - PACKET_HEADER_SIZE;
    PacketReader reader = packet_reader_make(buffer + PACKET_HEADER_SIZE, payload_len - 1);
    EditorOp out;
    TEST_ASSERT_FALSE(protocol_decode_op(&reader, &out));
}

void test_protocol_op_empty_level_name_round_trip(void)
{
    EditorOp operation = {.kind = EDITOR_OP_DELETE_ENTITY,
                          .level_name = strv_from_cstr(""),
                          .entity_id = 1,
                          .author_player_id = 0,
                          .op_seq = 0};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 0, &operation, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));

    EditorOp out;
    TEST_ASSERT_TRUE(protocol_decode_op(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_size_t(0, out.level_name.len);
}

void test_protocol_decode_op_rejects_unknown_kind(void)
{
    EditorOp operation = {.kind = EDITOR_OP_DELETE_ENTITY,
                          .level_name = strv_from_cstr("dungeon"),
                          .entity_id = 5,
                          .author_player_id = 2,
                          .op_seq = 9};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 1, &operation, &total_len));

    /* The op kind is the first payload byte, right after the header. */
    buffer[PACKET_HEADER_SIZE] = 200;

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    EditorOp out;
    TEST_ASSERT_FALSE(protocol_decode_op(&decoded.reader, &out));
}

void test_protocol_decode_op_rejects_truncated_buffer(void)
{
    EditorOp operation = {.kind = EDITOR_OP_SET_ATTR,
                          .level_name = strv_from_cstr("dungeon"),
                          .entity_id = 5,
                          .author_player_id = 2,
                          .op_seq = 9,
                          .attr = {.name = strv_from_cstr("hp"), .type = ATTR_INT, .value = {.i = 3}}};

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_op_packet(buffer, sizeof(buffer), 1, &operation, &total_len));

    /* Hand the payload decoder one fewer byte than the encoder wrote, so a
     * read runs off the end mid-record. Decode must fail closed and (ASan/
     * UBSan on) never read out of bounds. */
    size_t payload_len = total_len - PACKET_HEADER_SIZE;
    PacketReader reader = packet_reader_make(buffer + PACKET_HEADER_SIZE, payload_len - 1);
    EditorOp out;
    TEST_ASSERT_FALSE(protocol_decode_op(&reader, &out));
}

/* ---- MSG_CURSOR ---- */

void test_protocol_cursor_packet_round_trip(void)
{
    /* Two records: a named peer with a real selection, and one with an empty
     * name and a -1 (no selection) sentinel -- proves both the string and the
     * -1-selection paths round-trip. */
    PresenceRecord records[2] = {
        {.player_id = 1,
         .cursor_x = 12.5F,
         .cursor_y = -4.0F,
         .selected_entity_id = 7,
         .name = strv_from_cstr("Alice")},
        {.player_id = 2, .cursor_x = 0.0F, .cursor_y = 96.0F, .selected_entity_id = -1, .name = strv_from_cstr("")},
    };

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_cursor_packet(buffer, sizeof(buffer), 3, records, 2, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_CURSOR, decoded.header.type);

    /* Zero-init so the analyzer can't flag out[index] as possibly
     * uninitialized: it does not model that the out_count assertion below
     * prunes any decode-returned-fewer path before the fields are read. */
    PresenceRecord out[2] = {0};
    size_t out_count = 0;
    TEST_ASSERT_TRUE(protocol_decode_cursor(&decoded.reader, out, 2, &out_count));
    TEST_ASSERT_EQUAL_size_t(2, out_count);
    for (size_t index = 0; index < 2; index++) {
        TEST_ASSERT_EQUAL_INT32(records[index].player_id, out[index].player_id);
        TEST_ASSERT_EQUAL_FLOAT(records[index].cursor_x, out[index].cursor_x);
        TEST_ASSERT_EQUAL_FLOAT(records[index].cursor_y, out[index].cursor_y);
        TEST_ASSERT_EQUAL_INT32(records[index].selected_entity_id, out[index].selected_entity_id);
        TEST_ASSERT_TRUE(strv_eq(records[index].name, out[index].name));
    }
}

void test_protocol_cursor_empty_list_round_trip(void)
{
    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_cursor_packet(buffer, sizeof(buffer), 0, nullptr, 0, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_CURSOR, decoded.header.type);

    PresenceRecord out[1];
    size_t out_count = 99;
    TEST_ASSERT_TRUE(protocol_decode_cursor(&decoded.reader, out, 1, &out_count));
    TEST_ASSERT_EQUAL_size_t(0, out_count);
}

void test_protocol_decode_cursor_rejects_truncated_buffer(void)
{
    PresenceRecord record = {
        .player_id = 5, .cursor_x = 1.0F, .cursor_y = 2.0F, .selected_entity_id = 3, .name = strv_from_cstr("Bob")};
    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_cursor_packet(buffer, sizeof(buffer), 1, &record, 1, &total_len));

    /* Hand the payload decoder one fewer byte than the encoder wrote, so a
     * read runs off the end mid-record. Decode must fail closed and (ASan/
     * UBSan on) never read out of bounds. */
    size_t payload_len = total_len - PACKET_HEADER_SIZE;
    PacketReader reader = packet_reader_make(buffer + PACKET_HEADER_SIZE, payload_len - 1);
    PresenceRecord out[1];
    size_t out_count = 0;
    TEST_ASSERT_FALSE(protocol_decode_cursor(&reader, out, 1, &out_count));
}

/* ---- MSG_RESYNC ---- */

void test_protocol_resync_packet_round_trip(void)
{
    /* A short payload slice standing in for one chunk of the emitted TOML --
     * every scalar field plus both length-prefixed strings (level name, raw
     * payload bytes) must round-trip verbatim. */
    const char payload_bytes[] = "[[blueprint]]\nname = \"hero\"\n";
    ResyncChunk chunk = {
        .generation = 7,
        .total_bytes = 4096,
        .chunk_index = 3,
        .chunk_count = 4,
        .current_level_name = strv_from_cstr("overworld"),
        .payload = (Strv){.ptr = payload_bytes, .len = sizeof(payload_bytes) - 1},
    };

    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_resync_packet(buffer, sizeof(buffer), 9, &chunk, &total_len));

    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, total_len, &decoded, &err));
    TEST_ASSERT_EQUAL_INT(MSG_RESYNC, decoded.header.type);

    ResyncChunk out = {0};
    TEST_ASSERT_TRUE(protocol_decode_resync(&decoded.reader, &out));
    TEST_ASSERT_EQUAL_UINT32(chunk.generation, out.generation);
    TEST_ASSERT_EQUAL_UINT32(chunk.total_bytes, out.total_bytes);
    TEST_ASSERT_EQUAL_UINT16(chunk.chunk_index, out.chunk_index);
    TEST_ASSERT_EQUAL_UINT16(chunk.chunk_count, out.chunk_count);
    TEST_ASSERT_TRUE(strv_eq(chunk.current_level_name, out.current_level_name));
    TEST_ASSERT_TRUE(strv_eq(chunk.payload, out.payload));
}

void test_protocol_decode_resync_rejects_truncated_buffer(void)
{
    const char payload_bytes[] = "chunkdata";
    ResyncChunk chunk = {
        .generation = 1,
        .total_bytes = 100,
        .chunk_index = 0,
        .chunk_count = 1,
        .current_level_name = strv_from_cstr("test"),
        .payload = (Strv){.ptr = payload_bytes, .len = sizeof(payload_bytes) - 1},
    };
    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_resync_packet(buffer, sizeof(buffer), 1, &chunk, &total_len));

    /* Hand the payload decoder one fewer byte than the encoder wrote so a read
     * runs off the end mid-field. Decode must fail closed and (ASan/UBSan on)
     * never read out of bounds. */
    size_t payload_len = total_len - PACKET_HEADER_SIZE;
    PacketReader reader = packet_reader_make(buffer + PACKET_HEADER_SIZE, payload_len - 1);
    ResyncChunk out = {0};
    TEST_ASSERT_FALSE(protocol_decode_resync(&reader, &out));
}

/* ---- Malformed whole packets ---- */

void test_protocol_decode_packet_rejects_length_mismatch(void)
{
    AckMessage message = {.ack_seq = 1, .ack_bitfield = 0};
    uint8_t buffer[NET_PROTOCOL_TEST_BUFFER_SIZE];
    size_t total_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_ack_packet(buffer, sizeof(buffer), 0, message, &total_len));

    /* Tell decode one fewer byte is available than was actually encoded, so
     * the header's (unmodified) length field no longer matches
     * (claimed_len - PACKET_HEADER_SIZE). The header itself decodes fine --
     * only the length cross-check should reject this. */
    DecodedPacket decoded;
    ErrorState err = {0};
    TEST_ASSERT_FALSE(protocol_decode_packet(buffer, total_len - 1, &decoded, &err));
    TEST_ASSERT_NOT_NULL(error_get(&err));
}

/* ---- gamedata content hash + JOIN verify ---- */

void test_gamedata_content_hash_same_string_same_hash(void)
{
    uint64_t first = gamedata_content_hash("[level]\nname = \"start\"\n");
    uint64_t second = gamedata_content_hash("[level]\nname = \"start\"\n");
    TEST_ASSERT_EQUAL_UINT64(first, second);
}

void test_gamedata_content_hash_one_byte_change_different_hash(void)
{
    uint64_t original = gamedata_content_hash("[level]\nname = \"start\"\n");
    uint64_t changed = gamedata_content_hash("[level]\nname = \"Start\"\n");
    TEST_ASSERT_NOT_EQUAL_UINT64(original, changed);
}

void test_protocol_join_verify_ok_on_match(void)
{
    JoinVerifyResult result = protocol_join_verify(1234, 1234);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_NULL(result.reason);
}

void test_protocol_join_verify_refuses_on_mismatch(void)
{
    JoinVerifyResult result = protocol_join_verify(1234, 5678);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_NOT_NULL(result.reason);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_packet_header_round_trip);
    RUN_TEST(test_packet_header_decode_rejects_bad_magic);
    RUN_TEST(test_packet_header_decode_rejects_wrong_version);
    RUN_TEST(test_packet_header_decode_rejects_truncated_buffer);
    RUN_TEST(test_attr_record_round_trip_int);
    RUN_TEST(test_attr_record_round_trip_float_bit_exact);
    RUN_TEST(test_attr_record_round_trip_bool);
    RUN_TEST(test_attr_record_round_trip_string);
    RUN_TEST(test_protocol_decode_attr_record_rejects_truncated_string);
    RUN_TEST(test_protocol_snapshot_packet_round_trip);
    RUN_TEST(test_protocol_delta_packet_round_trip);
    RUN_TEST(test_protocol_decode_attr_list_rejects_when_cap_too_small);
    RUN_TEST(test_protocol_input_packet_round_trip);
    RUN_TEST(test_protocol_decode_input_rejects_truncated_buffer);
    RUN_TEST(test_protocol_event_packet_round_trip);
    RUN_TEST(test_protocol_join_packet_round_trip);
    RUN_TEST(test_protocol_beacon_packet_round_trip);
    RUN_TEST(test_protocol_ack_packet_round_trip);
    RUN_TEST(test_protocol_join_accept_packet_round_trip);
    RUN_TEST(test_protocol_op_move_packet_round_trip);
    RUN_TEST(test_protocol_op_set_attr_string_packet_round_trip);
    RUN_TEST(test_protocol_op_set_attr_float_packet_round_trip);
    RUN_TEST(test_protocol_op_delete_packet_round_trip);
    RUN_TEST(test_protocol_op_lock_acquire_packet_round_trip);
    RUN_TEST(test_protocol_op_place_packet_round_trip);
    RUN_TEST(test_protocol_op_remove_attr_packet_round_trip);
    RUN_TEST(test_protocol_decode_op_rejects_kind_past_remove_attr);
    RUN_TEST(test_protocol_decode_op_place_rejects_truncated_buffer);
    RUN_TEST(test_protocol_decode_op_remove_attr_rejects_truncated_buffer);
    RUN_TEST(test_protocol_op_empty_level_name_round_trip);
    RUN_TEST(test_protocol_decode_op_rejects_unknown_kind);
    RUN_TEST(test_protocol_decode_op_rejects_truncated_buffer);
    RUN_TEST(test_protocol_cursor_packet_round_trip);
    RUN_TEST(test_protocol_cursor_empty_list_round_trip);
    RUN_TEST(test_protocol_decode_cursor_rejects_truncated_buffer);
    RUN_TEST(test_protocol_resync_packet_round_trip);
    RUN_TEST(test_protocol_decode_resync_rejects_truncated_buffer);
    RUN_TEST(test_protocol_decode_packet_rejects_length_mismatch);
    RUN_TEST(test_gamedata_content_hash_same_string_same_hash);
    RUN_TEST(test_gamedata_content_hash_one_byte_change_different_hash);
    RUN_TEST(test_protocol_join_verify_ok_on_match);
    RUN_TEST(test_protocol_join_verify_refuses_on_mismatch);
    return UNITY_END();
}
