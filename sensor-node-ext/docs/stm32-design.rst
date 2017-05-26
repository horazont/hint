STM32-based design
##################

Terminology:

sensor node
  The PCB + MCU which speaks the hint wire protocol and thus provides the host interface. Placed inside.

sensor block
  The PCB + MCU which controls the sensors and communicates with the sensor node PCB to transmit sensor data. Placed outside.


Sensor block
============

Hardware requirements (in addition to the pinout described below):

* One of:

  * 1 LDO regulator for 5V -> 3.3V, filtering for AVcc, AGND
  * 2 LDO regulators for 5V -> 3.3V, one for Analogue and one for Digital

* Separate PCB for analogue parts

* Connectors

Sensor connections
------------------

This section describes the pinout required for each sensor type. *n* is always the number of sensors connected.

* Temperature: 3 pins: Vcc, GND, OneWire (digital inout)

* Light: 3+n pins: Vcc, GND, Select (*n* digital out), Read (1 digital timer in)

  NOTE: maximum frequency is about 800 kHz (tested with LED lights, increase of
  intensity did not yield increase of frequency)

  minimum frequency goes down to a few Hz -> we need to be overflow-safe

* DHT (humidity): 3+n pins: Vcc, GND, Data (*n* digital inout)

* Noise: 4+n pins: AVcc, AGND, Vcc, GND, Level (*n* analogue in)

Sensor node connections
-----------------------

* Vcc (5V), GND, Vcc (3.3V, return path for SWD)

* Serial Wire Debug (2 pins)

* USART (multiplexed with bootloader) (2 pins)

* BOOT0, BOOT1 (2 pins)

Sub-D 9 pin: Vcc5, Vcc3.3 (return path for SWD), GND, USART RX, USART TX, SWDIO, SWCLK, BOOT0, BOOT1

Total pinout
------------

From MCU:

* 1 timer/counter (for Light)
* 2 digital inout (for OneWire and DHT)
* 4 digital out (for Light)
* 1 analogue in (for Noise)

* 1 SPI/I2C (for Sensor Node)
* 1 SWD (for Sensor Node)

General:

* Vcc (5V)
* GND

Sensor node
===========

Host connection
---------------

* One of:

  * UART (2 pins)
  * I2C (2 pins)

* Vcc, GND
