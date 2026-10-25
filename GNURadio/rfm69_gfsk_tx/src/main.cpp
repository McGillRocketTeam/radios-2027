
// The radio is put in continuous mode then it clocks bits out on DIO1 (DCLK) and modulates whatever is on DIO2 (DATA)
//
// USB serial commands (115200, any terminal):
//   p  PN9 PRBS                 r  random hardware range
//   a  1010... (shows +/-Fdev)  c  carrier all zeros
//   t  transmit                 s  standby (stop TX)
//   d  dump registers           
#include <Arduino.h>
#include "hardware/pio.h"

#include "config.h"
#include "rfm69.h"
#include "dclk_tx.pio.h"   // generated from src/dclk_tx.pio, see README


//one state machine
static PIO  pio = pio0;
static uint sm;

static char     pattern = BOOT_PATTERN;
static bool     tx_on   = false;
static uint16_t pn9     = 0x1FF;      // any non-zero seed
static uint64_t words_pushed = 0;

static absolute_time_t next_report;
static uint64_t last_bits = 0;
static uint32_t blink = 0;

//All 32-bit words 
static uint32_t next_word(void) {
    switch (pattern) {
    case 'r': return rp2040.hwrand32(); //fully random sequence
    case 'a': return 0xAAAAAAAAu; //0x1010101010
    case 'c': return 0x00000000u; //0x0000000000
    case 'p':
    default: { //PRBS 9 x^9 + x^5 + 1
        uint32_t w = 0;
        for (int i = 0; i < 32; i++) {
            uint16_t bit = ((pn9 >> 8) ^ (pn9 >> 4)) & 1;   // taps 9 and 5
            pn9 = ((pn9 << 1) | bit) & 0x1FF;
            w = (w << 1) | bit;
        }
        return w;
    }
    }
}

static const char *pattern_name(char p) { //just for output to serial monitor
    switch (p) {
    case 'r': return "random";
    case 'a': return "1010 alternating";
    case 'c': return "carrier (all zeros)";
    default:  return "PN9";
    }
}

static void led(bool on) {
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW); 
}

static void start_tx(void) { 
    pio_sm_clear_fifos(pio, sm); //look into this line
    while (!pio_sm_is_tx_fifo_full(pio, sm)) { pio_sm_put(pio, sm, next_word()); words_pushed++; }//loads the PIO fifo
    tx_on = rfm69_set_mode(OPMODE_TX);
    Serial.printf(tx_on ? "TX on\n" : "TX failed to start\n");
}

static void stop_tx(void) {
    rfm69_set_mode(OPMODE_STANDBY);
    tx_on = false;
    led(false);
    Serial.printf("Standby\n");
}

static void print_status(void) {
    static const char *bt_names[] = {"none", "1.0", "0.5", "0.3"};
    Serial.printf("\n--- RFM69 GFSK TX ---\n");
    Serial.printf("Freq   %.3f MHz\n", rfm69_actual_freq() / 1e6);
    Serial.printf("Rate   %.1f bps\n", rfm69_actual_bitrate());
    Serial.printf("Fdev   %.1f Hz  (h = %.2f)\n", rfm69_actual_fdev(),
                  2.0 * rfm69_actual_fdev() / rfm69_actual_bitrate());
    Serial.printf("BT     %s\n", bt_names[RF_GAUSSIAN_BT & 3]);
    Serial.printf("Power  %d dBm (%s)\n", RF_TX_POWER_DBM, RFM69_HIGH_POWER ? "PA1/PA2" : "PA0");
    Serial.printf("Data   %s, MSB first\n", pattern_name(pattern));
    Serial.printf("State  %s\n", tx_on ? "TRANSMITTING" : "standby");
    Serial.printf("Keys: p r a c = pattern, t = TX, s = standby, d = dump regs, ? = this\n\n");
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    // Give a USB terminal ~2 s to attach so the boot log isn't lost.
    for (int i = 0; i < 20 && !Serial; i++) delay(100);

    Serial.printf("\nPico 2 RFM69 continuous GFSK transmitter\n");

    while (!rfm69_init()) {
        Serial.printf("No RFM69 found - check SPI wiring, 3.3 V and RESET. Retrying...\n");
        for (int i = 0; i < 10; i++) { led(i & 1); delay(100); }
    }

    rfm69_cfg_t cfg = {
        .freq_hz     = RF_FREQ_HZ,
        .bitrate_bps = RF_BITRATE_BPS,
        .fdev_hz     = RF_FDEV_HZ,
        .gaussian_bt = RF_GAUSSIAN_BT,
        .power_dbm   = RF_TX_POWER_DBM,
        .high_power  = RFM69_HIGH_POWER,
    };
     //initializes the RFM69HCW
    rfm69_configure(&cfg);

    //initializes the state machine in PIO
    uint offset = pio_add_program(pio, &dclk_tx_program);
    sm = pio_claim_unused_sm(pio, true);
    dclk_tx_program_init(pio, sm, offset, PIN_DCLK, PIN_DATA);

    if (BOOT_TX_ON) start_tx();
    print_status();

    next_report = make_timeout_time_ms(2000);
}

//literally all this is doing from the pico side is keep the programable IO pins fifo full withdata
void loop() {
    // Keep the PIO FIFO full. At 50 kbps that's ~1.6k words/s
    while (!pio_sm_is_tx_fifo_full(pio, sm)) {
        pio_sm_put(pio, sm, next_word());
        words_pushed++;
    }
    
    int ch = Serial.read();
    if (ch >= 0) {
        switch (ch) {
        case 'p': case 'r': case 'a': case 'c':
            pattern = (char)ch;
            Serial.printf("Pattern: %s\n", pattern_name(pattern));
            break;
        case 't': start_tx(); break;
        case 's': stop_tx(); break;
        case 'd': rfm69_dump_regs(); break;
        case '?': case '\r': case '\n': print_status(); break;
        default: break;
        }
    }

    if (time_reached(next_report)) {
        next_report = make_timeout_time_ms(2000);
        uint64_t bits = (words_pushed - pio_sm_get_tx_fifo_level(pio, sm)) * 32;
        double bps = (bits - last_bits) / 2.0;
        last_bits = bits;
        if (tx_on) {
            led(++blink & 1);
            Serial.printf("TX %s  measured DCLK ~%.0f bps%s\n", pattern_name(pattern), bps,
                          bps < 1.0 ? "  <-- no DCLK! check DIO1 wiring" : "");
        }
    }
}
