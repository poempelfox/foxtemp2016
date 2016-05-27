/* $Id: sht31.c $
 * Functions for reading the SHT 31 temperature / humidity sensor
 *
 * Also includes functions for bitbanging I2C over two pins of the AVR,
 * which mostly come from the old NTP LED clock project.
 */

#include <avr/io.h>
#include <inttypes.h>
#include <util/delay.h>
#include "sht31.h"

/* The Port used for the connection */
#define BBTWIPORT PORTA
#define BBTWIPIN PINA
#define BBTWIDDR DDRA

/* Which pins of the port */
#define SDAPIN PA1
#define SCLPIN PA0

#define I2C_READ  0x01
#define I2C_WRITE 0x00

/* The I2C address of the sensor.
 * If the addr pin of the sensor is pulled high, it's 0x45 instead. */
#define SHT31_I2C_ADDR (0x44 << 1)

/* Commands for the sensor. The sensor supports a bunch
 * of measuring modes: oneshot or periodic, with clock
 * stretching (pulling SCL low until measurement is
 * completed) or with out, ... Only the ones we are likely
 * to use are listed here, look up the rest in the data sheet. */
/* MSB */
#define SHT31_ONESHOT_NOCS      0x24
#define SHT31_ONESHOT_CS        0x2c
#define SHT31_READSTATUSREG_MSB 0xf3
/* LSB */
#define SHT31_ONESHOT_NOCS_HIGREP  0x00
#define SHT31_ONESHOT_NOCS_MEDREP  0x0b
#define SHT31_ONESHOT_NOCS_LOWREP  0x16
#define SHT31_ONESHOT_CS_HIGREP    0x06
#define SHT31_ONESHOT_CS_MEDREP    0x0d
#define SHT31_ONESHOT_CS_LOWREP    0x10
#define SHT31_READSTATUSREG_LSB    0x2d

/* FIXME */
/* FIXME
 *  */
#define DELAYVAL 14

void sht31_init(void)
{
  /* Initialize I2C bus: Enable pullups. */
  BBTWIPORT |= _BV(SCLPIN);
  BBTWIPORT |= _BV(SDAPIN);
  /* There is nothing to initialize at the SHT31 really. */
}

/* Send START, defined as high-to-low SDA with SCL high.
 * Expects SCL and SDA to be high already (pullups on)!
 * Returns with SDA and SCL actively pulled low. */
static void bbtwi_start(void) {
  /* Change to output mode. */
  BBTWIDDR |= _BV(SDAPIN);
  BBTWIDDR |= _BV(SCLPIN);
  /* change SDA to low */
  BBTWIPORT &= (uint8_t)~_BV(SDAPIN);
  _delay_loop_1(DELAYVAL);
  /* and SCL too */
  BBTWIPORT &= (uint8_t)~_BV(SCLPIN);
  _delay_loop_1(DELAYVAL);
}

/* Send STOP, defined as low-to-high SDA with SCL high.
 * Expects SCL and SDA to be low already!
 * Returns with SDA and SCL high. */
static void bbtwi_stop(void) {
  /* Set SCL */
  BBTWIPORT |= _BV(SCLPIN);
  _delay_loop_1(DELAYVAL);
  /* Set SDA */
  BBTWIPORT |= _BV(SDAPIN);
  _delay_loop_1(DELAYVAL);
  /* Probably safer to tristate the bus */
  BBTWIDDR &= (uint8_t)~_BV(SDAPIN);
  BBTWIDDR &= (uint8_t)~_BV(SCLPIN);
}

/* Transmits the byte in what.
 * Returns 1 if the byte was ACKed, 0 if not.
 * Expects SCL to be driven low and SDA to be driven whatever way already!
 * Returns with SCL and SDA driven low. */
static uint8_t bbtwi_transmit_byte(uint8_t what) {
  uint8_t i;
  for (i = 0; i < 8; i++) {
    /* First put data on the bus */
    if (what & 0x80) {
      BBTWIPORT |= _BV(SDAPIN);
    }
    _delay_loop_1(DELAYVAL);
    /* Then set SCL high */
    BBTWIPORT |= _BV(SCLPIN);
    _delay_loop_1(DELAYVAL);
    /* Take SCL back */
    BBTWIPORT &= (uint8_t)~_BV(SCLPIN);
    _delay_loop_1(DELAYVAL);
    /* And SDA too */
    BBTWIPORT &= (uint8_t)~_BV(SDAPIN);
    _delay_loop_1(DELAYVAL);
    what <<= 1;
  }
  /* OK that was the data, now we read back the ACK */
  /* We need to tristate SDA for that */
  BBTWIPORT |= _BV(SDAPIN);
  BBTWIDDR &= (uint8_t)~_BV(SDAPIN);
  /* Give the device some time */
  _delay_loop_1(DELAYVAL);
  /* Then set SCL high */
  BBTWIPORT |= _BV(SCLPIN);
  _delay_loop_1(DELAYVAL);
  i = BBTWIPIN & _BV(SDAPIN); /* Read ACK */
  /* Take SCL back */
  BBTWIPORT &= (uint8_t)~_BV(SCLPIN);
  _delay_loop_1(DELAYVAL);
  /* No more tristate, we pull SDA again */
  BBTWIPORT &= (uint8_t)~_BV(SDAPIN);
  BBTWIDDR |= _BV(SDAPIN);
  _delay_loop_1(DELAYVAL);
  return (i == 0);
}

/* Reads a byte from the bus and returns it.
 * expects to start with SCL actively driven low.
 */
static uint8_t bbtwi_read_byte(uint8_t sendack)
{
  uint8_t res = 0;
  uint8_t i;
  /* Make sure SDA is pulled up high but not actively driven */
  BBTWIPORT |= _BV(SDAPIN);
  BBTWIDDR &= (uint8_t)~_BV(SDAPIN);
  for (i = 0; i < 8; i++) {
    /* Raise the clock line */
    BBTWIPORT |= _BV(SCLPIN);
    /* Wait for the slave to pull the data line */
    _delay_loop_1(DELAYVAL);
    res <<= 1;
    if (BBTWIPIN & _BV(SDAPIN)) {
      res |= 0x01;
    }
    BBTWIPORT &= (uint8_t)~_BV(SCLPIN);
    _delay_loop_1(DELAYVAL);
  }
  if (sendack) {
    /* Alrighty then, we should send an ACK: drive low SDA. */
    BBTWIPORT &= (uint8_t)~_BV(SDAPIN);
  }
  BBTWIDDR |= _BV(SDAPIN);
  _delay_loop_1(DELAYVAL);
  BBTWIPORT |= _BV(SCLPIN);
  /* Wait for the slave to pull the data line */
  _delay_loop_1(DELAYVAL);
  BBTWIPORT &= (uint8_t)~_BV(SCLPIN);
  _delay_loop_1(DELAYVAL);
  BBTWIPORT |= _BV(SDAPIN);
  BBTWIDDR &= (uint8_t)~_BV(SDAPIN);
  _delay_loop_1(DELAYVAL);
  return res;
}

void sht31_startmeas(void)
{
  bbtwi_start();
  bbtwi_transmit_byte(SHT31_I2C_ADDR | I2C_WRITE);
  /* single shot, high repeatability, no 'clock stretch' */
  bbtwi_transmit_byte(SHT31_ONESHOT_NOCS);
  bbtwi_transmit_byte(SHT31_ONESHOT_NOCS_HIGREP);
  bbtwi_stop();
}

/* This function is based on Sensirons example code and datasheet */
uint8_t sht31_crc(uint8_t b1, uint8_t b2)
{
  uint8_t crc = 0xff; /* Start value */
  uint8_t b;
  crc ^= b1;
  for (b = 0; b < 8; b++) {
    if (crc & 0x80) {
      crc = (crc << 1) ^ 0x131;
    } else {
      crc = crc << 1;
    }
  }
  crc ^= b2;
  for (b = 0; b < 8; b++) {
    if (crc & 0x80) {
      crc = (crc << 1) ^ 0x131;
    } else {
      crc = crc << 1;
    }
  }
  return crc;
}

void sht31_read(struct sht31data * d)
{
  d->valid = 0;
  bbtwi_start();
  /* There is no "command", just addressing the device while indicating a */
  /* read. The device will reply with NAK if it has not finished yet. */
  if (!bbtwi_transmit_byte(SHT31_I2C_ADDR | I2C_READ)) {
    bbtwi_stop();
    return;
  }
  uint8_t b1 = bbtwi_read_byte(1);  /* Temp MSB */
  uint8_t b2 = bbtwi_read_byte(1);  /* Temp LSB */
  uint8_t b3 = bbtwi_read_byte(1);  /* Temp CRC */
  uint8_t b4 = bbtwi_read_byte(1);  /* Humi MSB */
  uint8_t b5 = bbtwi_read_byte(1);  /* Humi LSB */
  uint8_t b6 = bbtwi_read_byte(0);  /* Humi CRC */
  if ((sht31_crc(b1, b2) == b3) && (sht31_crc(b4, b5) == b6)) {
    d->valid = 1;
  }
  d->temp = (b1 << 8) | b2;
  d->hum = (b4 << 8) | b5;
  bbtwi_stop();
}
