# Foxtemp 2016

## Intro

This repository contains info about, firmware and a case for the
FoxTemp-Device version 2016.

This is basically a wireless successor to
[my old ds1820tosub-project](https://www.poempelfox.de/ds1820tousb/), with
the following changes:
- Now wireless. Sadly, that means it needs to be powered by batteries
- uses a SHT31 as a sensor, not a DS1820.
- can also measure humidity

Note that you will also need a way to receive the wirelessly transmitted
data. I simply used a [JeeLink](http://jeelabs.net/projects/hardware/wiki/JeeLink) v3c,
which is a simple USB transceiver for 868 MHz that is commonly used in
non-commercial home-automation systems. As such it is well supported by
FHEM, a popular non-commercial and cloud-free home-automation software.
A FHEM module for feeding temperature data from foxtemp2016 devices into
FHEM can also be found in this repository.

## Hardware

### Intro

This has been designed for low power applications, and when not transmitting
uses only a fraction of a miliampere (around 0.06 mA at 2 volts input voltage,
this depends on the input voltage obviously).

### BOM

The hardware uses as many "ready made" parts as possible and just slaps them
together. The bill of materials therefore is short:

* [JeeNode micro v3](http://jeelabs.org/jm3), which is
  an ATtiny84 based microcontroller module with an RFM12B 868 MHz wireless module.
  The board also includes a boost converter, which means it can be powered by
  anything from 1.0 to 5.0 volts and will generate the 3.0 volts the
  microcontroller and peripherals use from that.
* SHT31 temperature sensor, in the form of a
  [SHT31 breakout board by Adafruit](https://www.adafruit.com/product/2857).
  This is used because the SHT31 is very tiny, so soldering it directly
  without damaging it would not be an easy task, and because the breakout
  board serves perfectly as what would be called a "shield" in the Arduino
  world: It's plugged/stacked on top of the microcontroller board.
* a voltage divider for measuring the current voltage of the batteries, so you
  can tell how full they are.
  * resistor 10 MOhm
  * resistor 1 MOhm
  * small capacitor. Everything >= 1 nF should be just fine, I used 100 nF
     because I had that lying around.
* Goobay 78467 battery tray. This is simply a battery holder for two
  AA batteries that has cables attached.
* an additional 100 uF capacitor for stabilizing power, simply soldered
  between the power pin and ground. This is completely
  optional. I added it because IMHO the capacitors on the JeeNode Micro
  are a bit short, and I actually had problems with the microcontroller
  crashing during transmission (when the radio module suddenly starts drawing
  a lot of power) when connected through a cheap multimeter (for measuring the
  current drawn) in a different project.

### Putting it together

FIXME

## Case

A reference design for a laser-cut case that will fit the sensor construction
and the batteries is also included. You can find the DXF file in the subdirectory
`case`. It's based on a
[DIY-lasercut-case-design for a Raspberry Pi B](https://github.com/diy-electronics/raspberrypi-b-plus-case/)
which in turn seems to be based on some [Adafruit](http://www.adafruit.com)
case. I merely shrunk the case and added holes so that the sensor can actually get
air from the room.

The dimensions of the case are about 68x66x30 mm and it is intended to be
cut from 3 mm thick acryl. The case can be assembled without any glueing,
the parts click together.

The license for the case is CC-BY-SA (due to the fact that the case
design I modified was also CC-BY-SA).

## Firmware

The firmware in this repo has been optimized for low power consumption.
It sends the microcontroller into deep sleep and uses the watchdog timer
to wake it up in regular intervals. This is possible because on the
ATtiny84 the WDT has a special mode in which it will just cause an
interrupt that is capable of waking the device from every sleep mode,
and only if the timer expires a second time before being reset it
will actually trigger a full device reset.

The device will transmit information every 24 or 32 seconds. It tries
to randomize that interval by using the lowest bit of the
Analog-Digital-Converter data as a random value (which it pretty much
is). The idea behind this that if two devices start up at exactly the
same time, after a few minutes the time at which they transmit data
will spread out.

Each sensor needs to be given a unique ID of 8 bits. This is stored
in the EEPROM. If you set the ID in eeprom.c and recompile, you can
then flash the EEPROM from `foxtemp2016_eeprom.bin`/`.hex`. The
Makefile target uploadeeprom can serve as an example for how to do
this with a STK500. If the firmware does not find a configured ID
in the EEPROM, it will default to an ID of **3**.

## Software

The FHEM module can be found in the file `36_Foxtemp2016viaJeelink.pm`.
I'm not really proud of this one, it's a mess. Because there seems
to be no usable documentation on writing proper FHEM modules, I did
what everybody else seems to do: Copy an existing module and modify
it by trial and error until it somehow works.  
There are usage instructions at the top of that file, I'll repeat
them here:

The module is for use together with a JeeLink as a receiver.
On the JeeLink, you'll need to run a slightly modified version of the firmware
for reading LaCrosse (found in the FHEM repository as
`/contrib/36_LaCrosse-LaCrosseITPlusReader.zip`):  
It has support for a sensor type called "CustomSensor", but that is usually
not compiled in. There is a line  
   `CustomSensor::AnalyzeFrame(payload);`  
commented out in `LaCrosseITPlusReader10.ino` -
you need to remove the `////` to enable it, then recompile the firmware
and flash it onto the JeeLink.

You need to put `36_Foxtemp2016viaJeelink.pm` into `/opt/fhem/FHEM/`,
and to make it work, you'll also need to modify `36_JeeLink.pm`:
to the string "clientsJeeLink" append `:Foxtemp2016viaJeelink`.

Finally, to define a FoxTemp2016-sensor in FHEM, the command is  
  `define <name> Foxtemp2016viaJeelink <addr>`  
e.g.  
  `define kitchen FoxTemp2016viaJeelink 6`  
where addr is the ID of the sensor, either in decimal or
(prefixed with 0x) in hexadecimal notation.

