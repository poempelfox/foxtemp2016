# Foxtemp 2016

This repository contains info about and firmware for the FoxTemp-Device version 2016.

This is basically a wireless successor to my old ds1820tosub-project, with
the following changes:
- Now wireless. Sadly, that means it needs to be powered by batteries
- uses a SHT31 as a sensor, not a DS1820.
- can also measure humidity

## Hardware

The hardware is based on a [JeeNode micro v3](http://jeelabs.org/jm3), which is
an ATtiny84 based microcontroller module with an RFM12B 868 MHz wireless module.
The board also includes a boost converter, which means it can be powered by
anything from 1.0 to 5.0 volts and will generate the 3.0 volts the
microcontroller and peripherals use from that.
This has been designed for low power applications, and when not transmitting
uses only a fraction of a miliampere (around 0.06 mA at 2 volts input voltage,
this depends on the input voltage obviously).

## Firmware


