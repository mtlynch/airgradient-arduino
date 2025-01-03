/*
Important: This code is only for the DIY PRO / AirGradient ONE PCB Version 9 with the ESP-C3 MCU.

It is a high quality sensor showing PM2.5, CO2, TVOC, NOx, Temperature and Humidity on a small display and LEDbar and can send data over Wifi.

This version has been modified by Michael Lynch from the original AirGradient code.

Build Instructions: https://www.airgradient.com/open-airgradient/instructions/

Kits (including a pre-soldered version) are available: https://www.airgradient.com/indoor/

The codes needs the following libraries installed:
"WifiManager by tzapu, tablatronix" tested with version 2.0.11-beta
"U8g2" by oliver tested with version 2.32.15
"Sensirion I2C SGP41" by Sensation Version 0.1.0
"Sensirion Gas Index Algorithm" by Sensation Version 3.2.1
"pms" by Markusz Kakl version 1.1.0
"S8_UART" by Josep Comas Version 1.0.1
"Arduino-SHT" by Johannes Winkelmann Version 1.2.2
"Adafruit NeoPixel" by Adafruit Version 1.11.0

Configuration:
Please set in the code below the configuration parameters.

If you have any questions please visit our forum at https://forum.airgradient.com/

If you are a school or university contact us for a free trial on the AirGradient platform.
https://www.airgradient.com/

CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License

*/

#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <HardwareSerial.h>
#include <HTTPClient.h>
#include <NOxGasIndexAlgorithm.h>
#include <PMS.h>
#include <s8_uart.h>
#include <SHTSensor.h>
#include <SensirionI2CSgp41.h>
#include <VOCGasIndexAlgorithm.h>
#include <U8g2lib.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <WString.h>

#include "interval-counter.h"

#define DEBUG true

#define I2C_SDA 7
#define I2C_SCL 6

Adafruit_NeoPixel pixels(11, 10, NEO_GRB + NEO_KHZ800);
SensirionI2CSgp41 sgp41;
VOCGasIndexAlgorithm voc_algorithm;
NOxGasIndexAlgorithm nox_algorithm;
SHTSensor sht;

PMS pms1(Serial0);

S8_UART* sensor_S8;
S8_sensor sensor;

// time in seconds needed for NOx conditioning
uint16_t conditioning_s = 10;

// for persistent saving and loading
int addr = 4;
byte value;

// Display bottom right
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// set to true to switch from Celcius to Fahrenheit
boolean inF = true;

// PM2.5 in US AQI (default ug/m3)
boolean inUSAQI = false;

// use RGB LED Bar
boolean useRGBledBar = true;

// set to true if you want to connect to wifi. You have 60 seconds to connect. Then it will go into an offline mode.
boolean connectWIFI = true;

unsigned long loopCount = 0;

unsigned long currentMillis = 0;

const float INVALID_READING = -10001.0;

IntervalCounter oledIntervalCounter(5 * 1000);
IntervalCounter sendToServerIntervalCounter(10 * 1000);
IntervalCounter tvocIntervalCounter(1 * 1000);
int TVOC = -1;
int NOX = -1;

IntervalCounter co2IntervalCounter(5 * 1000);
int co2 = 0;

IntervalCounter pmIntervalCounter(5 * 1000);
uint16_t pm25 = 0;
uint16_t pm01 = 0;
uint16_t pm10 = 0;
boolean pmReadSucceeded = false;

IntervalCounter tempHumIntervalCounter(5 * 1000);
float temp;
float humidity;

const unsigned short WATCHDOG_PIN = 2;

int buttonConfig = 0;
int lastState = LOW;
int currentState;
unsigned long pressedTime = 0;
unsigned long releasedTime = 0;

void setup() {
  if (DEBUG) {
    Serial.begin(115200);
    // see https://github.com/espressif/arduino-esp32/issues/6983
    Serial.setTxTimeoutMs(0); // <<<====== solves the delay issue
  }

  Wire.begin(I2C_SDA, I2C_SCL);
  pixels.begin();
  pixels.clear();

  Serial1.begin(9600, SERIAL_8N1, 0, 1);
  Serial0.begin(9600);
  u8g2.begin();

  updateOLED2("Warming up", "Serial Number:", getNormalizedMac());
  sgp41.begin(Wire);
  delay(300);

  sht.init(Wire);
  //sht.setAccuracy(SHTSensor::SHT_ACCURACY_MEDIUM);
  delay(300);

  //init Watchdog
  pinMode(WATCHDOG_PIN, OUTPUT);
  digitalWrite(WATCHDOG_PIN, LOW);

  sensor_S8 = new S8_UART(Serial1);

  EEPROM.begin(512);
  delay(500);

  // push button
  pinMode(9, INPUT_PULLUP);

  buttonConfig = String(EEPROM.read(addr)).toInt();
  if (buttonConfig > 7) buttonConfig = 0;
  Serial.printf("buttonConfig: %d\n", buttonConfig);
  delay(400);

  updateOLED2("Press Button", "for LED test &", "offline mode");
  delay(2000);
  currentState = digitalRead(9);
  if (currentState == LOW) {
    ledTest();
    return;
  }

  updateOLED2("Press Button", "Now for", "Config Menu");
  delay(2000);
  currentState = digitalRead(9);
  if (currentState == LOW) {
    updateOLED2("Entering", "Config Menu", "");
    delay(3000);
    lastState = HIGH;
    inConf();
  }

  if (connectWIFI) {
    connectToWifi();
  }
  if (WiFi.status() == WL_CONNECTED) {
    // IPAddress has special support for Serial.print and Serial.println, but
    // it's hard to convert it to a string, so we use this unusual format to
    // print.
    Serial.print("WiFi connected! IP address: ");
    Serial.println(WiFi.localIP());
  }
  updateOLED2("Warming Up", "Serial Number:", getNormalizedMac());
}

void loop() {
  currentMillis = millis();
  Serial.printf("Starting loop %u, millis=%u\n", loopCount, currentMillis);
  updateTVOC();
  updateOLED();
  updateCo2();
  updatePm();
  updateTempHum();
  sendToServer();
  loopCount++;
}

void updateTVOC() {
  uint16_t error;
  char errorMessage[256];
  uint16_t defaultRh = 0x8000;
  uint16_t defaultT = 0x6666;
  uint16_t srawVoc = 0;
  uint16_t srawNox = 0;
  uint16_t defaultCompenstaionRh = 0x8000; // in ticks as defined by SGP41
  uint16_t defaultCompenstaionT = 0x6666; // in ticks as defined by SGP41
  uint16_t compensationRh = 0; // in ticks as defined by SGP41
  uint16_t compensationT = 0; // in ticks as defined by SGP41

  delay(1000);

  compensationT = static_cast < uint16_t > ((temp + 45) * 65535 / 175);
  compensationRh = static_cast < uint16_t > (humidity * 65535.0 / 100.0);

  if (conditioning_s > 0) {
    error = sgp41.executeConditioning(compensationRh, compensationT, srawVoc);
    conditioning_s--;
  } else {
    error = sgp41.measureRawSignals(compensationRh, compensationT, srawVoc,
      srawNox);
  }

  if (!tvocIntervalCounter.IsTimeToFire(currentMillis)) {
    return;
  }

  if (error) {
    Serial.println("Failed to read TVOC");
    TVOC = -1;
    NOX = -1;
  } else {
    TVOC = voc_algorithm.process(srawVoc);
    NOX = nox_algorithm.process(srawNox);
    Serial.printf("tvoc=%d\n", TVOC);
  }
}

void updateCo2() {
  if (!co2IntervalCounter.IsTimeToFire(currentMillis)) {
    return;
  }
  co2 = sensor_S8->get_co2();
  Serial.printf("co2=%d\n", co2);
}

void updatePm() {
  if (!pmIntervalCounter.IsTimeToFire(currentMillis)) {
    return;
  }

  PMS::DATA data;
  pmReadSucceeded = pms1.readUntil(data, 2000);
  if (!pmReadSucceeded) {
    return;
  }
  pm01 = data.PM_AE_UG_1_0;
  pm25 = data.PM_AE_UG_2_5;
  pm10 = data.PM_AE_UG_10_0;
}

void updateTempHum() {
  if (!tempHumIntervalCounter.IsTimeToFire(currentMillis)) {
    return;
  }

  if (sht.readSample()) {
    temp = sht.getTemperature();
    humidity = sht.getHumidity();
  } else {
    Serial.print("Error in readSample()\n");
    temp = INVALID_READING;
    humidity = INVALID_READING;
  }
}

void updateOLED() {
  if (oledIntervalCounter.IsTimeToFire(currentMillis)) {
    updateOLED3();
    setRGBledCO2color(co2);
  }
}

void inConf() {
  currentState = digitalRead(9);

  if (currentState) {
    Serial.println("currentState: high");
  } else {
    Serial.println("currentState: low");
  }

  if (lastState == HIGH && currentState == LOW) {
    pressedTime = millis();
  } else if (lastState == LOW && currentState == HIGH) {
    releasedTime = millis();
    long pressDuration = releasedTime - pressedTime;
    if (pressDuration < 1000) {
      buttonConfig = buttonConfig + 1;
      if (buttonConfig > 7) buttonConfig = 0;
    }
  }

  if (lastState == LOW && currentState == LOW) {
    long passedDuration = millis() - pressedTime;
    if (passedDuration > 4000) {
      updateOLED2("Saved", "Release", "Button Now");
      delay(1000);
      updateOLED2("Rebooting", "in", "5 seconds");
      delay(5000);
      EEPROM.write(addr, char(buttonConfig));
      EEPROM.commit();
      delay(1000);
      ESP.restart();
    }

  }
  lastState = currentState;
  delay(100);
  inConf();
}

void updateOLED2(String ln1, String ln2, String ln3) {
  char buf[9];
  u8g2.firstPage();
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_t0_16_tf);
    u8g2.drawStr(1, 10, String(ln1).c_str());
    u8g2.drawStr(1, 30, String(ln2).c_str());
    u8g2.drawStr(1, 50, String(ln3).c_str());
  } while (u8g2.nextPage());
}

void updateOLED3() {
  Serial.printf("in updateOLED3, inF=%d\n", inF);
  char buf[9];
  u8g2.firstPage();
  u8g2.firstPage();
  const uint screenWidth = 128;
  do {

    u8g2.setFont(u8g2_font_t0_16_tf);

    if (inF) {
      if (temp != INVALID_READING) {
        float tempF = (temp * 9 / 5) + 32;
        sprintf(buf, "%.1f°F", tempF);
      } else {
        sprintf(buf, "-°F");
      }
    } else {
      if (temp != INVALID_READING) {
        sprintf(buf, "%.1f°C", temp);
      } else {
        sprintf(buf, "-°C");
      }
    }
    u8g2.drawUTF8(1, 10, buf);

    if (humidity != INVALID_READING) {
      sprintf(buf, "%.1f%%", humidity);
    } else {
      sprintf(buf, " -%%");
    }
    if (humidity > 99.0) {
      u8g2.drawStr(screenWidth - 31, 10, buf);
    } else {
      u8g2.drawStr(screenWidth - 39, 10, buf);
      // there might also be single digits, not considered, sprintf might actually support a leading space
    }

    u8g2.drawLine(1, 13, screenWidth, 13);
    u8g2.setFont(u8g2_font_t0_12_tf);
    u8g2.drawUTF8(1, 27, "CO2");
    u8g2.setFont(u8g2_font_t0_22b_tf);
    if (co2 > 0) {
      sprintf(buf, "%d", co2);
    } else {
      sprintf(buf, "%s", "-");
    }
    u8g2.drawStr(1, 48, buf);
    u8g2.setFont(u8g2_font_t0_12_tf);
    u8g2.drawStr(1, 61, "ppm");
    u8g2.drawLine(45, 15, 45, 64);
    u8g2.setFont(u8g2_font_t0_12_tf);
    u8g2.drawStr(48, 27, "PM2.5");
    u8g2.setFont(u8g2_font_t0_22b_tf);

    if (inUSAQI) {
      if (pm25 >= 0) {
        sprintf(buf, "%d", PM_TO_AQI_US(pm25));
      } else {
        sprintf(buf, "%s", "-");
      }
      u8g2.drawStr(48, 48, buf);
      u8g2.setFont(u8g2_font_t0_12_tf);
      u8g2.drawUTF8(48, 61, "AQI");
    } else {
      if (pm25 >= 0) {
        sprintf(buf, "%d", pm25);
      } else {
        sprintf(buf, "%s", "-");
      }
      u8g2.drawStr(48, 48, buf);
      u8g2.setFont(u8g2_font_t0_12_tf);
      u8g2.drawUTF8(48, 61, "ug/m³");
    }

    u8g2.drawLine(82, 15, 82, 64);
    u8g2.setFont(u8g2_font_t0_12_tf);
    u8g2.drawStr(85, 27, "TVOC:");
    if (TVOC >= 0) {
      sprintf(buf, "%d", TVOC);
    } else {
      sprintf(buf, "%s", "-");
    }
    u8g2.drawStr(85, 39, buf);
    u8g2.drawStr(85, 53, "NOx:");
    if (NOX >= 0) {
      sprintf(buf, "%d", NOX);
    } else {
      sprintf(buf, "%s", "-");
    }
    u8g2.drawStr(85, 63, buf);

  } while (u8g2.nextPage());
}

String formatInfluxDbLineInt(String label, int value, String serial) {
  String tags = ",serial=" + serial;
  return label + tags + " value=" + String(value);
}

String formatInfluxDbLineUInt16(String label, uint16_t value, String serial) {
  String tags = ",serial=" + serial;
  return label + tags + " value=" + String(value);
}

String formatInfluxDbLineFloat(String label, float value, String serial) {
  String tags = ",serial=" + serial;
  return label + tags + " value=" + String(value);
}

String createInfluxDbPayload(String serial, int wifi_rssi, int co2, float humidity, float temp_c, uint16_t pm01,  uint16_t pm10, uint16_t pm25, int tvoc, int nox) {
  String payload = formatInfluxDbLineInt("wifi_rssi", wifi_rssi, serial);

  if (temp_c != INVALID_READING) {
    payload += "\n" + formatInfluxDbLineFloat("temp", temp_c, serial);
  }

  if (co2 > 0) {
    payload += "\n" + formatInfluxDbLineInt("co2", co2, serial);
  }
  if (humidity > 0) {
    payload += "\n" + formatInfluxDbLineFloat("humidity", humidity, serial);
  }
  if (pmReadSucceeded) {
    payload += "\n" + formatInfluxDbLineUInt16("pm01", pm01, serial);
    payload += "\n" + formatInfluxDbLineUInt16("pm10", pm10, serial);
    payload += "\n" + formatInfluxDbLineUInt16("pm25", pm25, serial);
  }
  if (tvoc > 0) {
    payload += "\n" + formatInfluxDbLineInt("tvoc_index", tvoc, serial);
  }
  if (nox > 0) {
    payload += "\n" + formatInfluxDbLineInt("nox_index", nox, serial);
  }

  return payload;
}

void sendToServer() {
  // TODO: Replace with environment variable.
  //const String POSTURL = "http://10.0.0.13:8086/write?db=airgradient";

  if (!sendToServerIntervalCounter.IsTimeToFire(currentMillis)) {
    return;
  }
  String payload = createInfluxDbPayload(getNormalizedMac(), WiFi.RSSI(), co2, humidity, temp, pm01, pm10, pm25, TVOC, NOX);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi Disconnected");
    return;
  }

  Serial.println("Sending payload...");
  Serial.println(payload);
  //Serial.printf("POSTing payload to %s\n", POSTURL.c_str());
  WiFiClient client;
  HTTPClient http;
  //http.begin(client, POSTURL);
  http.begin(client, "10.0.0.13", 8086, "/write?db=airgradient");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpCode = http.POST(payload);
  if (httpCode >= 0) {
    Serial.printf("Response from server: %d\n", httpCode);
  } else {
    Serial.printf("POST failed with error: %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
  }
  http.end();
  resetWatchdog();
}

void resetWatchdog() {
  Serial.println("Watchdog reset");
  digitalWrite(WATCHDOG_PIN, HIGH);
  delay(20);
  digitalWrite(WATCHDOG_PIN, LOW);
}

// Wifi Manager
void connectToWifi() {
  WiFiManager wifiManager;
  String HOTSPOT = "AG-" + getNormalizedMac();
  updateOLED2("180s to connect", "to Wifi Hotspot", HOTSPOT);
  wifiManager.setTimeout(180);
  if (!wifiManager.autoConnect((const char*)HOTSPOT.c_str())) {
    Serial.println("failed to connect and hit timeout");
    delay(6000);
  }
}

String getNormalizedMac() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  return mac;
}

void setRGBledCO2color(int co2Value) {
  if (co2Value >= 300 && co2Value < 800) setRGBledColor('g');
  if (co2Value >= 800 && co2Value < 1000) setRGBledColor('y');
  if (co2Value >= 1000 && co2Value < 1500) setRGBledColor('o');
  if (co2Value >= 1500 && co2Value < 2000) setRGBledColor('r');
  if (co2Value >= 2000 && co2Value < 3000) setRGBledColor('p');
  if (co2Value >= 3000 && co2Value < 10000) setRGBledColor('z');
}

void setRGBledColor(char color) {
  if (useRGBledBar) {
    //pixels.clear();
    switch (color) {
    case 'g':
      for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 255, 0));
        delay(30);
        pixels.show();
      }
      break;
    case 'y':
      for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 255, 0));
        delay(30);
        pixels.show();
      }
      break;
    case 'o':
      for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 128, 0));
        delay(30);
        pixels.show();
      }
      break;
    case 'r':
      for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 0, 0));
        delay(30);
        pixels.show();
      }
      break;
    case 'b':
      for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 0, 255));
        delay(30);
        pixels.show();
      }
      break;
    case 'w':
      for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 255, 255));
        delay(30);
        pixels.show();
      }
      break;
    case 'p':
      for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(153, 0, 153));
        delay(30);
        pixels.show();
      }
      break;
    case 'z':
      for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(102, 0, 0));
        delay(30);
        pixels.show();
      }
      break;
    case 'n':
      for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 0, 0));
        delay(30);
        pixels.show();
      }
      break;
    default:
      // if nothing else matches, do the default
      // default is optional
      break;
    }
  }
}

void ledTest() {
  updateOLED2("LED Test", "running", ".....");
  for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 0, 0));
        delay(30);
        pixels.show();
      }
  delay(500);
  for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 255, 0));
        delay(30);
        pixels.show();
      }
  delay(500);
  for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 0, 255));
        delay(30);
        pixels.show();
      }
  delay(500);
  for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(255, 255, 255));
        delay(30);
        pixels.show();
      }
  delay(500);
  for (int i = 0; i < 11; i++) {
        pixels.setPixelColor(i, pixels.Color(0, 0, 0));
        delay(30);
        pixels.show();
      }
  delay(500);
}

// Calculate PM2.5 US AQI
int PM_TO_AQI_US(int pm02) {
  if (pm02 <= 12.0) return ((50 - 0) / (12.0 - .0) * (pm02 - .0) + 0);
  else if (pm02 <= 35.4) return ((100 - 50) / (35.4 - 12.0) * (pm02 - 12.0) + 50);
  else if (pm02 <= 55.4) return ((150 - 100) / (55.4 - 35.4) * (pm02 - 35.4) + 100);
  else if (pm02 <= 150.4) return ((200 - 150) / (150.4 - 55.4) * (pm02 - 55.4) + 150);
  else if (pm02 <= 250.4) return ((300 - 200) / (250.4 - 150.4) * (pm02 - 150.4) + 200);
  else if (pm02 <= 350.4) return ((400 - 300) / (350.4 - 250.4) * (pm02 - 250.4) + 300);
  else if (pm02 <= 500.4) return ((500 - 400) / (500.4 - 350.4) * (pm02 - 350.4) + 400);
  else return 500;
};
