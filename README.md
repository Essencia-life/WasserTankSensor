### Watertank-Sensor and Information distribution
In Essencia, there watertanks. One is directly connected to all pipes / daily usage of water.
this watertank now has gotten some hardware and software to provide the people in essencia the information about the water-level.

## Sender (Sensor and Sender of Information)
The watertank has installed an ESP32-Heltec Board (with LORA-support) with a JSN-SR04T-v3.3 Ultrasonic-Board.
Sensing the Water Level (in Lit of the Watertank) through ultrasonic-sensor.
See "IS SENDER" in Code. it has a display, displaying distance from sensor to water surface. and the watertank-level (in %)

## Receiver (Receiver and provider of Data to cloud)
the receiver is just a connecting gate, which receives the information of the watertank-level (via lora = long-range-communiation).
it has a display (aswell) and is connected to the internet. this receiver (gate) is meant to push water-levels with timestamps and sensor-status into cloud
(t.b.d. implementation)

## Server (not available yet)
is meant to collect the information from Receiver, store it over "longer time" (what ever that means) and provide the "current water-level".
It could potentially also provide better information, like "consumtion", "comparison to previous day", "comparison to week",... etc. (to be done).
