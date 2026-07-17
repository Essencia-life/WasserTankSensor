# Watertank-Sensor and Information distribution
In Essencia, there some watertanks. One (which is equiped with this logic) is directly connected to all pipes / daily usage of water. 
It is the watertank of main attraction / seek of information of the status of this.
This watertank now has gotten some hardware and software (this Repo) to provide the people in essencia the information about the water-level.

## Sender (Sensor and Sender of Information)
The watertank has installed an ESP32-Heltec Board (with LORA-support) with a JSN-SR04T-v3.3 Ultrasonic-Board.
Sensing the Water Level (in Lit of the Watertank) through ultrasonic-sensor.
See "IS SENDER" in Code. it has a display, displaying distance from sensor to water surface. and the watertank-level (in %)

### Architecture / Graph
how the Sensor is installed in Watertank. Also showing which "distances" are used/meant in sw.
<img width="434" height="220" alt="Watertank_Sketch_Mounting_and_Distances" src="https://github.com/user-attachments/assets/f64ce825-2ba4-4d1e-ad7b-b6d5713ad396" />

## Receiver (Receiver and provider of Data to cloud)
the receiver is just a connecting gate, which receives the information of the watertank-level (via lora = long-range-communiation).
it has a display (aswell) and is connected to the internet. this receiver (gate) is meant to push water-levels with timestamps and sensor-status into cloud
(t.b.d. implementation)

## Server (not available yet)
is meant to collect the information from Receiver, store it over "longer time" (what ever that means) and provide the "current water-level".
It could potentially also provide better information, like "consumtion", "comparison to previous day", "comparison to week",... etc. (to be done).


### VSC (build info)
1. build with plattformio, two #IFDEFINES (IS_RECEIVER, IS_SENDER) and two environments created.
if you build sw for (S) or (R), take care to chose "environment (sender/receiver)" and "define IS_SENDER / IS_RECEIVER" before klicking "build and flash" (->)
<img width="866" height="398" alt="Bildschirmfoto 2026-07-17 um 17 08 42" src="https://github.com/user-attachments/assets/72fe7bc0-0625-456b-9df8-0bd6ac3ff305" />
(picture markes build for "sender". switch both to "receiver" if building and flashing receiver objects.

2. install the "libs" from platformio.ini (heltec, arduino, etc..) for building the project, if not done yet.
