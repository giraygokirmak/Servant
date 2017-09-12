/* Servant PowerPlug (AVR/RPI/BBB/LINUX) Firmware
 * Copyright (C) 2017 by Macit Giray Gokirmak (giray@gokirmak.gen.tr)
 *
 * This file is part of the Servant PowerPlug Firmware
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>. 
 */

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <Ticker.h>
#include <ArduinoOTA.h>

#define LED_RELAY_STATE    false
#define HOSTNAME           "Servant"
#define RESET_BUTTON       2
#define LED                1
#define EEPROM_SALT 12661

const int RELAY_PINS[4] =    {3, 3, 3, 3};
int buttonState = LOW;
//int relayState = HIGH;  // If you want initial switch state ON, uncomment this line
const int CMD_WAIT = 0;
const int CMD_BUTTON_CHANGE = 1;
int cmd = CMD_WAIT;
static long startPress = 0;
bool shouldSaveConfig = false;

typedef struct {
  char  bootState[4]      = "off";
  char  blynkToken[33]    = "token";
  char  blynkServer[33]   = "iot.yourserver.com.tr";
  char  blynkPort[6]      = "8442";
  int   salt              = EEPROM_SALT;
} WMSettings;

WMSettings settings;
Ticker ticker;

String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void tick()
{
  int state = digitalRead(LED);
  digitalWrite(LED, !state);
}

void configModeCallback (WiFiManager *myWiFiManager) {
  ticker.attach(0.2, tick);
}

void updateBlynk(int channel) {
  int state = digitalRead(RELAY_PINS[channel]);
  Blynk.virtualWrite(channel * 5 + 4, state * 255);
}

void setState(int state, int channel) {
  digitalWrite(RELAY_PINS[channel], state);
  if (LED_RELAY_STATE) {
    digitalWrite(LED, (state + 1) % 2);
  }
  updateBlynk(channel);
}

void turnOn(int channel = 0) {
  int relayState = HIGH;
  setState(relayState, channel);

  EEPROM.begin(512);
  EEPROM.get(0, settings);
  EEPROM.end();

  strcpy(settings.bootState, "on");

  EEPROM.begin(512);
  EEPROM.put(0, settings);
  EEPROM.end();
}

void turnOff(int channel = 0) {
  int relayState = LOW;
  setState(relayState, channel);

  EEPROM.begin(512);
  EEPROM.get(0, settings);
  EEPROM.end();

  strcpy(settings.bootState, "off");

  EEPROM.begin(512);
  EEPROM.put(0, settings);
  EEPROM.end();

}

void toggleState() {
  cmd = CMD_BUTTON_CHANGE;
}

void saveConfigCallback () {
  shouldSaveConfig = true;
}

void toggle(int channel = 0) {
  int relayState = digitalRead(RELAY_PINS[channel]) == HIGH ? LOW : HIGH;
  setState(relayState, channel);
}

void restart() {
  ESP.reset();
  delay(1000);
}

void reset() {
  WiFi.disconnect();
  delay(1000);
  ESP.reset();
  delay(1000);
}

BLYNK_WRITE_DEFAULT() {
  int pin = request.pin;
  int channel = pin / 5;
  int action = pin % 5;
  int a = param.asInt();
  if (a != 0) {
    switch (action) {
      case 0:
        turnOff(channel);
        break;
      case 1:
        turnOn(channel);
        break;
      case 2:
        toggle(channel);
        break;
      default:
        break;
    }
  }
}

BLYNK_READ_DEFAULT() {
  int pin = request.pin;
  int channel = pin / 5;
  int action = pin % 5;
  Blynk.virtualWrite(pin, digitalRead(RELAY_PINS[channel]));
}

BLYNK_WRITE(30) {
  int a = param.asInt();
  if (a != 0) {
    restart();
  }
}

BLYNK_WRITE(31) {
  int a = param.asInt();
  if (a != 0) {
    reset();
  }
}

void setup()
{
  pinMode(LED, OUTPUT);  
  ticker.attach(0.6, tick);

  const char *hostname = HOSTNAME;

  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(180);

  EEPROM.begin(512);
  EEPROM.get(0, settings);
  EEPROM.end();

  if (settings.salt != EEPROM_SALT) {
    WMSettings defaults;
    settings = defaults;
  }

  WiFiManagerParameter custom_boot_state("boot-state", "on/off on boot", settings.bootState, 33);
  wifiManager.addParameter(&custom_boot_state);

  WiFiManagerParameter custom_blynk_text("<br/>Blynk config. <br/> No token to disable.<br/>");
  wifiManager.addParameter(&custom_blynk_text);

  WiFiManagerParameter custom_blynk_token("blynk-token", "blynk token", settings.blynkToken, 33);
  wifiManager.addParameter(&custom_blynk_token);

  WiFiManagerParameter custom_blynk_server("blynk-server", "blynk server", settings.blynkServer, 33);
  wifiManager.addParameter(&custom_blynk_server);

  WiFiManagerParameter custom_blynk_port("blynk-port", "port", settings.blynkPort, 6);
  wifiManager.addParameter(&custom_blynk_port);

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.autoConnect(hostname)) {
    ESP.reset();
    delay(1000);
  }
  if (shouldSaveConfig) {
    strcpy(settings.bootState, custom_boot_state.getValue());
    strcpy(settings.blynkToken, custom_blynk_token.getValue());
    strcpy(settings.blynkServer, custom_blynk_server.getValue());
    strcpy(settings.blynkPort, custom_blynk_port.getValue());

    EEPROM.begin(512);
    EEPROM.put(0, settings);
    EEPROM.end();
  }

  Blynk.config(settings.blynkToken, settings.blynkServer, atoi(settings.blynkPort));

  //OTA
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.begin();

  ticker.detach();

  pinMode(RESET_BUTTON, INPUT);
  attachInterrupt(RESET_BUTTON, toggleState, CHANGE);

  pinMode(RELAY_PINS[0], OUTPUT);

  if (strcmp(settings.bootState, "on") == 0) {
    turnOn();
  } else {
    turnOff();
  }

  if (!LED_RELAY_STATE) {
    digitalWrite(LED, LOW);
  }
}

void loop()
{

  ArduinoOTA.handle();

  Blynk.run();

  switch (cmd) {
    case CMD_WAIT:
      break;
    case CMD_BUTTON_CHANGE:
      int currentState = digitalRead(RESET_BUTTON);
      if (currentState != buttonState) {
        if (buttonState == LOW && currentState == HIGH) {
          long duration = millis() - startPress;
          if (duration < 1000) {
            toggle();
          } else if (duration < 5000) {
            restart();
          } else if (duration < 60000) {
            reset();
          }
        } else if (buttonState == HIGH && currentState == LOW) {
          startPress = millis();
        }
        buttonState = currentState;
      }
      break;
  }
}

