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
#include "eeprom.h"
#include "rfm12.h"
#include "sht31.h"
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

/* This is just a fallback value, in case we cannot read this from EEPROM
 * on Boot */
uint8_t sensorid = 3; // 0 - 255 / 0xff

/* The frame we're preparing to send. */
static uint8_t frametosend[10];

static uint8_t calculatecrc(uint8_t * data, uint8_t len)
{
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
 * Byte  2: Number of data bytes that follow (6)
 * Byte  3: Sensortype (=0xf7 for FoxTemp)
 * Byte  4: temperature MSB (raw value from SHT31)
 * Byte  5: temperature LSB
 * Byte  6: humidity MSB (raw value from SHT31)
 * Byte  7: humidity LSB
 * Byte  8: Battery voltage
 * Byte  9: CRC
 */
void prepareframe(void)
{
  frametosend[ 0] = 0xCC;
  frametosend[ 1] = sensorid;
  frametosend[ 2] = 6; /* 6 bytes of data follow (CRC not counted) */
  frametosend[ 3] = 0xf7; /* Sensor type: FoxTemp */
  frametosend[ 4] = (temp >> 8) & 0xff;
  frametosend[ 5] = (temp >> 0) & 0xff;
  frametosend[ 6] = (hum >> 8) & 0xff;
  frametosend[ 7] = (hum >> 0) & 0xff;
  frametosend[ 8] = batvolt;
  frametosend[ 9] = calculatecrc(frametosend, 9);
}

void loadsettingsfromeeprom(void)
{
  uint8_t e1 = eeprom_read_byte(&ee_sensorid);
  uint8_t e2 = eeprom_read_byte(&ee_invsensorid);
  if ((e1 ^ 0xff) == e2) { /* OK, the 'checksum' matches. Use this as our ID */
    sensorid = e1;
  }
}

/* This is just to wake us up from sleep, it doesn't really do anything. */
ISR(WATCHDOG_vect)
{
  //swserialo_printpgm_P(PSTR("!WD!"));
  /* Nothing to do here. */
}

int main(void)
{
  /* Initialize stuff */
  /* Clock down to 1 MHz. */
  CLKPR = _BV(CLKPCE);
  CLKPR = _BV(CLKPS1) | _BV(CLKPS0);
  
  /* Turn off power to RFM12/69 to force a hard reset of that when
   * we reset from the watchdog timer and not the external RESET line. */
  DDRB |= _BV(PB0);
  PORTB |= _BV(PB0);
  _delay_ms(2000); /* Wait. */

  swserialo_init();
  swserialo_printpgm_P(PSTR("FoxTempDevice 2016.\r\n"));
  
  rfm12_initport();
  adc_init();
  sht31_init();
  loadsettingsfromeeprom();
  
  _delay_ms(1000); /* The RFM12 needs some time to start up */
  
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
  /* Disable unneeded digital input registers for ADC pin PA2 (used for voltage
   * measurement) and PA7 (unused/floating). */
  DIDR0 |= _BV(ADC2D);
#ifndef SWSERIALO
  DIDR0 |= _BV(ADC7D);
  PORTA |= _BV(PA7);
#endif /* SWSERIALO */
  /* PB2 is the IRQ line from the RFM12. We don't use it. Make sure that pin
   * is tristated on our side (it won't float, the RFM12B pulls it) */
  PORTB &= (uint8_t)~_BV(PB2);
  DDRB &= (uint8_t)~_BV(PB2);

  /* All set up, enable interrupts and go. */
  sei();

  sht31_startmeas();

  uint16_t transmitinterval = 2; /* this is in multiples of the watchdog timer timeout (8S)! */
  uint8_t mlcnt = 0;
  uint8_t readerrcnt = 0;
  while (1) { /* Main loop, we should never exit it. */
    mlcnt++;
    swserialo_printpgm_P(PSTR("."));
    if (mlcnt > transmitinterval) {
      rfm12_setsleep(0);  /* This mainly turns on the oscillator again */
      adc_power(1);
      adc_start();
      /* Fetch values from PREVIOUS measurement */
      struct sht31data hd;
      sht31_read(&hd);
      temp = 0xffff;
      hum = 0xffff;
      if (hd.valid) {
        readerrcnt = 0;
        temp = hd.temp;
        hum = hd.hum;
      } else {
        readerrcnt++;
        if (readerrcnt > 5) {
          /* We could not read the SHT31 5 times in a row?! */
          /* Then force reset through watchdog timer. */
          swserialo_printpgm_P(PSTR("TooManyReadErrorsWaitingForWatchdogReset\r\n"));
          while (1) {
            sleep_cpu();
          }
        }
      }
      sht31_startmeas();
      /* read voltage from ADC */
      uint16_t adcval = adc_read();
      adc_power(0);
      /* we use a 1 M / 10 MOhm voltage divider, thus the voltage we see on
       * the ADC is 10/11 of the real voltage. */
      batvolt = adcval >> 2;
      prepareframe();
      swserialo_printpgm_P(PSTR("\r\nTX\r\n"));
      rfm12_sendarray(frametosend, 10);
      pktssent++;
      /* Semirandom delay: the lowest bits from the ADC are mostly noise, so
       * we use that */
      transmitinterval = 3 + (adcval & 0x0001);
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
