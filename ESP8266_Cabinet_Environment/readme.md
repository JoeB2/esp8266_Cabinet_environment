This ESP8266 project controls two relays intended to turn on/off a heater(69w light bulb) and a small dehumidifier.
The MCU hosts three web pages and an OTA page.
The root page shows tem & humidity & relay status
The Settings page is for setting temp & humidity thresholds for turning on/off the Relays
The WiFi page allow entry of the SSID/PWD for a network and a fixed IP if so desired.

The MCU attemps to logon to the last stored SSID.
  Upon failing to log on the MCU initiates an Access Point "AP T/H: 10.0.1.14"
  Help will "uin-hide" the options for WiFi and settings and OTA buttons.

I am using this in a cabinet where I store things that are affected by humidity.

This version is for an esp01 / there is another version for ESP32.
Modify this one's platformio.ini for a different esp8266.
Remember to fixup the DHTPIN, HUMID_PIN, and HEAT_PIN defines for your implementation.
