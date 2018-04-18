/* $Id: rfm12.h $
 * Functions for communication with RF12 module.
 * Nowadays can alternatively talk to a RFM69 module if you define
 * USERFM69INSTEAD.
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

#ifdef USERFM69INSTEAD
/* Add two helpers for accessing the RFM69s registers. */
uint8_t rfm69_readreg(uint8_t reg) {
  return rfm12_spi16(((uint16_t)(reg & 0x7f) << 8) | 0x00) & 0xff;
}

static void rfm69_writereg(uint8_t reg, uint8_t val) {
  rfm12_spi16(((uint16_t)(reg | 0x80) << 8) | val);
}
#endif

void rfm12_clearfifo(void) {
#ifdef USERFM69INSTEAD
  rfm69_writereg(0x28, (1 << 4)); /* RegIrqFlags2 */
#else /* Normal RFM12 */
  for (uint8_t i = 0; i < PAYLOADSIZE; i++) {
    rfm12_spi16(0xB000);
  }
#endif /* Normal RFM12 */
}

void rfm12_settransmitter(uint8_t e) {
#ifdef USERFM69INSTEAD
  if (e) {
    rfm69_writereg(0x01, (rfm69_readreg(0x01) & 0xE3) | 0x0C);
  } else {
    rfm69_writereg(0x01, (rfm69_readreg(0x01) & 0xE3) | 0x04);
  }
#else /* Normal RFM12 */
  if (e) {
    rfm12_spi16(0x8239);
  } else {
    rfm12_spi16(0x8209);
  }
#endif /* Normal RFM12 */
}

void rfm12_setsleep(uint8_t s) {
  if (s) {
#ifdef USERFM69INSTEAD
    rfm69_writereg(0x01, (rfm69_readreg(0x01) & 0xE3) | 0x00);
#else /* Normal RFM12 */
    rfm12_spi16(0x8205);
#endif /* Normal RFM12 */
    RFMDDR &= (uint8_t)~_BV(RFMPIN_MOSI);
    RFMDDR &= (uint8_t)~_BV(RFMPIN_SCK);
    PRR |= _BV(PRUSI);
  } else {
    PRR &= (uint8_t)~_BV(PRUSI);
    RFMDDR |= _BV(RFMPIN_MOSI);
    RFMDDR |= _BV(RFMPIN_SCK);
    USICR = _BV(USIWM0);
#ifdef USERFM69INSTEAD
    rfm69_writereg(0x01, (rfm69_readreg(0x01) & 0xE3) | 0x04);
    while (!(rfm69_readreg(0x27) & 0x80)) { /* Wait until ready */ }
#else /* Normal RFM12 */
    rfm12_spi16(0x8209);
#endif /* Normal RFM12 */
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
#ifdef USERFM69INSTEAD
  /* Set the length of our payload */
  rfm69_writereg(0x38, length);
  rfm12_clearfifo(); /* Clear the FIFO */
  /* Now fill the FIFO. We manually set SS and use spi8 because this
   * is the only "register" that is larger than 8 bits. */
  PORTB &= (uint8_t)~_BV(RFMPIN_SS);
  rfm12_spi8(0x80); /* Select RegFifo (0x00) for writing (|0x80) */
  for (int i = 0; i < length; i++) {
    rfm12_spi8(data[i]);
  }
  PORTB |= _BV(RFMPIN_SS);
  /* FIFO has been filled. Tell the RFM69 to send by just turning on the transmitter. */
  rfm12_settransmitter(1);
  /* Wait for transmission to finish, visible in RegIrqFlags2. */
  uint8_t reg28 = 0x00;
  uint16_t maxreps = 10000;
  while (!(reg28 & 0x08)) {
    reg28 = rfm69_readreg(0x28);
    maxreps--;
    if (maxreps == 0) { /* Give up */
      break;
    }
  }
  rfm12_settransmitter(0);
#else /* Normal RFM12 */
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
#endif /* Normal RFM12 */
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
#ifdef USERFM69INSTEAD
  /* RegOpMode -> standby. */
  rfm69_writereg(0x01, 0x00 | 0x04);
  /* RegDataModul -> PacketMode, FSK, Shaping 0 */
  rfm69_writereg(0x02, 0x00);
  /* RegFDevMsb / RegFDevLsb -> 0x05C3 (90 kHz). */
  rfm69_writereg(0x05, 0x05);
  rfm69_writereg(0x06, 0xC3);
  /* RegPaLevel -> Pa0=1 Pa1=0 Pa2=0 Outputpower=31 -> 13 dbM for RFM69CW (would not work on HCW!) */
  rfm69_writereg(0x11, 0x9F);
  /* RegOcp -> defaults (jeelink-sketch sets 0 but that seems wrong) */
  rfm69_writereg(0x13, 0x1a);
  /* RegRxBw -> DccFreq 010   Mant 16   Exp 2 - this is a receiver-register,
   * we do not really care about it */
  rfm69_writereg(0x19, 0x42);
  /* RegDioMapping2 -> disable clkout (but thats the default anyways) */
  rfm69_writereg(0x26, 0x07);
  /* RegIrqFlags2 (0x28): some status flags, writing a 1 to FIFOOVERRUN bit
   * clears the FIFO. This is what clearfifo() does. */
  rfm12_clearfifo();
  /* RegRssiThresh -> 220 */
  rfm69_writereg(0x29, 220);
  /* RegPreambleMsb / Lsb - we want 3 bytes of preamble (0xAA) */
  rfm69_writereg(0x2C, 0x00);
  rfm69_writereg(0x2D, 0x03);
  /* RegSyncConfig -> SyncOn FiFoFillAuto SyncSize=2 SyncTol=0 */
  rfm69_writereg(0x2E, 0x88);
  /* RegSyncValue1/2 (3-8 exist too but we only use 2 so do not need to set them) */
  rfm69_writereg(0x2F, 0x2D);
  rfm69_writereg(0x30, 0xD4);
  /* RegPacketConfig1 -> FixedPacketLength CrcOn=0 */
  rfm69_writereg(0x37, 0x00);
  /* RegPayloadLength -> 0
   * This selects between two different modes: "0" means "Unlimited length
   * packet format", any other value "Fixed Length Packet Format" (with that
   * length). We set 0 for now, but actually fill the register before sending.
   */
  rfm69_writereg(0x38, 0x0c);
  /* RegFifoThreshold -> TxStartCond=1 value=0x0f */
  rfm69_writereg(0x3C, 0x8F);
  /* RegPacketConfig2 -> AesOn=0 and AutoRxRestart=1 even if we do not care about RX */
  rfm69_writereg(0x3D, 0x12);
  /* Set Frequency */
  /* The datasheet is horrible to read at that point, never stating a clear
   * formula ready for use. */
  /* F(Step) = F(XOSC) / (2 ** 19)      2 ** 19 = 524288
   * F(forreg) = FREQUENCY_IN_HZ / F(Step) */
  uint32_t freq = round((1000.0 * RFM_FREQUENCY) / (32000000.0 / 524288.0));
  /* (Alternative calculation from JeeNode library:)
   * Frequency steps are in units of (32,000,000 >> 19) = 61.03515625 Hz
   * use multiples of 64 to avoid multi-precision arithmetic, i.e. 3906.25 Hz
   * due to this, the lower 6 bits of the calculated factor will always be 0
   * this is still 4 ppm, i.e. well below the radio's 32 MHz crystal accuracy */
  /* uint32_t freq = (((RFM_FREQUENCY * 1000) << 2) / (32000000UL >> 11)) << 6; */
  rfm69_writereg(0x07, (freq >> 16) & 0xff);
  rfm69_writereg(0x08, (freq >>  8) & 0xff);
  rfm69_writereg(0x09, (freq >>  0) & 0xff);
  /* Set Datarate - the whole floating point mess seems to be optimized out 
   * by gcc at compile time, thank god. */
  uint16_t dr = (uint16_t)round(32000000.0 / RFM_DATARATE);
  rfm69_writereg(0x03, (dr >> 8));
  rfm69_writereg(0x04, (dr & 0xff));
#else /* Normal RFM12 */
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
#endif /* Normal RFM12 */

  rfm12_clearfifo();
}
