# $Id: Makefile $
# Makefile for Foxtemp2016

CC	= avr-gcc
OBJDUMP	= avr-objdump
OBJCOPY	= avr-objcopy
AVRDUDE	= avrdude
INCDIR	= .
# There are a few additional defines that en- or disable certain features,
# mainly to save space in case you are running out of flash.
# You can add them here.
#  -DSWSERIALO        enable software (bitbanging) serial port on PA7 (output only)
#  -DSWSERBAUD=...    set baudrate for serial port
#  -DUSERFM69INSTEAD  if you've soldered a RFM69CW instead of the normal RFM12 onto the Jeenode Micro.
#  -DRFM_DATARATE=9579.0 or 17241.0. This defaults to 17241 for RFM69, 9579 for RFM12.
ADDDEFS	= #-DUSERFM69INSTEAD -DSWSERIALO -DSWSERBAUD=9600

# target mcu (atmega 328p)
MCU	= attiny84
# Since avrdude is generally crappy software (I liked uisp a lot better, too
# bad the project is dead :-/), it cannot use the MCU name everybody else
# uses, it has to invent its own name for it. So this defines the same
# MCU as above, but with the name avrdude understands.
AVRDMCU	= t84

# Some more settings
# Clock Frequency of the AVR. Needed for various calculations.
CPUFREQ		= 1000000UL

SRCS	= adc.c eeprom.c main.c rfm12.c sht31.c swserialo.c
PROG	= foxtemp2016

# compiler flags
CFLAGS	= -g -Os -Wall -Wno-pointer-sign -std=c99 -mmcu=$(MCU) $(ADDDEFS)

# linker flags
LDFLAGS = -g -mmcu=$(MCU) -Wl,-Map,$(PROG).map -Wl,--gc-sections

CFLAGS += -DCPUFREQ=$(CPUFREQ) -DF_CPU=$(CPUFREQ)

OBJS	= $(SRCS:.c=.o)

all: compile dump text eeprom
	@echo -n "Compiled size: " && ls -l $(PROG).bin

compile: $(OBJS)
	$(CC) $(LDFLAGS) -o $(PROG).elf $(OBJS)

dump: compile
	$(OBJDUMP) -h -S $(PROG).elf > $(PROG).lst

%o : %c 
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

# Create the flash contents
text: compile
	$(OBJCOPY) -j .text -j .data -O ihex $(PROG).elf $(PROG).hex
	$(OBJCOPY) -j .text -j .data -O srec $(PROG).elf $(PROG).srec
	$(OBJCOPY) -j .text -j .data -O binary $(PROG).elf $(PROG).bin

# Rules for building the .eeprom rom images
eeprom: compile
	$(OBJCOPY) -j .eeprom --change-section-lma .eeprom=0 -O ihex $(PROG).elf $(PROG)_eeprom.hex
	$(OBJCOPY) -j .eeprom --change-section-lma .eeprom=0 -O srec $(PROG).elf $(PROG)_eeprom.srec
	$(OBJCOPY) -j .eeprom --change-section-lma .eeprom=0 -O binary $(PROG).elf $(PROG)_eeprom.bin

clean:
	rm -f $(PROG) hostreceiverforjeelink *~ *.elf *.rom *.bin *.eep *.o *.lst *.map *.srec *.hex

hostreceiverforjeelink: hostreceiverforjeelink.c
	gcc -o hostreceiverforjeelink -Wall -Wno-pointer-sign -O2 -DBRAINDEADOS hostreceiverforjeelink.c

fuses:
	@echo "If you want to be safe, the fuses should be set for a BODlevel"
	@echo "of 2.7 volts. Something along the lines of:"
	@echo "  avrdude ... -U hfuse:w:0xd5:m"
	@echo "However, that uses up 0,03 mA of power, in other words, it almost"
	@echo "doubles the power consumption. To disable BODlevel, set hfuse"
	@echo "to 0xd7 instead, but be warned that the longterm stability of that"
	@echo "is untested/unknown."

upload: uploadflash uploadeeprom

uploadflash:
	$(AVRDUDE) -c stk500v2 -p $(AVRDMCU) -P /dev/ttyUSB0 -U flash:w:$(PROG).hex

uploadeeprom:
	$(AVRDUDE) -c stk500v2 -p $(AVRDMCU) -P /dev/ttyUSB0 -U eeprom:w:$(PROG)_eeprom.srec:s

