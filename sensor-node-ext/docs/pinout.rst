Variant 1
###

Variant 1 uses a mostly passive sensor block (only the opamps are
outside). This has the advantage of low maintenance outside parts, while
requiring quite some pinout.

TODO: Capacitive humidity sensor (Rain detection!)

Sensor block <-> sensor node
===

* GND
* Vcc (5V)
* OneWire Data (1 digital in/out)
* Noise level (1 analogue in)
* Light sensor select (4 digital out)
* Light sensor read (1 timer in)
* DHT (1 digital in/out)

total: 10 pins


Variant 2
###

Variant 2 uses an active sensor block; an AVR (type TBD) is there and
communication with the actual sensor node happens using SPI or I2C.

Here it is important that the whole SPI programmer interface is routed via the
connector, to be able to program the μC from inside.

Sensor block <-> sensor node
===

* GND
* Vcc (5V)
* SPI (3 digital in/out)
* RST (for programming, 1 digital out)

Total pin count: 6


Required outside pinout of the sensor block
===

Temperature (3 pins):

* Vcc
* GND
* OneWire data (1 digital in/out)

Light (7 pins):

* Vcc
* GND
* Select (4 digital out)
* Read (1 timer in)

DHT (3 pins):

* Vcc
* GND
* Data (1 digital in/out)

Noise (3 to 5 pins):

* AVcc
* AGND
* Level (1 analogue in)
* maybe Vcc
* maybe GND


Total pin requirements on sensor block:

* 10 digital in/out, of that

  * 1 SPI
  * 1 timer in

* 1 analogue in
