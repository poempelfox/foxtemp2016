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
which is a simple USB transceiver for 833 MHz that is commonly used in
non-commercial home-automation systems. As such it is well supported by
FHEM, a popular non-commercial and cloud-free home-automation software.
A FHEM module for feeding temperature data from foxtemp2016 devices into
FHEM can also be found in this repository.

## Hardware

The hardware is based on a [JeeNode micro v3](http://jeelabs.org/jm3), which is
an ATtiny84 based microcontroller module with an RFM12B 868 MHz wireless module.
The board also includes a boost converter, which means it can be powered by
anything from 1.0 to 5.0 volts and will generate the 3.0 volts the
microcontroller and peripherals use from that.
This has been designed for low power applications, and when not transmitting
uses only a fraction of a miliampere (around 0.06 mA at 2 volts input voltage,
this depends on the input voltage obviously).

A reference design for a case that will fit the sensor construction and the
batteries is also included.

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


