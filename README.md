# ESP32-CAM_ZeppelinCAM
ESP32CAM: Control motors and stream video of a mini Blimp/Zepplin

## Hardware
It is developped and tested on following platforms, but it is easy to adapt for other chips. 
- AI-Thinker ESP32CAM
- XIAO ESP32S3 Sense

## Arduino libraries & version
- ESP32 Arduino board version 2.0.8
- Install following libraries in the Arduin IDE Library manager
  - AsyncTCP: version 1.1.4 by dvarrel
  - ArduinoWebsockets version 0.5.3 by Gil Maimon
  - ESPAsyncWebSrv, version 1.2.6 by dvarrel

## Inspiration
I got inspired by following examples
- The ESP32 Camera example CameraWebServer
- [ESP32-CAM_TANK van PepeTheFroggie](https://github.com/PepeTheFroggie/ESP32CAM_RCTANK)
- [RobotZero One: ESP32-CAM-rc-car](https://robotzero.one/esp32-cam-rc-car/) met software op https://github.com/robotzero1/esp32cam-rc-car
- [Cellphone controlled RC car](https://github.com/neonious/lowjs_esp32_examples/tree/master/neonious_one/cellphone_controlled_rc_car) 
- de joystick gebaseerd op [Kirupa: Create a Draggable Element in JavaScript](https://www.kirupa.com/html5/drag.htm)
