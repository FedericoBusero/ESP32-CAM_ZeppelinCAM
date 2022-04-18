// ESP32 Board manager version 1.0.4
// Arduino IDE: Board = AIThinker ESP32-CAM

#include <ArduinoWebsockets.h> // Install from Arduino library manager : "ArduinoWebsockets" by Gil Maimon, https://github.com/gilmaimon/ArduinoWebsockets
#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer && https://github.com/me-no-dev/AsyncTCP

#include <WiFi.h>
#include <driver/ledc.h>

#define USE_CAMERA
#define DEBUG_SERIAL Serial

#ifdef USE_CAMERA
#include "esp_camera.h"
#endif

// const char* ssid = "NSA"; //Enter SSID
// const char* password = "Orange"; //Enter Password
#define USE_SOFTAP
#define WIFI_SOFTAP_CHANNEL 1 // 1-13
const char ssid[] = "BlimpCam-";
const char password[] = "12345678";

#ifdef USE_SOFTAP
#include <DNSServer.h>
DNSServer dnsServer;
#endif

#include "zeppelincam_html.h" // Do not put html code in ino file to avoid problems with the preprocessor

#ifdef USE_CAMERA

// video streaming setting
#define MIN_TIME_PER_FRAME 200 // minimum time between video frames in ms e.g. minimum 200ms means max 5fps

// ESP32-CAM pin definitions
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

camera_fb_t * fb = NULL;
#endif

using namespace websockets;
WebsocketsServer server;
AsyncWebServer webserver(80);
WebsocketsClient sclient;


// timeoutes
#define TIMEOUT_MS_MOTORS 2500L // Safety shutdown: motors will go to power off position after x milliseconds no message received
#define TIMEOUT_MS_LED 1L        // LED will light up for x milliseconds after message received

long last_activity_message;

// Motor pin allocation
// Lijken te werken: 2, 12, 13, 14, 15
// Lijkt niet te werken: 16 (doet boel crashen)
// Werkt ook: 4 dat is LED_BUILTIN
// Nog niet getest: 0 (daar zit boot flash pin op en is geen pwm denk ik. Ook gebruikt door camera XCLK_GPIO_NUM ?), 1&3 (rx & tx)
/*
   Volgende pinnen gebruikt tijdens booten
   - 0
   - 2
   - 12 If driven High, flash voltage (VDD_SDIO) is 1.8V not default 3.3V. Has internal pull-down, so unconnected = Low = 3.3V. May prevent flashing and/or booting if 3.3V flash is used and this pin is pulled high, causing the flash to brownout. See the ESP32 datasheet for more details.
     Om permanent dit gedrag uit te schakelen: espefuse.py set_flash_voltage 3.3V
   - 15 If driven Low, silences boot messages printed by the ROM bootloader. Has an internal pull-up, so unconnected = High = normal output.
*/

const int fwdPin = 2;  //Forward Motor Pin
const int turnPin = 12;  //Steering Servo Pin
const int upPin = 15;  // Up Pin

const int hbridgePinA = 13; // H-bridge pin A
const int hbridgePinB = 14; // H-bridge pin B

#define PIN_LED 4

int currentspeedforward;
int currentspeedLR;
int servo_angle;
int up_pwm_value;

bool motors_halt;

#define ANALOGWRITE_FREQUENCY 1000
#define ANALOGWRITE_RESOLUTION LEDC_TIMER_12_BIT // LEDC_TIMER_8_BIT
#define PWMRANGE ((1<<ANALOGWRITE_RESOLUTION)-1) // LEDC_TIMER_12_BIT=> 4095; ANALOGWRITE_RANGE LEDC_TIMER_8_BIT => 255

#define LED_BRIGHTNESS_NO_CONNECTION  4
#define LED_BRIGHTNESS_HANDLEMESSAGE  10
#define LED_BRIGHTNESS_BOOT          100
#define LED_BRIGHTNESS_CONNECTED      10
#define LED_BRIGHTNESS_OFF            0

// Channel & timer allocation table

// LEDC_TIMER_0, channels LEDC_CHANNEL_0,LEDC_CHANNEL_1,LEDC_CHANNEL_8,LEDC_CHANNEL_9
// Used by camera
#define TIMER_CAMERA LEDC_TIMER_0
#define CHANNEL_CAMERA   LEDC_CHANNEL_0

// LEDC_TIMER_1, channels LEDC_CHANNEL_2,LEDC_CHANNEL_3, LEDC_CHANNEL_10,LEDC_CHANNEL_11
// Used by analogwrite: Forward & Up motor PWM)
#define CHANNEL_ANALOGWRITE_FORWARD LEDC_CHANNEL_2  // Forward
#define CHANNEL_ANALOGWRITE_UP      LEDC_CHANNEL_3  // Up motor
#define CHANNEL_ANALOGWRITE_LED  10 // LED BUILTIN, LEDC_CHANNEL_10 not definedd ??
// #define CHANNEL_ANALOGWRITE_X    LEDC_CHANNEL_11 // not definedd ??

// Servo's: LEDC_TIMER_2, channels LEDC_CHANNEL_4,LEDC_CHANNEL_5,LEDC_CHANNEL_12,LEDC_CHANNEL_13
// Used by servo
#define CHANNEL_SERVO1   LEDC_CHANNEL_4
// #define CHANNEL_SERVO2   LEDC_CHANNEL_5
// #define CHANNEL_SERVO3   LEDC_CHANNEL_12 // not definedd ??
// #define CHANNEL_SERVO4   LEDC_CHANNEL_13 // not definedd ??

// LEDC_TIMER_3, channels LEDC_CHANNEL_6,LEDC_CHANNEL_7,LEDC_CHANNEL_14,LEDC_CHANNEL_15
// Used for H-bridge
#define CHANNEL_ANALOGWRITE_HBRIDGEA LEDC_CHANNEL_6 // h-bridge pin A
#define CHANNEL_ANALOGWRITE_HBRIDGEB LEDC_CHANNEL_7 // h-bridge pin B
// #define CHANNEL_ANALOGWRITE_X LEDC_CHANNEL_14 // not definedd ??
// #define CHANNEL_ANALOGWRITE_X LEDC_CHANNEL_15 // not definedd ??

// ESP32 analogwrite functions
// void analogwrite_attach(uint8_t pin, ledc_channel_t channel)
void analogwrite_attach(uint8_t pin, int channel)
{
  // TODO pinmode OUTPUT, FUNCION_3
  ledcSetup(channel, ANALOGWRITE_FREQUENCY, ANALOGWRITE_RESOLUTION); //channel, freq, resolution
  ledcAttachPin(pin, channel); // pin, channel
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.print(F("AnalogWrite: PWMRANGE="));
  DEBUG_SERIAL.println(PWMRANGE);
#endif
}

// Arduino like analogWrite
// void analogwrite_channel(ledc_channel_t channel, uint32_t value) {
void analogwrite_channel(int channel, uint32_t value) {
  ledcWrite(channel, value);
}

// ESP32 Servo functions

void servo_attach(uint8_t pin, ledc_channel_t channel)
{
  ledcSetup(channel, 50, LEDC_TIMER_16_BIT); //channel, freq, resolution
  ledcAttachPin(pin, channel); // pin, channel
}

void servo_write_channel(uint8_t channel, uint32_t value, uint32_t valueMax = 180) {
  // calculate duty, 8191 from 2 ^ 13 - 1
  uint32_t duty = (8191 / valueMax) * min(value, valueMax);
  ledcWrite(channel, duty);
}

void updateMotors()
{
  if (motors_halt)
  {
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.println(F("updateMotors motor_halt=true"));
#endif
    // up motor
    analogwrite_channel(CHANNEL_ANALOGWRITE_UP, 0);

    // forward motor
    analogwrite_channel(CHANNEL_ANALOGWRITE_FORWARD, 0);
  }
  else
  {
/*
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.print(F("updateMotors servo_angle="));
    DEBUG_SERIAL.print(servo_angle);
    DEBUG_SERIAL.print(F(" currentspeedforward="));
    DEBUG_SERIAL.print(currentspeedforward);
    DEBUG_SERIAL.print(F(" currentspeedLR="));
    DEBUG_SERIAL.print(currentspeedLR);
    DEBUG_SERIAL.print(F(" up_pwm_value="));
    DEBUG_SERIAL.print(up_pwm_value);
    DEBUG_SERIAL.println();
#endif
*/
    // servo
    servo_write_channel(CHANNEL_SERVO1, servo_angle);
    // hg7881_run(currentspeedLR);
    drv8833_run(currentspeedLR);

    // up motor
    analogwrite_channel(CHANNEL_ANALOGWRITE_UP, up_pwm_value);

    // forward motor
    analogwrite_channel(CHANNEL_ANALOGWRITE_FORWARD, currentspeedforward);
  }
}

void camera_init()
{
#ifdef USE_CAMERA
  camera_config_t config;
  config.ledc_channel = CHANNEL_CAMERA;
  config.ledc_timer = TIMER_CAMERA;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 8000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.printf("Camera init failed with error 0x%x", err);
#endif
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_SVGA);
#endif
}

void hg7881_halt()
{
  analogwrite_channel(CHANNEL_ANALOGWRITE_HBRIDGEA, 0 );
  analogwrite_channel(CHANNEL_ANALOGWRITE_HBRIDGEB, 0 );
}

void hg7881_run(int16_t speed)
{
  if (speed >= 0) {
    analogwrite_channel(CHANNEL_ANALOGWRITE_HBRIDGEA, PWMRANGE ); // direction = forward
    analogwrite_channel(CHANNEL_ANALOGWRITE_HBRIDGEB, (int16_t)PWMRANGE - speed );
  } else {
    analogwrite_channel(CHANNEL_ANALOGWRITE_HBRIDGEA, 0 ); // direction = backward
    analogwrite_channel(CHANNEL_ANALOGWRITE_HBRIDGEB, -speed ); // PWM speed
  }
}

void drv8833_halt()
{
  analogwrite_channel(CHANNEL_ANALOGWRITE_HBRIDGEA, PWMRANGE );
  analogwrite_channel(CHANNEL_ANALOGWRITE_HBRIDGEB, PWMRANGE );
}

void drv8833_run(int16_t speed)
{
  if (speed >= 0) {
    analogwrite_channel(CHANNEL_ANALOGWRITE_HBRIDGEA, speed );
    analogwrite_channel(CHANNEL_ANALOGWRITE_HBRIDGEB, 0 ); // direction = forward
  } else {
    analogwrite_channel(CHANNEL_ANALOGWRITE_HBRIDGEA, 0 ); // direction = backward
    analogwrite_channel(CHANNEL_ANALOGWRITE_HBRIDGEB, -speed );
  }
}


void setup()
{
  delay(200);   
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.begin(115200);
  DEBUG_SERIAL.println(F("\nZeppelinCAM started"));
#endif

  // forward motor PWM
  analogwrite_attach(fwdPin, CHANNEL_ANALOGWRITE_FORWARD ); // pin, channel

  // up motor PWM
  analogwrite_attach(upPin, CHANNEL_ANALOGWRITE_UP); // pin, channel

  // H-bridge
  analogwrite_attach(hbridgePinA, CHANNEL_ANALOGWRITE_HBRIDGEA); // pin, channel
  analogwrite_attach(hbridgePinB, CHANNEL_ANALOGWRITE_HBRIDGEB); // pin, channel

  pinMode(PIN_LED, OUTPUT);
  analogwrite_attach(PIN_LED, CHANNEL_ANALOGWRITE_LED); // pin, channel

  // flash 2 time to show we are rebooting
  analogwrite_channel(CHANNEL_ANALOGWRITE_LED, LED_BRIGHTNESS_BOOT); 
  delay(10);
  analogwrite_channel(CHANNEL_ANALOGWRITE_LED, LED_BRIGHTNESS_OFF); 
  delay(100);
  analogwrite_channel(CHANNEL_ANALOGWRITE_LED, LED_BRIGHTNESS_BOOT); 
  delay(10);
  analogwrite_channel(CHANNEL_ANALOGWRITE_LED, LED_BRIGHTNESS_OFF); 

  // steering servo PWM
  servo_attach(turnPin, CHANNEL_SERVO1); // pin, channel

  init_motors();

  camera_init();

  // Wifi setup
  WiFi.persistent(true);
   
  uint8_t macAddr[6];
  WiFi.macAddress(macAddr);
#if defined(USE_SOFTAP)
  WiFi.disconnect();
  /* set up access point */
  WiFi.mode(WIFI_AP);

  char ssidmac[33];
  sprintf(ssidmac, "%s%02X%02X", ssid, macAddr[4], macAddr[5]); // ssidmac = ssid + 4 hexadecimal values of MAC address
  WiFi.softAP(ssidmac, password, WIFI_SOFTAP_CHANNEL);
  IPAddress apIP = WiFi.softAPIP();
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.print(F("SoftAP SSID="));
  DEBUG_SERIAL.println(ssidmac);
  DEBUG_SERIAL.print(F("IP: "));
  DEBUG_SERIAL.println(apIP);
#endif
  /* Setup the DNS server redirecting all the domains to the apIP */
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);
#else // USE_SOFTAP not defined
  WiFi.softAPdisconnect(true);
  // host_name = "BlimpCam-" + 6 hexadecimal values of MAC address
  char host_name[33];
  sprintf(host_name, "BlimpCam-%02X%02X%02X", macAddr[3], macAddr[4], macAddr[5]);
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.print(F("Hostname: "));
  DEBUG_SERIAL.println(host_name);
#endif
  WiFi.setHostname(host_name);

  // Connect to wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait some time to connect to wifi
  for (int i = 0; i < 15 && WiFi.status() != WL_CONNECTED; i++) {
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.print('.');
#endif
    delay(1000);
  }

#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.print(F("\nWiFi connected - IP address: "));
  DEBUG_SERIAL.println(WiFi.localIP());   // You can get IP address assigned to ESP
#endif

#endif // USE_SOFTAP

  webserver.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.println(F("on HTTP_GET: return"));
#endif
    request->send(200, "text/html", index_html);
  });

  webserver.begin();
  server.listen(82);
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.print(F("Is server live? "));
  DEBUG_SERIAL.println(server.available());
#endif
  last_activity_message = millis();
}

void handleSlider(int value)
{
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.print(F("handleSlider value="));
  DEBUG_SERIAL.println(value);
#endif
  up_pwm_value = map(value, 0, 360, 0, PWMRANGE);
  updateMotors();
}


void handleJoystick(int x, int y)
{
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.print(F("handleJoystick x="));
  DEBUG_SERIAL.print(x);
  DEBUG_SERIAL.print(F(" y="));
  DEBUG_SERIAL.println(y);
#endif
  servo_angle = map(x, -180, 180, 35, 135);

  currentspeedLR = map(x, -180, 180, -PWMRANGE, PWMRANGE);
  currentspeedforward = constrain(map(y, 180, -180, -PWMRANGE, PWMRANGE), 0, PWMRANGE);
  updateMotors();
}

void handle_message(websockets::WebsocketsMessage msg) {
  const char *msgstr = msg.c_str();
  const char *p;

#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.println();
  DEBUG_SERIAL.print(F("handle_message "));
#endif

  int id = atoi(msgstr);
  int param1 = 0;
  int param2 = 0;

  p = strchr(msgstr, ':');
  if (p)
  {
    param1 = atoi(++p);
    p = strchr(p, ',');
    if (p)
    {
      param2 = atoi(++p);
    }
  }

#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.println(msgstr);
  DEBUG_SERIAL.print(F(" id = "));
  DEBUG_SERIAL.print(id);
  DEBUG_SERIAL.print(F(" param1 = "));
  DEBUG_SERIAL.print(param1);
  DEBUG_SERIAL.print(F(" param2 = "));
  DEBUG_SERIAL.println(param2);
#endif

  analogwrite_channel(CHANNEL_ANALOGWRITE_LED, LED_BRIGHTNESS_HANDLEMESSAGE); 

  last_activity_message = millis();

  switch (id)
  {
    case 0:       // ping
      break;

    case 1:
      handleJoystick(param1, param2);
      break;

    case 2: handleSlider(param1);
      break;
  }

  if (motors_halt)
  {
    motors_resume();
  }
}

void motors_pause()
{
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.println(F("motors_pause"));
#endif

  motors_halt = true;
  updateMotors();
}

void motors_resume()
{
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.println(F("motors_resume"));
#endif
  motors_halt = false;
  updateMotors();
}

void init_motors()
{
  currentspeedforward = 0;
  currentspeedLR = 0;
  servo_angle = 90;
  up_pwm_value = 0;

  motors_halt = false;
  updateMotors();
}

void onConnect()
{
  analogwrite_channel(CHANNEL_ANALOGWRITE_LED, LED_BRIGHTNESS_CONNECTED); 

#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.println(F("onConnect"));
#endif
  init_motors();
}

void onDisconnect()
{
#ifdef DEBUG_SERIAL
  DEBUG_SERIAL.println(F("onDisconnect"));
#endif
  init_motors();
}

void loop()
{
  static unsigned long millis_last_camera = 0;
  static int is_connected = 0;

#if defined(USE_SOFTAP)
  dnsServer.processNextRequest();
#endif

  if (millis() > last_activity_message + TIMEOUT_MS_LED)
  {
    analogwrite_channel(CHANNEL_ANALOGWRITE_LED, LED_BRIGHTNESS_OFF);
  }

  if (millis() > last_activity_message + TIMEOUT_MS_MOTORS)
  {

#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.println(F("Safety shutdown ..."));
#endif
    motors_pause();

    last_activity_message = millis();
  }

  if (is_connected)
  {
    if (sclient.available()) { // als return non-nul, dan is er een client geconnecteerd
      sclient.poll(); // als return non-nul, dan is er iets ontvangen

#ifdef USE_CAMERA
      long currentmillis = millis();
      if (currentmillis - millis_last_camera > MIN_TIME_PER_FRAME)
      {
        millis_last_camera = currentmillis;

        fb = esp_camera_fb_get();
        if (fb)
        {
          sclient.sendBinary((const char *)fb->buf, fb->len);
          esp_camera_fb_return(fb);
          fb = NULL;
        }
      }
#endif
      updateMotors();
    }
    else
    {
      // no longer connected
      onDisconnect();
      is_connected = 0;
    }
  }
  if (server.poll()) // if a new socket connection is requested
  {
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.print(F("server.poll is_connected="));
    DEBUG_SERIAL.println(is_connected);
#endif
    sclient = server.accept();
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.println(F("Connection accept"));
#endif
    sclient.onMessage(handle_message);
    onConnect();
    is_connected = 1;
  }

  if (!is_connected)
  {
    analogwrite_channel(CHANNEL_ANALOGWRITE_LED, (millis() % 1000) > 500 ? LED_BRIGHTNESS_NO_CONNECTION : LED_BRIGHTNESS_OFF); 
  }
  delay(2);
}
