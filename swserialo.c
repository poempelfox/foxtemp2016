/* $Id: swserialo.h $
 * serial output line implemented in software (can use any I/O-pin without
 * hardware support)
 */

#include <avr/io.h>
#include <inttypes.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include "swserialo.h"

#ifndef SWSERBAUD
#define SWSERBAUD 9600
#endif /* SWSERBAUD */

/* the delay()s we execute naturally have a certain overhead. You can set the
 * following define to correct for that. However, if your baudrate is set low
 * enough compared to your CPUFREQ, it should work fine with "0".
 * You may also need to adjust this (+ or -) if you are using the internal osc.
 * of the AVR, because the accuracy of that is usually horrible, and deviates
 * from the nominal frequency quite a lot. */
#define DELAYOVERHEAD 4.0
/* How long is one bit in microseconds, or rather: How long do we wait in a
 * busy loop after we transferred one bit? */
#define USPERBIT ((1000000.0 / SWSERBAUD) - DELAYOVERHEAD)

/* Which is the pin that we use? */
#define SWSDDR DDRA
#define SWSPORT PORTA
#define SWSBIT PA0

#ifdef SWSERIALO

void swserialo_init(void)
{
  /* Configure port for output */
  SWSDDR |= _BV(SWSBIT);
  /* and drive high, because the line is '1' when idle. */
  SWSPORT |= _BV(SWSBIT);
}

void swserialo_printchar(uint8_t c)
{
  /* Start bit */
  SWSPORT &= (uint8_t)~_BV(SWSBIT);
  _delay_us(USPERBIT);
  /* Note: surprisingly, the LSB is transmitted first! */
  for (uint8_t bm = 0x01; bm > 0; bm <<= 1) {
    if (c & bm) {
      SWSPORT |= _BV(SWSBIT);
    } else {
      SWSPORT &= (uint8_t)~_BV(SWSBIT);
    }
    _delay_us(USPERBIT);
  }
  /* Stop bit */
  SWSPORT |= _BV(SWSBIT);
  _delay_us(USPERBIT);
}

void swserialo_printpgm_P(PGM_P what) {
  uint8_t t;
  while ((t = pgm_read_byte(what++))) {
    swserialo_printchar(t);
  }
}

void swserialo_printbin8(uint8_t what) {
  for (uint8_t m = 0x80; m > 0; m >>= 1) {
    if (what & m) {
      swserialo_printchar('1');
    } else {
      swserialo_printchar('0');
    }
  }
}

#else /* no SWSERIALO */

void swserialo_init(void) {
  /* Enable pullup to ensure defined state (saves power) */
  SWSPORT |= _BV(SWSBIT);
}
void swserialo_printchar(uint8_t c) { }
void swserialo_printpgm_P(PGM_P what) { }
void swserialo_printbin8(uint8_t what) { }

#endif /* SWSERIALO */
