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
#include "vec.h"       // VEC_DECL

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
    /* S8.6: HOST->client reply to a verified MSG_JOIN, carrying the
     * player_id network.c's register_client just assigned the sender. See
     * JoinAcceptMessage below. */
    MSG_JOIN_ACCEPT,
    /* S8.7: a single editor operation (see EditorOp below). Rides the
     * reliable sub-channel in later slices; this slice is encode/decode
     * only -- nothing applies an op to game state yet. Appended last on
     * purpose: MessageType values are wire bytes, so enumerators are never
     * reordered or renumbered. No PROTOCOL_VERSION bump is needed -- an
     * unknown message type is already silently ignored by every drain
     * loop, so adding one is backward compatible. */
    MSG_OP,
    /* S8.7e: editor PRESENCE -- a peer's cursor, selection, and display name
     * (see PresenceRecord below). Soft, high-frequency, loss-tolerant state:
     * rides the fire-and-forget path like MSG_SNAPSHOT/MSG_DELTA, NOT the
     * reliable op sub-channel. Appended last on purpose, same wire-byte
     * caveat as MSG_OP -- enumerators are never reordered or renumbered, and
     * an unknown type is already silently ignored by every drain loop, so no
     * PROTOCOL_VERSION bump is needed. */
    MSG_CURSOR,
    /* S8.7f1: one chunk of a full-gamedata structural resync (see ResyncChunk
     * below). Structural edits (blueprints, rules, atlas, animation, levels,
     * tiles) have no fine-grained wire encoding by design, so they propagate
     * as a coarse full-gamedata re-emit the host streams to every client one
     * chunk per packet. Appended last on purpose, same wire-byte caveat as
     * MSG_OP/MSG_CURSOR -- enumerators are never reordered or renumbered, and
     * an unknown type is already silently ignored by every drain loop, so no
     * PROTOCOL_VERSION bump is needed. */
    MSG_RESYNC,
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

/* Field order (name, value, entity_id, type -- not declaration order
 * entity_id/name/type/value) is deliberate, not cosmetic: name and value
 * are both 16 bytes with 8-byte alignment (Strv/AttrRecordValue each embed
 * a pointer), so grouping them first and packing the two 4-byte fields
 * (entity_id, type) at the end leaves zero padding bytes. The
 * entity_id/name/type/value order clang-analyzer's optin.performance.Padding
 * check flagged left 8 padding bytes (4 after entity_id, 4 after type) --
 * every construction site already uses designated initializers
 * (net_session.c, net_protocol.c, net_protocol_test.c), so this reorder
 * changes memory layout only, never call-site behavior. */
typedef struct {
    Strv name;
    AttrRecordValue value;
    int32_t entity_id;
    AttrType type;
} AttrRecord;

[[nodiscard]] bool protocol_encode_attr_record(PacketWriter *writer, AttrRecord record);
[[nodiscard]] bool protocol_decode_attr_record(PacketReader *reader, AttrRecord *out);

/* Dynamically-sized collection of AttrRecords (S8.4b): the count a host
 * needs to sync -- every entity's position plus instance attrs, see
 * net_session.h's own doc comment -- is bounded by level content, not by
 * frame count or any fixed cap, so a vec (not a MAX_*-sized array) is the
 * right shape here per CLAUDE.md's vec-over-MAX_* rule. */
VEC_DECL(attr_record, AttrRecord)

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

/* ---- MSG_JOIN_ACCEPT: the S8.6 reply half of the JOIN handshake.
 * Sent by the host once (network.c's handle_join_datagram) for every
 * MSG_JOIN it accepts (a hash match, whether a brand-new registration or a
 * re-JOIN refresh -- see register_client's own doc comment), so a client
 * that resends JOIN while still waiting eventually gets an accept even if
 * the first one was lost. Never sent for a refused (hash-mismatched) JOIN
 * -- a client that never receives one simply never leaves NET_JOINING,
 * same silent-refusal contract protocol_join_verify already documents.
 *
 * op_seq_baseline (S8.7c) is the next op_seq the accepting host will stamp
 * -- i.e. the first editor-op echo the joining client should expect. The
 * client seeds its own next_expected_op_seq from it exactly once, on the
 * JOINING->CLIENT flip edge (net_session.c's accept branch), so a client
 * joining a session where ops were already stamped, or rejoining a running
 * session, starts in sync with the host's counter instead of at a stale or
 * mismatched value. Re-sent (fresh, current) on every re-JOIN, same as the
 * accept itself. Appended after player_id on the wire; no PROTOCOL_VERSION
 * bump, same added-field rationale MSG_OP's own doc comment gives. ---- */

typedef struct {
    int32_t player_id;
    uint32_t op_seq_baseline;
} JoinAcceptMessage;

[[nodiscard]] bool protocol_encode_join_accept(PacketWriter *writer, JoinAcceptMessage message);
[[nodiscard]] bool protocol_decode_join_accept(PacketReader *reader, JoinAcceptMessage *out);

/* ---- MSG_OP: a single editor operation (S8.7).
 *
 * The collaborative editor propagates scene edits as discrete ops between
 * peers. This slice defines only the wire format; later slices ride these
 * ops on the reliable sub-channel and apply them to game state. ---- */

/* Wire-encoded as a single u8. Appended-to only, same wire-byte caveat as
 * MessageType: enumerators are never reordered or renumbered.
 *
 * S8.7d1 lock kinds (all header-only payloads, exactly like
 * EDITOR_OP_DELETE_ENTITY):
 *  - EDITOR_OP_LOCK_ACQUIRE -- client->host: request to lock entity_id;
 *    host->clients echo: the lock was GRANTED to author_player_id.
 *  - EDITOR_OP_LOCK_RELEASE -- client->host: release my lock; host->clients
 *    echo: the lock is gone (also emitted by the host on silence timeout,
 *    author = the former holder).
 *  - EDITOR_OP_LOCK_DENY -- host->clients echo only (clients never
 *    originate it): author_player_id's acquire on entity_id was refused.
 *    Broadcast like every other echo, so every replica observes it in the
 *    one same total order; only the named requester acts on it. */
typedef enum {
    EDITOR_OP_MOVE_ENTITY,
    EDITOR_OP_SET_ATTR,
    EDITOR_OP_DELETE_ENTITY,
    EDITOR_OP_LOCK_ACQUIRE,
    EDITOR_OP_LOCK_RELEASE,
    EDITOR_OP_LOCK_DENY,
} EditorOpKind;

/* A single editor op. Flat fields (not a union), matching EventRecord's
 * flat style; `kind` selects which payload fields are meaningful.
 *
 * level_name is mandatory: entity ids are per-level (level.h's
 * Level.next_entity_id), so an op must name the level its entity_id lives
 * in -- a peer may be editing a level other than the current one.
 *
 * op_seq is the HOST-assigned global serialization number. A client's op
 * REQUEST carries 0; the host stamps the real value on its echo -- later
 * slices implement that. This slice just carries the field.
 *
 * move_x/move_y are meaningful only for EDITOR_OP_MOVE_ENTITY. attr is
 * meaningful only for EDITOR_OP_SET_ATTR, and the embedded record's own
 * entity_id mirrors this header's entity_id: the encoder overwrites it from
 * entity_id before writing (see point 4 / protocol_encode_op_packet), so
 * the two can never disagree on the wire. EDITOR_OP_DELETE_ENTITY and the
 * three S8.7d1 lock kinds (LOCK_ACQUIRE/RELEASE/DENY) have no payload beyond
 * the header.
 *
 * Decoded Strv fields (level_name, and any string inside attr) view into
 * the caller's packet buffer, with the same lifetime contract every other
 * decoded string field in this header documents (see the Ownership note at
 * the top of this file). */
typedef struct {
    EditorOpKind kind;
    Strv level_name;
    int32_t entity_id;
    int32_t author_player_id;
    uint32_t op_seq;
    float move_x;
    float move_y;
    AttrRecord attr;
} EditorOp;

/* Decodes an MSG_OP payload after the caller already decoded the header
 * (same division of labor as protocol_decode_event). Rejects an unknown
 * kind byte (anything > EDITOR_OP_LOCK_DENY) and fails closed on any
 * truncated read. */
[[nodiscard]] bool protocol_decode_op(PacketReader *reader, EditorOp *out);

/* ---- MSG_CURSOR: editor presence records (S8.7e).
 *
 * One record per editing peer: its editor cursor position, the entity it
 * currently has selected (-1 for none), and its display name. A MSG_CURSOR
 * payload is a LIST of these -- a u8 count, then that many records. Both
 * directions use the same shape: a client sends a one-record list (itself),
 * the host relays a list of every peer's latest.
 *
 * Fire-and-forget contract: MSG_CURSOR carries soft, high-frequency,
 * loss-tolerant state, so there is NO seq dedup and NO ordering guarantee.
 * A reordered stale packet briefly regresses a cursor and self-corrects on
 * the next tick -- exactly the SNAPSHOT/DELTA rationale (which also re-sends
 * full state every tick rather than reliably diffing). See MSG_CURSOR's own
 * doc comment on MessageType above.
 *
 * The decoded `name` Strv views into the reader's own buffer, with the same
 * lifetime contract every other decoded string field in this header
 * documents (see the Ownership note at the top of this file). */
typedef struct {
    int32_t player_id;
    float cursor_x;
    float cursor_y;
    int32_t selected_entity_id;
    Strv name;
} PresenceRecord;

/* Decode an MSG_CURSOR payload after the caller already decoded the header
 * (same division of labor as protocol_decode_attr_list). Fills out_records
 * (caller-owned, length cap) and fails outright -- rather than truncating --
 * if the wire count exceeds cap, mirroring protocol_decode_attr_list. Fails
 * closed on any truncated read. */
[[nodiscard]] bool
protocol_decode_cursor(PacketReader *reader, PresenceRecord *out_records, size_t cap, size_t *out_count);

/* ---- MSG_RESYNC: one chunk of a full-gamedata structural resync (S8.7f1).
 *
 * A structural edit re-emits the whole gamedata as TOML; the host streams
 * those bytes to every client one chunk per packet, and each client
 * reassembles the full buffer, null-terminates it, and reloads its own
 * GameState from it -- both sides re-parse the same bytes so per-level entity
 * ids and the gamedata content hash re-converge on every peer.
 *
 * Fire-and-forget repeat-set, NOT a reliable channel: the host re-sends the
 * complete chunk set on a timer until each client confirms (network.h's
 * resync bookkeeping), so any dropped chunk is simply refilled by the next
 * sweep -- no per-chunk seq/ack/resend machinery needed. total_bytes is the
 * whole emitted TOML length; chunk_index/chunk_count place this chunk within
 * it; current_level_name is the host's current level (the client reloads onto
 * it); payload is this chunk's slice of the TOML bytes (NETWORK_RESYNC_CHUNK_BYTES
 * for every chunk but the last, which carries the remainder). Both
 * current_level_name and payload are length-prefixed strings on the wire; the
 * decoded Strv views point into the reader's own buffer, same lifetime
 * contract every other decoded string field in this header documents. ---- */
#define NETWORK_RESYNC_CHUNK_BYTES 1024

typedef struct {
    uint32_t generation;
    uint32_t total_bytes;
    uint16_t chunk_index;
    uint16_t chunk_count;
    Strv current_level_name;
    Strv payload;
} ResyncChunk;

/* Encode one resync chunk as a whole MSG_RESYNC packet into a caller-owned
 * buffer, reporting total bytes written via out_len. Fails closed on any
 * bounds overflow, like every other protocol_encode_*_packet. */
[[nodiscard]] bool protocol_encode_resync_packet(
    uint8_t *buffer, size_t capacity, uint32_t seq, const ResyncChunk *chunk, size_t *out_len);

/* Decode an MSG_RESYNC payload after the caller already decoded the header
 * (same division of labor as protocol_decode_cursor). The decoded
 * current_level_name/payload Strv views point into the reader's own buffer.
 * Fails closed on any truncated read. */
[[nodiscard]] bool protocol_decode_resync(PacketReader *reader, ResyncChunk *out);

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
[[nodiscard]] bool protocol_encode_join_accept_packet(
    uint8_t *buffer, size_t capacity, uint32_t seq, JoinAcceptMessage message, size_t *out_len);
/* Takes the op by pointer (EditorOp is larger than EventRecord). Fails
 * closed on any bounds overflow, like every other protocol_encode_*_packet. */
[[nodiscard]] bool
protocol_encode_op_packet(uint8_t *buffer, size_t capacity, uint32_t seq, const EditorOp *operation, size_t *out_len);
/* S8.7e: encode a presence-record list as one MSG_CURSOR packet. `count` is
 * capped at UINT8_MAX (the u8 wire count); fails closed on any bounds
 * overflow, like every other protocol_encode_*_packet. */
[[nodiscard]] bool protocol_encode_cursor_packet(
    uint8_t *buffer, size_t capacity, uint32_t seq, const PresenceRecord *records, size_t count, size_t *out_len);

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
