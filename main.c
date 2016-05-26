/* $Id: main.c $
 * main for JeeNode Weather Station, tieing all parts together.
 * (C) Michael "Fox" Meier 2016
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <stdio.h>
#include <string.h>
#include <util/delay.h>

#include "adc.h"
#include "hyt221.h"
#include "rfm12.h"
#include "swserialo.h"

/* We need to disable the watchdog very early, because it stays active
 * after a reset with a timeout of only 15 ms. */
void dwdtonreset(void) __attribute__((naked)) __attribute__((section(".init3")));
void dwdtonreset(void) {
  MCUSR = 0;
  wdt_disable();
}

/* The values last measured */
/* temperature. 0xffff on error, 14 useful bits. */
uint16_t temp = 0;
/* humidity. 0xffff on error, 14 useful bits */
uint16_t hum = 0;
/* Battery level. Range 0-255, 255 = our supply voltage = 3,3V */
uint8_t batvolt = 0;
/* How often did we send a packet? */
uint32_t pktssent = 0;

/* FIXME: Store this in EEPROM / Read from there on Boot */
uint8_t sensorid = 7; // 0 - 255 / 0xff

/* The frame we're preparing to send. */
static uint8_t frametosend[9];

static uint8_t calculatecrc(uint8_t * data, uint8_t len) {
  uint8_t i, j;
  uint8_t res = 0;
  for (j = 0; j < len; j++) {
    uint8_t val = data[j];
    for (i = 0; i < 8; i++) {
      uint8_t tmp = (uint8_t)((res ^ val) & 0x80);
      res <<= 1;
      if (0 != tmp) {
        res ^= 0x31;
      }
      val <<= 1;
    }
  }
  return res;
}

/* Fill the frame to send with out collected data and a CRC.
 * The protocol we use is that of a "CustomSensor" from the
 * FHEM LaCrosseItPlusReader sketch for the Jeelink.
 * So you'll just have to enable the support for CustomSensor in that sketch
 * and flash it onto a JeeNode and voila, you have your receiver.
 *
 * Byte  0: Startbyte (=0xCC)
 * Byte  1: Sensor-ID (0 - 255/0xff)
 * Byte  2: Number of data bytes that follow (15)
 * Byte  3: temperature MSB (raw value from SHT15)
 * Byte  4: temperature LSB
 * Byte  5: humidity MSB (raw value from SHT15)
 * Byte  6: humidity LSB
 * Byte  7: Battery voltage
 * Byte  8: CRC
 */
void prepareframe(void) {
  frametosend[ 0] = 0xCC;
  frametosend[ 1] = sensorid;
  frametosend[ 2] = 5; /* 5 bytes of data follow (CRC not counted) */
  frametosend[ 3] = (temp >> 8) & 0xff;
  frametosend[ 4] = (temp >> 0) & 0xff;
  frametosend[ 5] = (hum >> 8) & 0xff;
  frametosend[ 6] = (hum >> 0) & 0xff;
  frametosend[ 7] = batvolt;
  frametosend[ 8] = calculatecrc(frametosend, 8);
}

/* This is just to wake us up from sleep, it doesn't really do anything. */
ISR(WATCHDOG_vect)
{
  //swserialo_printpgm_P(PSTR("!WD!"));
  /* Nothing to do here. */
}

int main(void) {
  /* Initialize stuff */
  /* Clock down to 1 MHz. */
  CLKPR = _BV(CLKPCE);
  CLKPR = _BV(CLKPS1) | _BV(CLKPS0);
  
  swserialo_init();
  swserialo_printpgm_P(PSTR("HaWo TempDevice 2016.\r\n"));
  
  rfm12_initport();
  adc_init();
  hyt221_init();
  
  _delay_ms(500); /* The RFM12 needs some time to start up */
  
  rfm12_initchip();
  rfm12_setsleep(1);
  
  /* Enable watchdog timer interrupt with a timeout of 8 seconds */
  WDTCSR = _BV(WDCE) | _BV(WDE);
  WDTCSR = _BV(WDE) | _BV(WDIE) | _BV(WDP0) | _BV(WDP3);

  /* Prepare sleep mode */
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  
  /* Disable unused chip parts and ports */
  /* We do not use any of the timers */
  PRR |= _BV(PRTIM0) | _BV(PRTIM1);
  /* Disable analog comparator */
  ACSR |= _BV(ACD);
  /* Disable unneeded digital input registers for ADC pin PA1 (used for voltage
   * measurement) and PA7 (unused/floating). */
  DIDR0 |= _BV(ADC1D) | _BV(ADC7D);
  PORTA |= _BV(PA7);
  /* PB2 is the IRQ line from the RFM12. We don't use it. Make sure that pin
   * is tristated on our side (it won't float, the RFM12B pulls it) */
  PORTB &= (uint8_t)~_BV(PB2);
  DDRB &= (uint8_t)~_BV(PB2);

  /* All set up, enable interrupts and go. */
  sei();

  hyt221_startmeas();

  uint16_t transmitinterval = 4; /* this is in multiples of the watchdog timer timeout (8S)! */
  uint8_t mlcnt = 0;
  while (1) { /* Main loop, we should never exit it. */
    mlcnt++;
    swserialo_printpgm_P(PSTR("."));
    if (mlcnt > transmitinterval) {
      rfm12_setsleep(0);  /* This mainly turns on the oscillator again */
      adc_power(1);
      adc_start();
      /* Fetch values from PREVIOUS measurement */
      struct hyt221data hd;
      hyt221_read(&hd);
      temp = 0xffff;
      hum = 0xffff;
      if (hd.valid) {
        temp = hd.temp;
        hum = hd.hum;
      }
      hyt221_startmeas();
      /* read voltage from ADC */
      batvolt = adc_read() >> 2;
      adc_power(0);
      prepareframe();
      swserialo_printpgm_P(PSTR(" TX "));
      rfm12_sendarray(frametosend, 9);
      pktssent++;
      if (batvolt > 153) { /* ca. 1.8 volts */
        transmitinterval = 4;
      } else { /* supercap half empty - increase transmitinterval to save power */
        transmitinterval = 7;
      }
      rfm12_setsleep(1);
      mlcnt = 0;
    }
    wdt_reset();
    sleep_cpu(); /* Go to sleep until the watchdog timer wakes us */
    /* We should only reach this if we were just woken by the watchdog timer.
     * We need to re-enable the watchdog-interrupt-flag, else the next watchdog
     * -reset will not just trigger the interrupt, but be a full reset. */
    WDTCSR = _BV(WDCE) | _BV(WDE);
    WDTCSR = _BV(WDE) | _BV(WDIE) | _BV(WDP0) | _BV(WDP3);
  }
}
