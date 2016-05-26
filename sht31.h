/* $Id: sht31.h $
 * Functions for reading the SHT 31 temperature / humidity sensor
 */

#ifndef _SHT31_H_
#define _SHT31_H_

struct sht31data {
  uint16_t temp;
  uint16_t hum;
  uint8_t valid;
};

/* Initialize I2C and sht31 */
void sht31_init(void);

/* Start measurement */
void sht31_startmeas(void);

/* Read result of measurement. Needs to be called no earlier than 15 ms
 * after starting. */
void sht31_read(struct sht31data * d);

#endif /* _SHT31_H_ */
