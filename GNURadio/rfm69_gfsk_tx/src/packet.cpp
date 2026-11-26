// packet.cpp - implementation of the IDLE/SENDING packet state machine.
// Read the big comment at the top of include/packet.h first; it shows the frame
// layout and the state diagram this file implements.
#include <Arduino.h>      // for time helpers (absolute_time_t, make_timeout_time_ms...)
#include <string.h>       // memcpy
#include "packet.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// The two states. An enum class keeps the names from leaking into the global
// namespace and stops us from comparing a state to a plain integer by accident.
enum class PktState : uint8_t {
    IDLE,      // sending 1010... and waiting for the 1 s timer
    SENDING,   // handing out words from pkt_buf until it's empty
};

static PktState state = PktState::IDLE;

// The whole packet (preamble .. pad) is built into this byte buffer in one go
// when the timer fires. Then packet_next_word() reads it 4 bytes at a time.
// Size: preamble 4 + sync 4 + len 1 + payload max + crc 2 + pad up to 3.
static uint8_t pkt_buf[PKT_PREAMBLE_BYTES + 4 + 1 + PKT_MAX_PAYLOAD + 2 + 3];
static size_t  pkt_len = 0;   // total bytes in pkt_buf (always a multiple of 4)
static size_t  pkt_pos = 0;   // index of the next byte to send

static absolute_time_t next_packet_at;   // when the next packet should start
static uint16_t seq = 0;                 // increments once per packet
static uint32_t sent = 0;                // packets started since boot

// ---------------------------------------------------------------------------
// CRC-16/CCITT-FALSE
// ---------------------------------------------------------------------------
// poly 0x1021, init 0xFFFF, no reflection, no final XOR.
// Check value: crc16("123456789") == 0x29B1. The Python receiver uses the exact
// same algorithm (and its test checks that value).
// A plain bit-by-bit loop is fine here: we run it on ~9 bytes once a second.
static uint16_t crc16_ccitt(const uint8_t *data, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)data[i] << 8;           // bring the next byte into the top
        for (int b = 0; b < 8; b++) {
            // If the top bit is 1, shift and XOR in the polynomial; otherwise just shift.
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Building one packet
// ---------------------------------------------------------------------------
// Called once, at the IDLE -> SENDING transition. Fills pkt_buf and resets
// pkt_pos so packet_next_word() starts from the first preamble byte.
static void build_packet(void) {
    size_t i = 0;

    // 1) Preamble: 0xAA bytes (10101010).
    for (int k = 0; k < PKT_PREAMBLE_BYTES; k++) pkt_buf[i++] = 0xAA;

    // 2) Sync word, big-endian so its MSB goes out first.
    pkt_buf[i++] = (uint8_t)(PKT_SYNC_WORD >> 24);
    pkt_buf[i++] = (uint8_t)(PKT_SYNC_WORD >> 16);
    pkt_buf[i++] = (uint8_t)(PKT_SYNC_WORD >> 8);
    pkt_buf[i++] = (uint8_t)(PKT_SYNC_WORD);

    // 3) Payload is built in a small scratch array first, so we know its length
    //    before we write the LEN byte.
    static const char text[] = "HOPERF";      // sizeof includes the '\0' terminator
    uint8_t payload[PKT_MAX_PAYLOAD];
    size_t  plen = 0;
    payload[plen++] = (uint8_t)(seq >> 8);     // sequence number, high byte first
    payload[plen++] = (uint8_t)(seq);
    memcpy(&payload[plen], text, sizeof(text) - 1);   // copy the text without its '\0'
    plen += sizeof(text) - 1;

    // 4) LEN byte, then the payload itself.
    size_t crc_start = i;                       // CRC covers LEN + PAYLOAD
    pkt_buf[i++] = (uint8_t)plen;
    memcpy(&pkt_buf[i], payload, plen);
    i += plen;

    // 5) CRC over LEN + PAYLOAD, big-endian.
    uint16_t crc = crc16_ccitt(&pkt_buf[crc_start], i - crc_start);
    pkt_buf[i++] = (uint8_t)(crc >> 8);
    pkt_buf[i++] = (uint8_t)(crc);

    // 6) Pad with 0xAA up to a whole number of 32-bit words. The pad looks like
    //    idle to the receiver, so it simply goes back to hunting for sync.
    while (i % 4) pkt_buf[i++] = 0xAA;

    pkt_len = i;
    pkt_pos = 0;
    seq++;       // next packet gets the next number (wraps 65535 -> 0)
    sent++;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void packet_reset(void) {
    state = PktState::IDLE;
    pkt_pos = pkt_len = 0;
    // First packet goes out 1 s from now. That gives the receiver a full second
    // of 1010 idle to settle its frequency offset estimate and clock loop.
    next_packet_at = make_timeout_time_ms(PKT_INTERVAL_MS);
}

uint32_t packet_next_word(void) {
    switch (state) {
    case PktState::IDLE:
        // Is it time for the next packet yet?
        if (!time_reached(next_packet_at)) {
            return PKT_IDLE_WORD;               // no: keep sending 1010...
        }
        // Yes. Schedule the following packet relative to the *planned* time
        // (not "now"), so small delays don't add up and the rate stays exactly
        // 1 per second on average.
        next_packet_at = delayed_by_ms(next_packet_at, PKT_INTERVAL_MS);
        build_packet();
        state = PktState::SENDING;
        // Fall through so this call already returns the first packet word.
        [[fallthrough]];

    case PktState::SENDING: {
        // Pack 4 bytes into one word, first byte in the top 8 bits, because the
        // PIO shifts out MSB first.
        uint32_t w = ((uint32_t)pkt_buf[pkt_pos]     << 24) |
                     ((uint32_t)pkt_buf[pkt_pos + 1] << 16) |
                     ((uint32_t)pkt_buf[pkt_pos + 2] << 8)  |
                     ((uint32_t)pkt_buf[pkt_pos + 3]);
        pkt_pos += 4;
        if (pkt_pos >= pkt_len) state = PktState::IDLE;   // packet finished
        return w;
    }
    }
    return PKT_IDLE_WORD;   // not reached; keeps the compiler happy
}

uint16_t packet_last_seq(void) { return (uint16_t)(seq - 1); }
uint32_t packet_count(void)    { return sent; }
