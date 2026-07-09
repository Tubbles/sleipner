#pragma once

/* net_protocol.h — the multiplayer wire format (S8.2). Pure byte-buffer
 * encode/decode: this file never touches NetTransport (net.h), sockets, or
 * a loopback inbox. It turns typed game data (InputState, attribute
 * changes, join/beacon/ack info) into bytes and back, and nothing else.
 * S8.4 is the layer that calls net_send/net_recv and feeds the results
 * through here.
 *
 * Byte order: every multi-byte field is BIG-ENDIAN ("network byte order"),
 * packed by hand a byte at a time (see write_be_uint/read_be_uint in
 * net_protocol.c) rather than via htons/ntohl. That keeps encode/decode
 * deterministic and host-endianness-independent, so the round-trip tests
 * in net_protocol_test.c exercise the real wire format on any host,
 * without pulling a sockets header into a file that never opens a socket.
 *
 * Ownership: every decoded string is a Strv — a view into the byte buffer
 * the caller passed to protocol_decode_packet. Per strv.h's contract, that
 * view is only valid as long as the backing buffer is; a caller that needs
 * a decoded name/argument to outlive the receive buffer (e.g. to store it
 * in gamedata_arena) must copy it out (str_from_strv) before reusing or
 * freeing the buffer. Encoding is symmetric: every encode_* function takes
 * Strv (read-only view in, no allocation) rather than an owning Str. */

#include "attribute.h" // AttrType
#include "error.h"     // ErrorState
#include "input.h"     // InputState
#include "strv.h"      // Strv

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Byte buffer writer/reader ----
 *
 * Both are plain data carriers: a caller-owned buffer, its capacity, and a
 * cursor. Neither allocates; capacity is fixed at construction. Every write
 * or read call bounds-checks against (capacity - cursor) before touching
 * memory, so a call that would overflow the buffer or read past its end
 * returns false and leaves the buffer/cursor untouched -- it never reads or
 * writes out of bounds. Construct with packet_writer_make / packet_reader_make,
 * or aggregate-initialize directly (plain structs, no hidden state). */

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t cursor;
} PacketWriter;

typedef struct {
    const uint8_t *data;
    size_t capacity;
    size_t cursor;
} PacketReader;

static inline PacketWriter packet_writer_make(uint8_t *data, size_t capacity)
{
    return (PacketWriter){.data = data, .capacity = capacity, .cursor = 0};
}

static inline PacketReader packet_reader_make(const uint8_t *data, size_t capacity)
{
    return (PacketReader){.data = data, .capacity = capacity, .cursor = 0};
}

[[nodiscard]] bool packet_writer_write_u8(PacketWriter *writer, uint8_t value);
[[nodiscard]] bool packet_writer_write_u16(PacketWriter *writer, uint16_t value);
[[nodiscard]] bool packet_writer_write_u32(PacketWriter *writer, uint32_t value);
[[nodiscard]] bool packet_writer_write_u64(PacketWriter *writer, uint64_t value);
[[nodiscard]] bool packet_writer_write_i32(PacketWriter *writer, int32_t value);
/* IEEE-754 bit pattern, written as a big-endian u32 -- see net_protocol.c. */
[[nodiscard]] bool packet_writer_write_f32(PacketWriter *writer, float value);
[[nodiscard]] bool packet_writer_write_bytes(PacketWriter *writer, const uint8_t *bytes, size_t count);
/* u16 length prefix + raw bytes. Fails if value.len does not fit a u16. */
[[nodiscard]] bool packet_writer_write_string(PacketWriter *writer, Strv value);

[[nodiscard]] bool packet_reader_read_u8(PacketReader *reader, uint8_t *out);
[[nodiscard]] bool packet_reader_read_u16(PacketReader *reader, uint16_t *out);
[[nodiscard]] bool packet_reader_read_u32(PacketReader *reader, uint32_t *out);
[[nodiscard]] bool packet_reader_read_u64(PacketReader *reader, uint64_t *out);
[[nodiscard]] bool packet_reader_read_i32(PacketReader *reader, int32_t *out);
[[nodiscard]] bool packet_reader_read_f32(PacketReader *reader, float *out);
[[nodiscard]] bool packet_reader_read_bytes(PacketReader *reader, uint8_t *out, size_t count);
/* *out is a Strv into reader's own buffer -- see the Ownership note above. */
[[nodiscard]] bool packet_reader_read_string(PacketReader *reader, Strv *out);

/* ---- Packet header ---- */

#define PROTOCOL_MAGIC "SLPN"
#define PROTOCOL_MAGIC_LEN 4
#define PROTOCOL_VERSION 1

typedef struct {
    char magic[PROTOCOL_MAGIC_LEN];
    uint8_t version;
    uint8_t type;    /* a MessageType, stored as u8 on the wire */
    uint16_t length; /* payload length in bytes, header excluded */
    uint32_t seq;
} PacketHeader;

/* Wire size of a PacketHeader: magic + version + type + length + seq.
 * Computed from field widths (not a bare literal) so it can never drift
 * from packet_header_encode/decode's actual field-by-field layout. */
#define PACKET_HEADER_SIZE                                                                                             \
    (PROTOCOL_MAGIC_LEN + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint32_t))

[[nodiscard]] bool packet_header_encode(PacketWriter *writer, PacketHeader header);

/* Rejects (returns false, err set to the reason) a wrong magic, a wrong
 * version, or a buffer too short to hold a full header. err must be
 * non-null, per this codebase's ErrorState convention. */
[[nodiscard]] bool packet_header_decode(PacketReader *reader, PacketHeader *out, ErrorState *err);

/* ---- Message types ---- */

typedef enum {
    MSG_INPUT,
    MSG_SNAPSHOT,
    MSG_DELTA,
    MSG_EVENT,
    MSG_JOIN,
    MSG_BEACON,
    MSG_ACK,
} MessageType;

/* ---- Attr record: entity_id + name + typed value.
 * The shared unit MSG_SNAPSHOT/MSG_DELTA carry (one per changed/synced
 * attribute). Reuses AttrType (attribute.h) as the wire type tag; the
 * value itself is a protocol-local union (Strv, not attribute.h's owning
 * Str) since encode only ever needs to read bytes, never own them. ---- */

typedef union {
    float f;
    int32_t i;
    bool b;
    Strv str;
} AttrRecordValue;

typedef struct {
    int32_t entity_id;
    Strv name;
    AttrType type;
    AttrRecordValue value;
} AttrRecord;

[[nodiscard]] bool protocol_encode_attr_record(PacketWriter *writer, AttrRecord record);
[[nodiscard]] bool protocol_decode_attr_record(PacketReader *reader, AttrRecord *out);

/* MSG_SNAPSHOT (full) and MSG_DELTA (changed-only) share this exact wire
 * shape -- a u16 count followed by that many attr records; only the
 * packet header's `type` distinguishes the two semantically. Decode fills
 * out_records (caller-owned, length cap) and fails outright, rather than
 * truncating, if the wire count exceeds cap. */
[[nodiscard]] bool protocol_encode_attr_list(PacketWriter *writer, const AttrRecord *records, size_t count);
[[nodiscard]] bool
protocol_decode_attr_list(PacketReader *reader, AttrRecord *out_records, size_t cap, size_t *out_count);

/* ---- MSG_INPUT: an InputState, field for field. ---- */

[[nodiscard]] bool protocol_encode_input(PacketWriter *writer, const InputState *input);
[[nodiscard]] bool protocol_decode_input(PacketReader *reader, InputState *out);

/* ---- MSG_EVENT: a minimal game-event record.
 *
 * TriggerEvent (rule.h) is {TriggerType, entity_index, Str argument}, but
 * rule.h pulls in the full rule/effect/entity apparatus -- far more than a
 * pure wire-format layer should depend on. event_type is therefore a raw
 * int32 (the caller's TriggerType cast to int32, or any other event id
 * space S8.4 wants) rather than TriggerType itself. This is a deliberate
 * narrowing of TriggerEvent's shape to what the wire needs to convey. ---- */

typedef struct {
    int32_t event_type;
    int32_t entity_id;
    Strv argument;
} EventRecord;

[[nodiscard]] bool protocol_encode_event(PacketWriter *writer, EventRecord event);
[[nodiscard]] bool protocol_decode_event(PacketReader *reader, EventRecord *out);

/* ---- MSG_JOIN: gamedata content hash + client name.
 * See gamedata_content_hash/protocol_join_verify below. ---- */

typedef struct {
    uint64_t gamedata_hash;
    Strv client_name;
} JoinMessage;

[[nodiscard]] bool protocol_encode_join(PacketWriter *writer, JoinMessage message);
[[nodiscard]] bool protocol_decode_join(PacketReader *reader, JoinMessage *out);

/* ---- MSG_BEACON: LAN advertisement (S8.3 broadcasts this ~1/s). ---- */

typedef struct {
    Strv host_name;
    uint16_t listen_port;
} BeaconMessage;

[[nodiscard]] bool protocol_encode_beacon(PacketWriter *writer, BeaconMessage message);
[[nodiscard]] bool protocol_decode_beacon(PacketReader *reader, BeaconMessage *out);

/* ---- MSG_ACK: the reliable sub-channel S8.4 uses for events.
 * ack_bitfield's bit N (0-indexed from the low bit) acks sequence number
 * (ack_seq - 1 - N), covering the PROTOCOL_ACK_BITFIELD_BITS sequence
 * numbers immediately before ack_seq -- standard seq+bitfield resend, no
 * TCP involved. ---- */

#define PROTOCOL_ACK_BITFIELD_BITS 32

typedef struct {
    uint32_t ack_seq;
    uint32_t ack_bitfield;
} AckMessage;

[[nodiscard]] bool protocol_encode_ack(PacketWriter *writer, AckMessage message);
[[nodiscard]] bool protocol_decode_ack(PacketReader *reader, AckMessage *out);

/* ---- Whole-packet convenience: header + payload in one call, with
 * `length` back-patched once the payload's actual size is known. Each
 * writes into a caller-owned buffer (e.g. a NET_MAX_PACKET_SIZE stack
 * buffer) and reports the total bytes written (header + payload) via
 * out_len. One wrapper per message type, since C has no generic payload
 * parameter -- each just brackets its matching protocol_encode_* above
 * with a header write and a length back-patch. ---- */

[[nodiscard]] bool
protocol_encode_input_packet(uint8_t *buffer, size_t capacity, uint32_t seq, const InputState *input, size_t *out_len);
[[nodiscard]] bool protocol_encode_snapshot_packet(
    uint8_t *buffer, size_t capacity, uint32_t seq, const AttrRecord *records, size_t count, size_t *out_len);
[[nodiscard]] bool protocol_encode_delta_packet(
    uint8_t *buffer, size_t capacity, uint32_t seq, const AttrRecord *records, size_t count, size_t *out_len);
[[nodiscard]] bool
protocol_encode_event_packet(uint8_t *buffer, size_t capacity, uint32_t seq, EventRecord event, size_t *out_len);
[[nodiscard]] bool
protocol_encode_join_packet(uint8_t *buffer, size_t capacity, uint32_t seq, JoinMessage message, size_t *out_len);
[[nodiscard]] bool
protocol_encode_beacon_packet(uint8_t *buffer, size_t capacity, uint32_t seq, BeaconMessage message, size_t *out_len);
[[nodiscard]] bool
protocol_encode_ack_packet(uint8_t *buffer, size_t capacity, uint32_t seq, AckMessage message, size_t *out_len);

/* Decoded header plus a reader already positioned at the payload, bounded
 * to exactly header.length bytes -- a payload decode call can never read
 * past the packet it was decoded from, even if the caller's original
 * buffer was larger. Validates the header (magic/version/truncation, via
 * packet_header_decode) and that header.length equals the actual payload
 * byte count (len - PACKET_HEADER_SIZE); a mismatch is rejected as
 * malformed rather than trusting the declared length. err must be
 * non-null. */
typedef struct {
    PacketHeader header;
    PacketReader reader;
} DecodedPacket;

[[nodiscard]] bool protocol_decode_packet(const uint8_t *bytes, size_t len, DecodedPacket *out, ErrorState *err);

/* ---- gamedata content hash, exchanged at JOIN ----
 *
 * FNV-1a, 64-bit, over toml_string's bytes up to its null terminator.
 * Mirrors strv_hash's (strv.h) 32-bit FNV-1a algorithm exactly, just at
 * double the width and over a plain C string rather than a Strv, since
 * GamedataParams.toml_string (game.h) is what's hashed. Picked over
 * widening strv_hash itself so every existing 32-bit-keyed map
 * (map_strv_sound, map_strv_int, ...) keeps its current hash unchanged. */
[[nodiscard]] uint64_t gamedata_content_hash(const char *toml_string);

typedef struct {
    bool ok;
    const char *reason; /* nullptr when ok; a static string otherwise */
} JoinVerifyResult;

/* Pure comparison: ok when the hashes match, a refusal reason otherwise.
 * S8.4 calls this after decoding a MSG_JOIN to decide whether to accept
 * the client or refuse the connection. */
[[nodiscard]] JoinVerifyResult protocol_join_verify(uint64_t local_hash, uint64_t remote_hash);
