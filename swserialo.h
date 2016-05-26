/* $Id: swserialo.h $
 * serial output line implemented in software (can use any I/O-pin without
 * hardware support)
 */

#ifndef _SWSERIALO_H_
#define _SWSERIALO_H_

#include <avr/pgmspace.h>

void swserialo_init(void);
void swserialo_printchar(uint8_t c);
void swserialo_printpgm_P(PGM_P what);
void swserialo_printbin8(uint8_t what);

#endif /* _SWSERIALO_H_ */
