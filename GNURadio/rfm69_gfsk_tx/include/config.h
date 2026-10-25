//**VERY IMPORTANT FILE IF YOU WANT TO CHANGE ANYTHING WITH RADIOS OR HARDWARE
#pragma once

// Wiring                     Pico 2       RFM69HCW pin
#define PIN_MISO      16   // SPI0 RX  ->  MISO
#define PIN_CS        17   // SPI0 CSn ->  NSS / CS
#define PIN_SCK       18   // SPI0 SCK ->  SCK
#define PIN_MOSI      19   // SPI0 TX  ->  MOSI
#define PIN_RESET     20   //          ->  RESET (active high)
#define PIN_DCLK      21   //          <-  DIO1  (bit clock out of the radio)
#define PIN_DATA      22   //          ->  DIO2  (NRZ data into the modulator)
#define RFM_SPI       spi0
#define RFM_SPI_HZ    (4 * 1000 * 1000)   // RFM69 is fine up to 10 MHz

//Actual Radio config
#define RF_FREQ_HZ      915000000u   
#define RF_BITRATE_BPS  50000u       
#define RF_FDEV_HZ      25000u       // h = 2*Fdev/BR = 1.0 here

#define RF_GAUSSIAN_BT  2


#define RF_TX_POWER_DBM 0 //output **ANTENNA REMINDER**
#define RFM69_HIGH_POWER 1   

//Starting in PRBS
#define BOOT_PATTERN    'p'
#define BOOT_TX_ON      1    