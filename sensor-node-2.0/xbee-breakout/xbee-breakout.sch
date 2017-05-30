EESchema Schematic File Version 2
LIBS:power
LIBS:device
LIBS:transistors
LIBS:conn
LIBS:linear
LIBS:regul
LIBS:74xx
LIBS:cmos4000
LIBS:adc-dac
LIBS:memory
LIBS:xilinx
LIBS:microcontrollers
LIBS:dsp
LIBS:microchip
LIBS:analog_switches
LIBS:motorola
LIBS:texas
LIBS:intel
LIBS:audio
LIBS:interface
LIBS:digital-audio
LIBS:philips
LIBS:display
LIBS:cypress
LIBS:siliconi
LIBS:opto
LIBS:atmel
LIBS:contrib
LIBS:valves
LIBS:xbee-breakout-cache
EELAYER 25 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 1 1
Title ""
Date ""
Rev ""
Comp ""
Comment1 ""
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
$Comp
L CONN_01X10 P3
U 1 1 577FC819
P 6400 2600
F 0 "P3" H 6400 3150 50  0000 C CNN
F 1 "CONN_01X10" V 6500 2600 50  0000 C CNN
F 2 "w_pin_strip:pin_socket_2mm_10" H 6400 2600 50  0001 C CNN
F 3 "" H 6400 2600 50  0000 C CNN
	1    6400 2600
	-1   0    0    1   
$EndComp
$Comp
L CONN_01X10 P1
U 1 1 577FC84A
P 5350 2600
F 0 "P1" H 5350 3150 50  0000 C CNN
F 1 "CONN_01X10" V 5450 2600 50  0000 C CNN
F 2 "w_pin_strip:pin_socket_2mm_10" H 5350 2600 50  0001 C CNN
F 3 "" H 5350 2600 50  0000 C CNN
	1    5350 2600
	1    0    0    -1  
$EndComp
$Comp
L CONN_01X06 P2
U 1 1 577FC8D9
P 5800 3950
F 0 "P2" H 5800 4300 50  0000 C CNN
F 1 "CONN_01X06" V 5900 3950 50  0000 C CNN
F 2 "w_pin_strip:pin_socket_6" H 5800 3950 50  0001 C CNN
F 3 "" H 5800 3950 50  0000 C CNN
	1    5800 3950
	1    0    0    -1  
$EndComp
$Comp
L +3.3V #PWR01
U 1 1 577FC9D9
P 5600 4200
F 0 "#PWR01" H 5600 4050 50  0001 C CNN
F 1 "+3.3V" H 5600 4340 50  0000 C CNN
F 2 "" H 5600 4200 50  0000 C CNN
F 3 "" H 5600 4200 50  0000 C CNN
	1    5600 4200
	0    -1   -1   0   
$EndComp
$Comp
L GND #PWR02
U 1 1 577FCA08
P 5600 3700
F 0 "#PWR02" H 5600 3450 50  0001 C CNN
F 1 "GND" H 5600 3550 50  0000 C CNN
F 2 "" H 5600 3700 50  0000 C CNN
F 3 "" H 5600 3700 50  0000 C CNN
	1    5600 3700
	0    1    1    0   
$EndComp
Text Label 5600 3800 2    60   ~ 0
XBEE_DOUT
Text Label 5600 3900 2    60   ~ 0
XBEE_DIN
Text Label 5600 4000 2    60   ~ 0
XBEE_RTS
Text Label 5600 4100 2    60   ~ 0
XBEE_CTS
$Comp
L +3.3V #PWR03
U 1 1 577FCAED
P 5150 2150
F 0 "#PWR03" H 5150 2000 50  0001 C CNN
F 1 "+3.3V" H 5150 2290 50  0000 C CNN
F 2 "" H 5150 2150 50  0000 C CNN
F 3 "" H 5150 2150 50  0000 C CNN
	1    5150 2150
	0    -1   -1   0   
$EndComp
$Comp
L GND #PWR04
U 1 1 577FCB0A
P 5150 3050
F 0 "#PWR04" H 5150 2800 50  0001 C CNN
F 1 "GND" H 5150 2900 50  0000 C CNN
F 2 "" H 5150 3050 50  0000 C CNN
F 3 "" H 5150 3050 50  0000 C CNN
	1    5150 3050
	0    1    1    0   
$EndComp
Text Label 5150 2250 2    60   ~ 0
XBEE_DOUT
Text Label 5150 2350 2    60   ~ 0
XBEE_DIN
Text Label 6600 2550 0    60   ~ 0
XBEE_RTS
Text Label 6600 2950 0    60   ~ 0
XBEE_CTS
$Comp
L GND #PWR?
U 1 1 57811300
P 6600 2750
F 0 "#PWR?" H 6600 2500 50  0001 C CNN
F 1 "GND" H 6600 2600 50  0000 C CNN
F 2 "" H 6600 2750 50  0000 C CNN
F 3 "" H 6600 2750 50  0000 C CNN
	1    6600 2750
	0    -1   -1   0   
$EndComp
$EndSCHEMATC
