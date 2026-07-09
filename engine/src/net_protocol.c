#include "net_protocol.h"

#include "attribute.h"
#include "error.h"
#include "input.h"
#include "strv.h"
#include "vec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

VEC_IMPL(attr_record, AttrRecord)

#define BITS_PER_BYTE 8

/* Pack/unpack `byte_count` bytes of `value`, most-significant byte first
 * (big-endian / network byte order). Looping over byte_count rather than
 * hand-writing one shift per width keeps every shift amount derived
 * (BITS_PER_BYTE * index), never a bare magic constant, and gives every
 * write_u16/u32/u64 below the same body regardless of width.
 *
 * `value` sits between `dest` and `byte_count` (rather than after both)
 * so the two same-width unsigned parameters aren't adjacent -- same dodge
 * toml_emitter.c's emit_float_literal uses for bugprone-easily-swappable-
 * parameters. */
static void write_be_uint(uint64_t value, uint8_t *dest, size_t byte_count)
{
    for (size_t index = 0; index < byte_count; index++) {
        size_t shift = BITS_PER_BYTE * (byte_count - 1 - index);
        dest[index] = (uint8_t)(value >> shift);
    }
}

static uint64_t read_be_uint(const uint8_t *src, size_t byte_count)
{
    uint64_t value = 0;
    for (size_t index = 0; index < byte_count; index++) {
        value = (value << BITS_PER_BYTE) | src[index];
    }
    return value;
}

/* ---- PacketWriter ---- */

static bool packet_writer_reserve(const PacketWriter *writer, size_t count)
{
    return count <= writer->capacity - writer->cursor;
}

static bool packet_writer_write_uint(PacketWriter *writer, uint64_t value, size_t byte_count)
{
    if (!packet_writer_reserve(writer, byte_count)) {
        return false;
    }
    write_be_uint(value, writer->data + writer->cursor, byte_count);
    writer->cursor += byte_count;
    return true;
}

bool packet_writer_write_u8(PacketWriter *writer, uint8_t value)
{
    return packet_writer_write_uint(writer, value, sizeof(value));
}

bool packet_writer_write_u16(PacketWriter *writer, uint16_t value)
{
    return packet_writer_write_uint(writer, value, sizeof(value));
}

bool packet_writer_write_u32(PacketWriter *writer, uint32_t value)
{
    return packet_writer_write_uint(writer, value, sizeof(value));
}

bool packet_writer_write_u64(PacketWriter *writer, uint64_t value)
{
    return packet_writer_write_uint(writer, value, sizeof(value));
}

bool packet_writer_write_i32(PacketWriter *writer, int32_t value)
{
    return packet_writer_write_uint(writer, (uint32_t)value, sizeof(value));
}

bool packet_writer_write_f32(PacketWriter *writer, float value)
{
    static_assert(sizeof(float) == sizeof(uint32_t), "f32 wire encoding assumes a 4-byte IEEE-754 float");
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return packet_writer_write_u32(writer, bits);
}

bool packet_writer_write_bytes(PacketWriter *writer, const uint8_t *bytes, size_t count)
{
    if (!packet_writer_reserve(writer, count)) {
        return false;
    }
    /* A zero-length write is a legitimate no-op (e.g. an empty Strv
     * argument, EventRecord.argument = (Strv){0}) but `bytes` may then be
     * a genuine null pointer -- memcpy's `nonnull` parameter attribute
     * makes calling it with a null source undefined behavior even when
     * count is 0, so skip the call entirely rather than let UBSan (and
     * the standard) flag it. */
    if (count > 0) {
        memcpy(writer->data + writer->cursor, bytes, count);
    }
    writer->cursor += count;
    return true;
}

bool packet_writer_write_string(PacketWriter *writer, Strv value)
{
    if (value.len > UINT16_MAX) {
        return false;
    }
    if (!packet_writer_write_u16(writer, (uint16_t)value.len)) {
        return false;
    }
    return packet_writer_write_bytes(writer, (const uint8_t *)value.ptr, value.len);
}

/* ---- PacketReader ---- */

static bool packet_reader_can_read(const PacketReader *reader, size_t count)
{
    return count <= reader->capacity - reader->cursor;
}

static bool packet_reader_read_uint(PacketReader *reader, uint64_t *out, size_t byte_count)
{
    if (!packet_reader_can_read(reader, byte_count)) {
        return false;
    }
    *out = read_be_uint(reader->data + reader->cursor, byte_count);
    reader->cursor += byte_count;
    return true;
}

bool packet_reader_read_u8(PacketReader *reader, uint8_t *out)
{
    uint64_t value = 0;
    if (!packet_reader_read_uint(reader, &value, sizeof(*out))) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

bool packet_reader_read_u16(PacketReader *reader, uint16_t *out)
{
    uint64_t value = 0;
    if (!packet_reader_read_uint(reader, &value, sizeof(*out))) {
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

bool packet_reader_read_u32(PacketReader *reader, uint32_t *out)
{
    uint64_t value = 0;
    if (!packet_reader_read_uint(reader, &value, sizeof(*out))) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

bool packet_reader_read_u64(PacketReader *reader, uint64_t *out)
{
    return packet_reader_read_uint(reader, out, sizeof(*out));
}

bool packet_reader_read_i32(PacketReader *reader, int32_t *out)
{
    uint32_t bits = 0;
    if (!packet_reader_read_u32(reader, &bits)) {
        return false;
    }
    *out = (int32_t)bits;
    return true;
}

bool packet_reader_read_f32(PacketReader *reader, float *out)
{
    uint32_t bits = 0;
    if (!packet_reader_read_u32(reader, &bits)) {
        return false;
    }
    memcpy(out, &bits, sizeof(bits));
    return true;
}

bool packet_reader_read_bytes(PacketReader *reader, uint8_t *out, size_t count)
{
    if (!packet_reader_can_read(reader, count)) {
        return false;
    }
    memcpy(out, reader->data + reader->cursor, count);
    reader->cursor += count;
    return true;
}

bool packet_reader_read_string(PacketReader *reader, Strv *out)
{
    uint16_t len = 0;
    if (!packet_reader_read_u16(reader, &len)) {
        return false;
    }
    if (!packet_reader_can_read(reader, len)) {
        return false;
    }
    *out = (Strv){.ptr = (const char *)(reader->data + reader->cursor), .len = len};
    reader->cursor += len;
    return true;
}

/* ---- Packet header ---- */

bool packet_header_encode(PacketWriter *writer, PacketHeader header)
{
    if (!packet_writer_write_bytes(writer, (const uint8_t *)header.magic, PROTOCOL_MAGIC_LEN)) {
        return false;
    }
    if (!packet_writer_write_u8(writer, header.version)) {
        return false;
    }
    if (!packet_writer_write_u8(writer, header.type)) {
        return false;
    }
    if (!packet_writer_write_u16(writer, header.length)) {
        return false;
    }
    return packet_writer_write_u32(writer, header.seq);
}

bool packet_header_decode(PacketReader *reader, PacketHeader *out, ErrorState *err)
{
    uint8_t magic_bytes[PROTOCOL_MAGIC_LEN];
    uint8_t version = 0;
    uint8_t type = 0;
    uint16_t length = 0;
    uint32_t seq = 0;

    bool read_ok = packet_reader_read_bytes(reader, magic_bytes, PROTOCOL_MAGIC_LEN) &&
                   packet_reader_read_u8(reader, &version) && packet_reader_read_u8(reader, &type) &&
                   packet_reader_read_u16(reader, &length) && packet_reader_read_u32(reader, &seq);
    if (!read_ok) {
        error_set(err, "packet_header_decode: truncated packet header");
        return false;
    }
    if (memcmp(magic_bytes, PROTOCOL_MAGIC, PROTOCOL_MAGIC_LEN) != 0) {
        error_set(err, "packet_header_decode: bad magic");
        return false;
    }
    if (version != PROTOCOL_VERSION) {
        error_set(err, "packet_header_decode: unsupported version %u (expected %u)", version, PROTOCOL_VERSION);
        return false;
    }

    memcpy(out->magic, magic_bytes, PROTOCOL_MAGIC_LEN);
    out->version = version;
    out->type = type;
    out->length = length;
    out->seq = seq;
    return true;
}

/* ---- Attr record ---- */

bool protocol_encode_attr_record(PacketWriter *writer, AttrRecord record)
{
    if (!packet_writer_write_i32(writer, record.entity_id)) {
        return false;
    }
    if (!packet_writer_write_string(writer, record.name)) {
        return false;
    }
    if (!packet_writer_write_u8(writer, (uint8_t)record.type)) {
        return false;
    }
    switch (record.type) {
    case ATTR_FLOAT:
        return packet_writer_write_f32(writer, record.value.f);
    case ATTR_INT:
        return packet_writer_write_i32(writer, record.value.i);
    case ATTR_BOOL:
        return packet_writer_write_u8(writer, record.value.b ? 1 : 0);
    case ATTR_STRING:
        return packet_writer_write_string(writer, record.value.str);
    }
    return false;
}

bool protocol_decode_attr_record(PacketReader *reader, AttrRecord *out)
{
    int32_t entity_id = 0;
    Strv name = {0};
    uint8_t type_byte = 0;
    if (!packet_reader_read_i32(reader, &entity_id) || !packet_reader_read_string(reader, &name) ||
        !packet_reader_read_u8(reader, &type_byte)) {
        return false;
    }
    if (type_byte > ATTR_STRING) {
        return false;
    }
    AttrType type = (AttrType)type_byte;

    AttrRecordValue value = {0};
    bool value_ok = false;
    switch (type) {
    case ATTR_FLOAT:
        value_ok = packet_reader_read_f32(reader, &value.f);
        break;
    case ATTR_INT:
        value_ok = packet_reader_read_i32(reader, &value.i);
        break;
    case ATTR_BOOL: {
        uint8_t bool_byte = 0;
        value_ok = packet_reader_read_u8(reader, &bool_byte);
        value.b = bool_byte != 0;
        break;
    }
    case ATTR_STRING:
        value_ok = packet_reader_read_string(reader, &value.str);
        break;
    }
    if (!value_ok) {
        return false;
    }

    out->entity_id = entity_id;
    out->name = name;
    out->type = type;
    out->value = value;
    return true;
}

bool protocol_encode_attr_list(PacketWriter *writer, const AttrRecord *records, size_t count)
{
    if (count > UINT16_MAX) {
        return false;
    }
    if (!packet_writer_write_u16(writer, (uint16_t)count)) {
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        if (!protocol_encode_attr_record(writer, records[index])) {
            return false;
        }
    }
    return true;
}

bool protocol_decode_attr_list(PacketReader *reader, AttrRecord *out_records, size_t cap, size_t *out_count)
{
    uint16_t count = 0;
    if (!packet_reader_read_u16(reader, &count)) {
        return false;
    }
    if (count > cap) {
        return false;
    }
    for (uint16_t index = 0; index < count; index++) {
        if (!protocol_decode_attr_record(reader, &out_records[index])) {
            return false;
        }
    }
    *out_count = count;
    return true;
}

/* ---- MSG_INPUT ---- */

static bool write_key_bitset(PacketWriter *writer, const uint64_t bitset[INPUT_KEY_BITSET_WORDS])
{
    for (int index = 0; index < INPUT_KEY_BITSET_WORDS; index++) {
        if (!packet_writer_write_u64(writer, bitset[index])) {
            return false;
        }
    }
    return true;
}

static bool read_key_bitset(PacketReader *reader, uint64_t bitset[INPUT_KEY_BITSET_WORDS])
{
    for (int index = 0; index < INPUT_KEY_BITSET_WORDS; index++) {
        if (!packet_reader_read_u64(reader, &bitset[index])) {
            return false;
        }
    }
    return true;
}

bool protocol_encode_input(PacketWriter *writer, const InputState *input)
{
    if (!write_key_bitset(writer, input->key_down)) {
        return false;
    }
    if (!write_key_bitset(writer, input->key_pressed)) {
        return false;
    }
    if (!packet_writer_write_u8(writer, input->gp_connected ? 1 : 0)) {
        return false;
    }
    if (!packet_writer_write_u32(writer, input->gp_button_down)) {
        return false;
    }
    if (!packet_writer_write_u32(writer, input->gp_button_pressed)) {
        return false;
    }
    for (int index = 0; index < INPUT_GP_AXIS_COUNT; index++) {
        if (!packet_writer_write_f32(writer, input->gp_axis[index])) {
            return false;
        }
    }
    return true;
}

bool protocol_decode_input(PacketReader *reader, InputState *out)
{
    InputState input = {0};
    if (!read_key_bitset(reader, input.key_down)) {
        return false;
    }
    if (!read_key_bitset(reader, input.key_pressed)) {
        return false;
    }
    uint8_t gp_connected_byte = 0;
    if (!packet_reader_read_u8(reader, &gp_connected_byte)) {
        return false;
    }
    input.gp_connected = gp_connected_byte != 0;
    if (!packet_reader_read_u32(reader, &input.gp_button_down)) {
        return false;
    }
    if (!packet_reader_read_u32(reader, &input.gp_button_pressed)) {
        return false;
    }
    for (int index = 0; index < INPUT_GP_AXIS_COUNT; index++) {
        if (!packet_reader_read_f32(reader, &input.gp_axis[index])) {
            return false;
        }
    }
    *out = input;
    return true;
}

/* ---- MSG_EVENT ---- */

bool protocol_encode_event(PacketWriter *writer, EventRecord event)
{
    if (!packet_writer_write_i32(writer, event.event_type)) {
        return false;
    }
    if (!packet_writer_write_i32(writer, event.entity_id)) {
        return false;
    }
    return packet_writer_write_string(writer, event.argument);
}

bool protocol_decode_event(PacketReader *reader, EventRecord *out)
{
    EventRecord event = {0};
    if (!packet_reader_read_i32(reader, &event.event_type)) {
        return false;
    }
    if (!packet_reader_read_i32(reader, &event.entity_id)) {
        return false;
    }
    if (!packet_reader_read_string(reader, &event.argument)) {
        return false;
    }
    *out = event;
    return true;
}

/* ---- MSG_JOIN ---- */

bool protocol_encode_join(PacketWriter *writer, JoinMessage message)
{
    if (!packet_writer_write_u64(writer, message.gamedata_hash)) {
        return false;
    }
    return packet_writer_write_string(writer, message.client_name);
}

bool protocol_decode_join(PacketReader *reader, JoinMessage *out)
{
    JoinMessage message = {0};
    if (!packet_reader_read_u64(reader, &message.gamedata_hash)) {
        return false;
    }
    if (!packet_reader_read_string(reader, &message.client_name)) {
        return false;
    }
    *out = message;
    return true;
}

/* ---- MSG_BEACON ---- */

bool protocol_encode_beacon(PacketWriter *writer, BeaconMessage message)
{
    if (!packet_writer_write_string(writer, message.host_name)) {
        return false;
    }
    return packet_writer_write_u16(writer, message.listen_port);
}

bool protocol_decode_beacon(PacketReader *reader, BeaconMessage *out)
{
    BeaconMessage message = {0};
    if (!packet_reader_read_string(reader, &message.host_name)) {
        return false;
    }
    if (!packet_reader_read_u16(reader, &message.listen_port)) {
        return false;
    }
    *out = message;
    return true;
}

/* ---- MSG_ACK ---- */

bool protocol_encode_ack(PacketWriter *writer, AckMessage message)
{
    if (!packet_writer_write_u32(writer, message.ack_seq)) {
        return false;
    }
    return packet_writer_write_u32(writer, message.ack_bitfield);
}

bool protocol_decode_ack(PacketReader *reader, AckMessage *out)
{
    AckMessage message = {0};
    if (!packet_reader_read_u32(reader, &message.ack_seq)) {
        return false;
    }
    if (!packet_reader_read_u32(reader, &message.ack_bitfield)) {
        return false;
    }
    *out = message;
    return true;
}

/* ---- Whole-packet convenience ---- */

/* Constructs *writer over (buffer, capacity) and writes the packet header
 * into it (length backpatched later by packet_end). Every caller below
 * forwards its own `capacity` and `seq` here together, in the same call --
 * that's what keeps bugprone-easily-swappable-parameters from flagging
 * *their* signatures (the check suppresses a pair it finds used together
 * in one call). Doing so leaves capacity/type/seq bunched in this
 * function's own parameter list, so they're interleaved with the two
 * pointer parameters (capacity, writer, type, buffer, seq) precisely to
 * keep no two of them adjacent -- same dodge toml_emitter.c's
 * emit_float_literal uses, just with more parameters to separate. */
static bool packet_begin(size_t capacity, PacketWriter *writer, MessageType type, uint8_t *buffer, uint32_t seq)
{
    *writer = packet_writer_make(buffer, capacity);
    PacketHeader header = {.version = PROTOCOL_VERSION, .type = (uint8_t)type, .length = 0, .seq = seq};
    memcpy(header.magic, PROTOCOL_MAGIC, PROTOCOL_MAGIC_LEN);
    return packet_header_encode(writer, header);
}

/* Byte offset of the `length` field within an encoded header: magic then
 * version then type precede it, each contributing their own width. */
#define PACKET_LENGTH_FIELD_OFFSET (PROTOCOL_MAGIC_LEN + sizeof(uint8_t) + sizeof(uint8_t))

static bool packet_end(const PacketWriter *writer, size_t *out_len)
{
    size_t payload_length = writer->cursor - PACKET_HEADER_SIZE;
    if (payload_length > UINT16_MAX) {
        return false;
    }
    write_be_uint(payload_length, writer->data + PACKET_LENGTH_FIELD_OFFSET, sizeof(uint16_t));
    *out_len = writer->cursor;
    return true;
}

bool protocol_encode_input_packet(
    uint8_t *buffer, size_t capacity, uint32_t seq, const InputState *input, size_t *out_len)
{
    PacketWriter writer;
    if (!packet_begin(capacity, &writer, MSG_INPUT, buffer, seq)) {
        return false;
    }
    if (!protocol_encode_input(&writer, input)) {
        return false;
    }
    return packet_end(&writer, out_len);
}

static bool encode_attr_list_packet(uint8_t *buffer,
                                    size_t capacity,
                                    MessageType type,
                                    uint32_t seq,
                                    const AttrRecord *records,
                                    size_t count,
                                    size_t *out_len)
{
    PacketWriter writer;
    if (!packet_begin(capacity, &writer, type, buffer, seq)) {
        return false;
    }
    if (!protocol_encode_attr_list(&writer, records, count)) {
        return false;
    }
    return packet_end(&writer, out_len);
}

bool protocol_encode_snapshot_packet(
    uint8_t *buffer, size_t capacity, uint32_t seq, const AttrRecord *records, size_t count, size_t *out_len)
{
    return encode_attr_list_packet(buffer, capacity, MSG_SNAPSHOT, seq, records, count, out_len);
}

bool protocol_encode_delta_packet(
    uint8_t *buffer, size_t capacity, uint32_t seq, const AttrRecord *records, size_t count, size_t *out_len)
{
    return encode_attr_list_packet(buffer, capacity, MSG_DELTA, seq, records, count, out_len);
}

bool protocol_encode_event_packet(uint8_t *buffer, size_t capacity, uint32_t seq, EventRecord event, size_t *out_len)
{
    PacketWriter writer;
    if (!packet_begin(capacity, &writer, MSG_EVENT, buffer, seq)) {
        return false;
    }
    if (!protocol_encode_event(&writer, event)) {
        return false;
    }
    return packet_end(&writer, out_len);
}

bool protocol_encode_join_packet(uint8_t *buffer, size_t capacity, uint32_t seq, JoinMessage message, size_t *out_len)
{
    PacketWriter writer;
    if (!packet_begin(capacity, &writer, MSG_JOIN, buffer, seq)) {
        return false;
    }
    if (!protocol_encode_join(&writer, message)) {
        return false;
    }
    return packet_end(&writer, out_len);
}

bool protocol_encode_beacon_packet(
    uint8_t *buffer, size_t capacity, uint32_t seq, BeaconMessage message, size_t *out_len)
{
    PacketWriter writer;
    if (!packet_begin(capacity, &writer, MSG_BEACON, buffer, seq)) {
        return false;
    }
    if (!protocol_encode_beacon(&writer, message)) {
        return false;
    }
    return packet_end(&writer, out_len);
}

bool protocol_encode_ack_packet(uint8_t *buffer, size_t capacity, uint32_t seq, AckMessage message, size_t *out_len)
{
    PacketWriter writer;
    if (!packet_begin(capacity, &writer, MSG_ACK, buffer, seq)) {
        return false;
    }
    if (!protocol_encode_ack(&writer, message)) {
        return false;
    }
    return packet_end(&writer, out_len);
}

bool protocol_decode_packet(const uint8_t *bytes, size_t len, DecodedPacket *out, ErrorState *err)
{
    PacketReader reader = packet_reader_make(bytes, len);
    if (!packet_header_decode(&reader, &out->header, err)) {
        return false;
    }
    size_t payload_length = len - PACKET_HEADER_SIZE;
    if (out->header.length != payload_length) {
        error_set(err, "protocol_decode_packet: header length %u does not match payload byte count %zu",
                  out->header.length, payload_length);
        return false;
    }
    out->reader = packet_reader_make(bytes + PACKET_HEADER_SIZE, payload_length);
    return true;
}

/* ---- gamedata content hash ---- */

#define FNV64_OFFSET_BASIS 14695981039346656037ULL
#define FNV64_PRIME 1099511628211ULL

uint64_t gamedata_content_hash(const char *toml_string)
{
    uint64_t hash = FNV64_OFFSET_BASIS;
    for (const unsigned char *byte = (const unsigned char *)toml_string; *byte != '\0'; byte++) {
        hash ^= *byte;
        hash *= FNV64_PRIME;
    }
    return hash;
}

JoinVerifyResult protocol_join_verify(uint64_t local_hash, uint64_t remote_hash)
{
    if (local_hash == remote_hash) {
        return (JoinVerifyResult){.ok = true, .reason = nullptr};
    }
    return (JoinVerifyResult){.ok = false, .reason = "gamedata content hash mismatch"};
}
