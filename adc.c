/* $Id: adc.c $
 * Functions for the analog digital converter - used to measure supply voltage
 */

#include <avr/io.h>
#include "adc.h"

void adc_init(void)
{
  /* Select prescaler for ADC, disable autotriggering, turn off ADC */
  ADCSRA = _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);
  /* Select reference voltage (VCC) and pin PA2 */
  ADMUX = 2;
  /* Disable ADC for now (gets reenabled for the measurements */
  PRR |= _BV(PRADC);
}

void adc_power(uint8_t p)
{
  if (p) {
    /* Reenable ADC */
    PRR &= (uint8_t)~_BV(PRADC);
  } else {
    /* Send ADC to sleep */
    ADCSRA &= (uint8_t)~_BV(ADEN);
    PRR |= _BV(PRADC);
  }
}

/* Start ADC conversion */
void adc_start(void)
{
  ADCSRA |= _BV(ADEN) | _BV(ADSC);
}

uint16_t adc_read(void)
{
  /* Wait for ADC */
  while ((ADCSRA & _BV(ADSC))) { }
  /* Read result */
  uint16_t res = ADCL;
  res |= (ADCH << 8);
  return res;
}

