Traffic Plan
############

We have to assume a best-case throughput of 16kbit/s (with encryption) or 21kbit/s (without encryption).
Each RF packet can hold up to 66 bytes (with encryption) or 84 bytes (without encryption).

Streams
=======

1. {Accel,Magnetometer}_{X,Y,Z}: up to 150 B/s each (assuming 66 bytes MTU) or 145 B/s each (assuming 84 bytes MTU)

   -> 900 B/s total (with encryption) or 870 B/s total (without encryption)

2. DS18B20 sensors: (1 byte header +) 2 bytes timestamp + 8 bytes ID + 2 bytes raw value = 13 bytes / sample. sampling rate ≈0.1 Hz

   Assuming up to 5 sensors (very generous, and questionable that we can sample five sensors every 10 seconds)

   -> 6.5 B/s ≈ 7 B/s

3. Light sensor: (1 byte header +) 2 bytes timestamp + (2 bytes raw value / channel * 4 channels =) 8 bytes = 11 bytes / sample. sampling rate ≈0.1 Hz

   One sensor only

   -> 1.1 B/s ≈ 2 B/s

4. Noise sensor: (1 byte header +) 2 bytes timestamp + 2 bytes raw value = 5 bytes / sample. sampling rate ≈ 1 Hz

   One sensor only

   -> 5.0 B/s ≈ 5 B/s

Total traffic: 914 B/s (with encryption) or 884 B/s (without encryption)

This leaves us with approximately 1 kB/s (with encryption) or 1.7 kB/s (without encryption) headroom.
Relatively, that is a headroom of factor 1.19 (with encryption) or 1.97 (without encryption).
Both isn’t great, but it is what we have to deal with now. If nothing else helps, we have to reduce the data rate from the IMU.
