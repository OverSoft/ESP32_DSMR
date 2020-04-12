# Dutch smart meter reader with LCD visualization (for ESP32)
Copyright (c) 2020 Laurens Leemans - OverSoft

Displays current power usage in kW and shows your current daily usage.  
Also provides a TCP port for Domoticz to connect to, to log the data.  
Includes an OTA update function, via HTTP.

The webserver also provides easy access to current usage, curreny daily usage and last received message.
* http://IP/        (Last received message, current usage and current daily usage and link to update page)
* http://IP/current (Current usage in kW)
* http://IP/day     (Current daily usage in kWh)

Domoticz can connect via the "P1 Smart Meter with LAN interface" plugin.
Just add the plugin, fill in the IP and port (8088).


I created this project for use on the Xinyuan TTGO-T  
https://github.com/Xinyuan-LilyGO/TTGO-T-Display  
It's an ESP32 dev-board with a 240x135 LCD attached.  

It should run on any ESP32 with a LCD screen connected over SPI that's supported by the TFT_eSPI.
If you use an LCD screen with a different resolution, you might need to tweak a lot of positions and the dial JPG.

The dial is calibrated to -6kW max return power and +18kW max consumption power. 
This is the max consumption for a standard 3x25A Dutch power connection.
6kW return power is based on a 20 panel solar panel setup.
If you want to adjust this, make a new dial JPG and alter the values below.

Configure your SSID, password and serial rx pin (tx pin is not used, but still left in) below.
The serial data should be inverted as with any DSMR connection. See README.MD for an example circuit. (TODO)


### To use, install the following libraries in the Arduino IDE:
* The ESP32 libraries
* TFT_eSPI
* TJpg_Decoder
* arduino-dmsr (https://github.com/matthijskooijman/arduino-dsmr)

If you're having trouble flashing this to an ESP32, set the partition scheme to "Minimal SPIFFS with OTA".


## Used libraries:

arduino-dmsr by Matthijs Kooijman (parses the smart meter messages)  
https://github.com/matthijskooijman/arduino-dsmr

TFT_eSPI by Bodmer (LCD library to draw things to a LCD screen)  
https://github.com/Bodmer/TFT_eSPI

TJpg_Decoder by Bodmer (decodes JPGs in program memory)  
https://github.com/Bodmer/TJpg_Decoder
