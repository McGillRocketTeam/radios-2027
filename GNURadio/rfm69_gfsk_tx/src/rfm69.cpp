// rfm69.cpp - minimal RFM69 driver for continuous-mode GFSK transmit.
#include <Arduino.h>
#include "rfm69.h"
#include "config.h"

#include <math.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

//software chip select during SPI(inline just means #define for functions)
static inline void cs_low(void)  { asm volatile("nop \n nop"); gpio_put(PIN_CS, 0); asm volatile("nop \n nop"); } 
static inline void cs_high(void) { asm volatile("nop \n nop"); gpio_put(PIN_CS, 1); asm volatile("nop \n nop"); }

//W/R bit then 7-bit address followed by a second byte of data
uint8_t rfm69_read(uint8_t reg) {
    uint8_t tx[2] = { (uint8_t)(reg & 0x7F), 0x00 };
    uint8_t rx[2];
    cs_low();
    spi_write_read_blocking(RFM_SPI, tx, rx, 2); //reading and writing at same time
    cs_high();
    return rx[1];
}

void rfm69_write(uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { (uint8_t)(reg | 0x80), val };
    cs_low();
    spi_write_blocking(RFM_SPI, tx, 2); //just writing any data in is thrown away
    cs_high();
}

bool rfm69_init(void) {
    spi_init(RFM_SPI, RFM_SPI_HZ);
    spi_set_format(RFM_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST); //setting config zero which is a standard spi comms
    //configuring pins
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI); 
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // RESET is active high: >100 us pulse, then 5 ms before talking to it.
    gpio_init(PIN_RESET);
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_put(PIN_RESET, 1);
    sleep_us(200);
    gpio_put(PIN_RESET, 0);
    sleep_ms(10);

    uint8_t v = rfm69_read(REG_VERSION);//access reg 0x10, this is validation step
    Serial.printf("RFM69 RegVersion = 0x%02X (expect 0x24)\n", v);
    return v == 0x24;//expecting this version
}

//this sets the IRQ being mode ready, tx ready
bool rfm69_set_mode(uint8_t mode) {
    rfm69_write(REG_OPMODE, mode);
    uint8_t want = IRQ1_MODEREADY | (mode == OPMODE_TX ? IRQ1_TXREADY : 0);
    absolute_time_t deadline = make_timeout_time_ms(50);
    while ((rfm69_read(REG_IRQFLAGS1) & want) != want) {
        if (time_reached(deadline)) {
            Serial.printf("RFM69: timeout entering mode 0x%02X (IrqFlags1=0x%02X)\n",
                   mode, rfm69_read(REG_IRQFLAGS1));
            return false;
        }
    }
    return true;
}

//checks 3 states -2 to 13, 14 to 17 and -18 to 13 and sets PA accordingly
static uint8_t pa_level(int power_dbm, bool high_power) {
    if (high_power) {
        // RFM69HCW: PA0 is not bonded to the antenna pin -> must use PA1/PA2.
        if (power_dbm < -2)  power_dbm = -2;
        if (power_dbm > 17)  power_dbm = 17;
        if (power_dbm <= 13) return 0x40 | (uint8_t)(power_dbm + 18);  // PA1:      -18 + OutputPower
        return 0x60 | (uint8_t)(power_dbm + 14);                       // PA1+PA2:  -14 + OutputPower
    }
    if (power_dbm < -18) power_dbm = -18;
    if (power_dbm > 13)  power_dbm = 13;
    return 0x80 | (uint8_t)(power_dbm + 18);                           // PA0
}

//this is the combination of rfm69 and config headers and is setting up everything
void rfm69_configure(const rfm69_cfg_t *c) {
    rfm69_set_mode(OPMODE_STANDBY); //always configure when quiet not while in TX

    // DataMode = 10 (continuous, bit synchronizer on -> DCLK valid),
    // ModulationType = 00 (FSK), shaping = Gaussian BT.
    rfm69_write(REG_DATAMODUL, 0x40 | (c->gaussian_bt & 0x03));

    uint32_t br = (uint32_t)lround(RFM69_FXOSC / c->bitrate_bps);
    rfm69_write(REG_BITRATEMSB, br >> 8);
    rfm69_write(REG_BITRATELSB, br & 0xFF);

    uint32_t fd = (uint32_t)lround(c->fdev_hz / RFM69_FSTEP);
    rfm69_write(REG_FDEVMSB, (fd >> 8) & 0x3F);
    rfm69_write(REG_FDEVLSB, fd & 0xFF);

    uint32_t frf = (uint32_t)llround(c->freq_hz / RFM69_FSTEP);
    rfm69_write(REG_FRFMSB, (frf >> 16) & 0xFF);
    rfm69_write(REG_FRFMID, (frf >> 8) & 0xFF);
    rfm69_write(REG_FRFLSB, frf & 0xFF);   // Frf is latched on the LSB write

    // Make sure the +20 dBm boost is off (continuous TX!).
    rfm69_write(REG_TESTPA1, 0x55);
    rfm69_write(REG_TESTPA2, 0x70);
    rfm69_write(REG_OCP, 0x1A);            // OCP on, 95 mA (default)
    rfm69_write(REG_PALEVEL, pa_level(c->power_dbm, c->high_power));
    rfm69_write(REG_PARAMP, 0x09);         // 40 us ramp (default)

    // Continuous-mode mapping: DIO1 = DCLK, DIO2 = DATA, DIO0 = 00.
    rfm69_write(REG_DIOMAPPING1, 0x00);
    // ClkOut on DIO5 off -> removes a 1 MHz spur source on the board.
    rfm69_write(REG_DIOMAPPING2, 0x07);

    // Sanity checks from the datasheet.
    double h = 2.0 * c->fdev_hz / c->bitrate_bps;
    if (c->fdev_hz + c->bitrate_bps / 2 > 500000)
        Serial.printf("WARNING: Fdev + BR/2 > 500 kHz (out of spec)\n");
    if (h < 0.5 || h > 10.0)
        Serial.printf("WARNING: modulation index %.2f outside 0.5..10\n", h);
}

double rfm69_actual_bitrate(void) {
    uint16_t br = (rfm69_read(REG_BITRATEMSB) << 8) | rfm69_read(REG_BITRATELSB);
    return RFM69_FXOSC / br;
}

double rfm69_actual_fdev(void) {
    uint16_t fd = ((rfm69_read(REG_FDEVMSB) & 0x3F) << 8) | rfm69_read(REG_FDEVLSB);
    return fd * RFM69_FSTEP;
}

double rfm69_actual_freq(void) {
    uint32_t frf = ((uint32_t)rfm69_read(REG_FRFMSB) << 16) |
                   ((uint32_t)rfm69_read(REG_FRFMID) << 8) | rfm69_read(REG_FRFLSB);
    return frf * RFM69_FSTEP;
}

void rfm69_dump_regs(void) {
    for (uint8_t r = 0x01; r <= 0x4F; r++) {
        Serial.printf("%02X:%02X%s", r, rfm69_read(r), (r % 8 == 0) ? "\n" : "  ");
    }
    Serial.printf("\n");
}
