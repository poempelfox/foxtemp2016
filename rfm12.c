/* $Id: rfm12.h $
 * Functions for communication with RF12 module
 *
 * This code was heavily copy+pasted from RFMxx.cpp in the FHEM LaCrosseIT+
 * Jeenode Firmware Code.
 */

#include <avr/io.h>
#include <math.h>
#include "rfm12.h"
#include "swserialo.h"

/* Pin mappings:
 *  SS     PB1
 *  MOSI   PA6
 *  MISO   PA5
 *  SCK    PA4
 */
#define RFMDDR   DDRA
#define RFMPIN   PINA
#define RFMPORT  PORTA

#define RFMPIN_SS    PB1
#define RFMPIN_MOSI  PA5
#define RFMPIN_MISO  PA6
#define RFMPIN_SCK   PA4

#define RFM_FREQUENCY 868300ul
#define RFM_DATARATE 9579.0

#define PAYLOADSIZE 64

/* Note: Internal use only. Does not set the SS pin, the calling function
 * has to do that! */
uint8_t rfm12_spi8(uint8_t value) {
  USIDR = value;
  /* transmit */
  for (uint8_t i = 0; i < 8; i++) {
    USICR = _BV(USIWM0) | _BV(USITC);
    USICR = _BV(USIWM0) | _BV(USITC) | _BV(USICLK);
  }
  return USIDR;
}

uint16_t rfm12_spi16(uint16_t value) {
  PORTB &= (uint8_t)~_BV(RFMPIN_SS);
  uint16_t reply = rfm12_spi8(value >> 8) << 8;
  reply |= rfm12_spi8(value);
  PORTB |= _BV(RFMPIN_SS);
  
  return reply;
}

void rfm12_clearfifo(void) {
  for (uint8_t i = 0; i < PAYLOADSIZE; i++) {
    rfm12_spi16(0xB000);
  }
}

void rfm12_settransmitter(uint8_t e) {
  if (e) {
    rfm12_spi16(0x8239);
  } else {
    rfm12_spi16(0x8209);
  }
}

void rfm12_setsleep(uint8_t s) {
  if (s) {
    rfm12_spi16(0x8205);
    RFMDDR &= (uint8_t)~_BV(RFMPIN_MOSI);
    RFMDDR &= (uint8_t)~_BV(RFMPIN_SCK);
    PRR |= _BV(PRUSI);
  } else {
    PRR &= (uint8_t)~_BV(PRUSI);
    RFMDDR |= _BV(RFMPIN_MOSI);
    RFMDDR |= _BV(RFMPIN_SCK);
    USICR = _BV(USIWM0);
    rfm12_spi16(0x8209);
  }
}

static uint16_t rfm12_readstatus(void) {
  return rfm12_spi16(0x0000);
}

/* Wait for ready to send */
static void rfm12_waitforrts(void) {
  while (!(rfm12_readstatus() & 0x8000)) { }
}

void rfm12_sendbyte(uint8_t data) {
  rfm12_waitforrts();
  rfm12_spi16(0xB800 | data);
}

void rfm12_sendarray(uint8_t * data, uint8_t length) {
  rfm12_settransmitter(1);

  // Sync, sync, sync ...
  rfm12_sendbyte(0xAA);
  rfm12_sendbyte(0xAA);
  rfm12_sendbyte(0xAA);
  rfm12_sendbyte(0x2D);
  rfm12_sendbyte(0xD4);
  // Send the data (payload)
  for (int i = 0; i < length; i++) {
    rfm12_sendbyte(data[i]);
  }
  /* Unfortunately, there is no good way of telling when our transmission
   * is really finished, so we have to work around this: We queue a dummy
   * byte for sending, and then wait for ready-to-send again, which we will
   * get as soon as the RFM12 started sending the dummy byte. We can then
   * safely turn off the transmitter, as our payload has been sent. */
  rfm12_sendbyte(0x00); /* This is really just a dummy byte */
  rfm12_waitforrts();
  rfm12_settransmitter(0);
}

void rfm12_initport(void) {
  /* Turn on power for the RF12B */
  DDRB |= _BV(PB0);
  PORTB &= (uint8_t)~_BV(PB0);
  /* Configure Pins for output / input */
  RFMDDR |= _BV(RFMPIN_MOSI);
  RFMDDR &= (uint8_t)~_BV(RFMPIN_MISO);
  RFMDDR |= _BV(RFMPIN_SCK);
  PORTB |= _BV(RFMPIN_SS);
  DDRB |= _BV(RFMPIN_SS);
  USICR = _BV(USIWM0);
}

void rfm12_initchip(void) {
  rfm12_spi16(0x8209);   // RX/TX off, Clock output off
  rfm12_spi16(0x80E8);   // 80e8 CONFIGURATION EL,EF,868 band,12.5pF  (iT+ 915  80f8)
  rfm12_spi16(0xC26a);   // DATA FILTER
  rfm12_spi16(0xCA12);   // FIFO AND RESET  8,SYNC,!ff,DR 
  rfm12_spi16(0xCEd4);   // SYNCHRON PATTERN  0x2dd4 
  rfm12_spi16(0xC481);   // AFC "Keep the F-Offset only during VDI=high" 
  rfm12_spi16(0x94a0);   // RECEIVER CONTROL VDI Medium 134khz LNA max DRRSI 103 dbm  
  rfm12_spi16(0xCC77);   // 
  rfm12_spi16(0x9850);   // TX Config: 0x9850: Shift positive,  Deviation 90 kHz
  rfm12_spi16(0xE000);   // 
  rfm12_spi16(0xC800);   // 
  rfm12_spi16(0xC040);   // 1.66MHz,2.2V 
  /* Set Frequency */
  rfm12_spi16(0xA000 + ((RFM_FREQUENCY - 860000) / 5));
  /* Set Datarate - the whole floating point mess seems to be optimized out 
   * by gcc at compile time, thank god. */
  uint8_t dr = (uint8_t)round((10000000.0 / 29.0) / RFM_DATARATE) - 1;
  rfm12_spi16(0xC600 | dr);

  rfm12_clearfifo();
}
