// packet.h - the 'p' pattern: a tiny packet framer that runs as a state machine.
//
// WHAT IT SENDS ON THE AIR
// ------------------------
// The radio is in continuous mode, so it NEVER stops modulating. We always
// have to hand the PIO another 32-bit word. This module decides what that word is:
//
//   ...1010101010 | PREAMBLE | SYNC | LEN | PAYLOAD ...  | CRC | pad | 1010101010...
//      idle          4 bytes   4 B    1 B   LEN bytes      2 B   0-3B    idle
//      (0xAA)        0xAA      1ACFFC1D                         0xAA    (0xAA)
//
//   - IDLE (0xAA = 10101010): sent whenever no packet is in flight. Alternating
//     bits are zero-mean (equal time at +Fdev and -Fdev), so the receiver can
//     estimate and remove any frequency offset, and they hold the
//     receiver's clock recovery in lock because every bit has a transition.
//   - PREAMBLE: 4 more bytes of 0xAA. Idle already looks like preamble, so this
//     is just a guarantee that at least 32 alternating bits come before the sync
//     word, even right after start-up.
//   - SYNC: 0x1ACFFC1D, the CCSDS "attached sync marker". It is a well-studied
//     32-bit word with low autocorrelation sidelobes, so it is very unlikely to
//     show up by accident in idle bits or in a shifted copy of itself. The
//     receiver searches for this word to find where a packet's bytes begin.
//   - LEN: number of payload bytes that follow (not counting CRC).
//   - PAYLOAD: 2-byte big-endian sequence number, then the ASCII text "HOPERF".
//     The sequence number lets the receiver count missed packets.
//   - CRC: CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over LEN + PAYLOAD,
//     sent big-endian. The receiver uses it to throw away corrupted packets.
//   - pad: 0xAA bytes to fill out the last 32-bit word (the PIO eats whole words).
//
// Everything is sent MSB first, the same as the other patterns, because the
// PIO shifts left (see README: sm_config_set_out_shift(&c, false, ...)).
//
// THE STATE MACHINE
// -----------------
//            1 second elapsed: build a packet
//   +------+ ------------------------------> +---------+
//   | IDLE |                                 | SENDING |
//   +------+ <------------------------------ +---------+
//            last word of the packet handed out
//
//   IDLE    : every call returns 0xAAAAAAAA.
//   SENDING : every call returns the next 4 bytes of the packet buffer.
//
// The state machine only advances when the PIO asks for a word, so the bits on
// the air follow the radio's own bit clock (DCLK).
#pragma once
#include <stdint.h>

// Frame constants. If you change any of these, change the matching constants
// at the top of GNURadio/rfm69_packet_rx.py too or the receiver will not lock.
#define PKT_IDLE_WORD      0xAAAAAAAAu   // 1010... between packets
#define PKT_PREAMBLE_BYTES 4             // extra 0xAA bytes before sync
#define PKT_SYNC_WORD      0x1ACFFC1Du   // CCSDS attached sync marker
#define PKT_INTERVAL_MS    1000          // one packet per second
#define PKT_MAX_PAYLOAD    32            // payload buffer size (bytes)

// Reset the state machine to IDLE and start the 1 s timer from "now".
// Call this when the user selects the packet pattern or TX (re)starts.
void packet_reset(void);

// Return the next 32 bits to put on the air (MSB is sent first).
// Call this every time the PIO TX FIFO has room for one more word.
uint32_t packet_next_word(void);

// Sequence number of the most recently *started* packet (for serial status).
uint16_t packet_last_seq(void);

// Number of packets started since boot.
uint32_t packet_count(void);
