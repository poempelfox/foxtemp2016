/* $Id: rfm12.h $
 * Functions for communication with RF12 module
 */

#ifndef _RFM12_H_
#define _RFM12_H_

void rfm12_initport(void);
void rfm12_initchip(void);
void rfm12_clearfifo(void);
void rfm12_settransmitter(uint8_t e);
void rfm12_sendbyte(uint8_t data);
void rfm12_sendarray(uint8_t * data, uint8_t length);
void rfm12_setsleep(uint8_t s);

#endif /* _RFM12_H_ */
