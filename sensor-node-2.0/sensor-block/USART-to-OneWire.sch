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
LIBS:stm32
LIBS:analog_devices
LIBS:Power_Management
LIBS:db9x2
LIBS:sensor-node-ext-cache
EELAYER 25 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 3 3
Title ""
Date ""
Rev ""
Comp ""
Comment1 ""
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
Text HLabel 3600 3050 0    60   Input ~ 0
USART_TX
Text HLabel 7100 2950 2    60   Output ~ 0
USART_RX
Text HLabel 7100 2800 2    60   3State ~ 0
ONEWIRE_BUS
$Comp
L GND #PWR050
U 1 1 577A9DF6
P 4150 3200
F 0 "#PWR050" H 4150 2950 50  0001 C CNN
F 1 "GND" H 4150 3050 50  0000 C CNN
F 2 "" H 4150 3200 50  0000 C CNN
F 3 "" H 4150 3200 50  0000 C CNN
	1    4150 3200
	1    0    0    -1  
$EndComp
$Comp
L R R2
U 1 1 577A9E5E
P 4150 2650
F 0 "R2" V 4230 2650 50  0000 C CNN
F 1 "100k" V 4150 2650 50  0000 C CNN
F 2 "Resistors_SMD:R_0805_HandSoldering" V 4080 2650 50  0001 C CNN
F 3 "" H 4150 2650 50  0000 C CNN
	1    4150 2650
	1    0    0    -1  
$EndComp
$Comp
L R R3
U 1 1 577A9EA5
P 4750 2650
F 0 "R3" V 4830 2650 50  0000 C CNN
F 1 "4.7k" V 4750 2650 50  0000 C CNN
F 2 "Resistors_SMD:R_0805_HandSoldering" V 4680 2650 50  0001 C CNN
F 3 "" H 4750 2650 50  0000 C CNN
	1    4750 2650
	1    0    0    -1  
$EndComp
Wire Wire Line
	3600 3050 3850 3050
Wire Wire Line
	4150 2800 4450 2800
Wire Wire Line
	4450 2800 4450 3050
Wire Wire Line
	4750 2500 4750 2450
Wire Wire Line
	4150 2500 4150 2450
Wire Wire Line
	4750 3200 4750 3300
Wire Wire Line
	4750 2800 7100 2800
Connection ~ 6800 2800
Wire Wire Line
	6800 2800 6800 2950
Wire Wire Line
	6800 2950 7100 2950
$Comp
L +3.3V #PWR053
U 1 1 577AE5F2
P 4150 2450
F 0 "#PWR053" H 4150 2300 50  0001 C CNN
F 1 "+3.3V" H 4150 2590 50  0000 C CNN
F 2 "" H 4150 2450 50  0000 C CNN
F 3 "" H 4150 2450 50  0000 C CNN
	1    4150 2450
	1    0    0    -1  
$EndComp
$Comp
L +3.3V #PWR054
U 1 1 577AE616
P 4750 2450
F 0 "#PWR054" H 4750 2300 50  0001 C CNN
F 1 "+3.3V" H 4750 2590 50  0000 C CNN
F 2 "" H 4750 2450 50  0000 C CNN
F 3 "" H 4750 2450 50  0000 C CNN
	1    4750 2450
	1    0    0    -1  
$EndComp
$Comp
L BS170 Q1
U 1 1 578108C2
P 4050 3000
F 0 "Q1" H 4250 3075 50  0000 L CNN
F 1 "BS170" H 4250 3000 50  0000 L CNN
F 2 "TO_SOT_Packages_THT:TO-92_Inline_Wide" H 4250 2925 50  0000 L CIN
F 3 "" H 4050 3000 50  0000 L CNN
	1    4050 3000
	1    0    0    -1  
$EndComp
$Comp
L BS170 Q2
U 1 1 57810980
P 4650 3000
F 0 "Q2" H 4850 3075 50  0000 L CNN
F 1 "BS170" H 4850 3000 50  0000 L CNN
F 2 "TO_SOT_Packages_THT:TO-92_Inline_Wide" H 4850 2925 50  0000 L CIN
F 3 "" H 4650 3000 50  0000 L CNN
	1    4650 3000
	1    0    0    -1  
$EndComp
Text Label 4450 2800 1    60   ~ 0
nUSART_TX
$Comp
L GND #PWR?
U 1 1 57811765
P 4750 3300
F 0 "#PWR?" H 4750 3050 50  0001 C CNN
F 1 "GND" H 4750 3150 50  0000 C CNN
F 2 "" H 4750 3300 50  0000 C CNN
F 3 "" H 4750 3300 50  0000 C CNN
	1    4750 3300
	1    0    0    -1  
$EndComp
$EndSCHEMATC
