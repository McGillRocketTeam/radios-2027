// this is the driver for the rfm69
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Represents registers adjusted for configuring code
#define REG_OPMODE        0x01 
#define REG_DATAMODUL     0x02
#define REG_BITRATEMSB    0x03
#define REG_BITRATELSB    0x04
#define REG_FDEVMSB       0x05
#define REG_FDEVLSB       0x06
#define REG_FRFMSB        0x07
#define REG_FRFMID        0x08
#define REG_FRFLSB        0x09
#define REG_VERSION       0x10
#define REG_PALEVEL       0x11
#define REG_PARAMP        0x12
#define REG_OCP           0x13
#define REG_DIOMAPPING1   0x25
#define REG_DIOMAPPING2   0x26
#define REG_IRQFLAGS1     0x27
#define REG_TESTPA1       0x5A
#define REG_TESTPA2       0x5C

//4 types of operation
#define OPMODE_SLEEP      0x00
#define OPMODE_STANDBY    0x04
#define OPMODE_FS         0x08
#define OPMODE_TX         0x0C

//where interrupt requests occur from
#define IRQ1_MODEREADY    0x80 
#define IRQ1_TXREADY      0x20
#define IRQ1_PLLLOCK      0x10 

//define the oscillation and scalling of oscillator
#define RFM69_FXOSC       32000000.0
#define RFM69_FSTEP       (RFM69_FXOSC / 524288.0)   // 2^19 -> 61.035 Hz

typedef struct {
    uint32_t freq_hz;
    uint32_t bitrate_bps;
    uint32_t fdev_hz;
    uint8_t  gaussian_bt;   // 0..3, see config.h
    int8_t   power_dbm;
    bool     high_power;
} rfm69_cfg_t;

bool    rfm69_init(void);                       // reset radio and initilaize over SPI
uint8_t rfm69_read(uint8_t reg); //To configure a single register use read and write
void    rfm69_write(uint8_t reg, uint8_t val);
void    rfm69_configure(const rfm69_cfg_t *cfg); // continuous GFSK, DIO1=DCLK, DIO2=DATA
bool    rfm69_set_mode(uint8_t mode);           // waits for ModeReady (and TxReady in TX)
double  rfm69_actual_bitrate(void); //sets bitrate
double  rfm69_actual_fdev(void); //sets deviation freq
double  rfm69_actual_freq(void); //sets center freq
void    rfm69_dump_regs(void); //prints out the registers
