////// Defines
#define HOST_NAME "heatpump"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
HTTPClient http;
// Webserver
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "index.h"
AsyncWebServer server(80);

// Cylinder temperature:
#include <OneWire.h>
#include <DallasTemperature.h>
float cyltemp = -99.99;
uint8_t cyltempint = 0;
uint8_t cyltempdec = 0;
OneWire oneWire(1);
DallasTemperature sensors(&oneWire);

// SSID and password
const char* ssid = "wifiSSID"; //your wifi ssid
const char* password = "wifiPass"; //your wifi password

//emon stuff
#define EMONAPI "xxx" //your emonpi write api

// Time
uint32_t mLastTime = 0;
uint32_t mTimeSeconds = 0;

//heartbeat leds
const long heartbeat = 2000; //blink the esp led this often
long blinktime = 0;

// heatpump status
byte POWER = 0;
byte CH = 0; //Central heating demand
byte DHW = 0; //DHW demand
int8_t OAT = 0; //outdoor air temp *2 to give 1/2 degree resolution
byte FROST = 0; //Frost protection active
byte DEFROST = 0; //Defrost cycle active
byte LOWTARRIF = 0; //Low tarrif mode //not yet implemented
byte NIGHTMODE = 0; //Night mode //not yet implemented
byte FAN = 0; //fan on
byte PUMP = 0; //circulating pump on
byte COMPRESSOR = 0; //compressor running
byte COMPOVERRUN = 0; //compressor overrun
int8_t CIRCWATERRETURN = 0; //Circulating water return temperature, 1degC
uint8_t COMPFREQ = 0; //Compressor operating frequency, 1Hz
int8_t DISCHARGTEMP = 0; //Refrigerant discharge temperature, 1degC
uint8_t POWERCONS = 0; //Power consumption, 100W
uint8_t FANSPEED = 0; //Fan speed, 10rpm
int8_t DEFRTEMP = 0; //Defrost temperature, 1degC
//int8_t OATREQ = 0; //Outside Air Temp, requested, 1degC. Probably not used
uint8_t PUMPSPEED = 0; //Water circulating pump rpm, 100rpm
int8_t SUCTIONTEMP = 0; //Refrigerant suction temperature, 1degC
int8_t CIRCWATERFLOW = 0; //Circulating water return temperature, 1degC

// http server handlers
uint8_t buttonpause = 0; //stop pressing buttons?

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void handleRoot(AsyncWebServerRequest *request) {
  digitalWrite(LED_BUILTIN, 0);
  request->send_P(200, "text/html", MAIN_page);
  digitalWrite(LED_BUILTIN, 1);
}

void handledata(AsyncWebServerRequest *request) {
  digitalWrite(LED_BUILTIN, 0);
  String datastring = String(cyltempint) + "." + String(cyltempdec) + "#" + String(POWER) + "#" + String(CH) + "#" + String(DHW) + "#" + String(OAT) + "#" + String(FROST) + "#" + String(DEFROST) + "#" + String(LOWTARRIF) + "#" + String(NIGHTMODE) + "#" + String(FAN) + "#" + String(PUMP) + "#" + String(COMPRESSOR) + "#" + String(COMPOVERRUN) + "#" + String(CIRCWATERRETURN) + "#" + String(COMPFREQ) + "#" + String(DISCHARGTEMP) + "#" + String(POWERCONS) + "#" + String(FANSPEED) + "#" + String(DEFRTEMP) + "#" + String(PUMPSPEED) + "#" + String(SUCTIONTEMP) + "#" + String(CIRCWATERFLOW) + "#" + String(ESP.getFreeHeap());
  request->send(200, "text/plain", datastring);
  digitalWrite(LED_BUILTIN, 1);
}

void pausebuttons(AsyncWebServerRequest *request) {
  digitalWrite(LED_BUILTIN, 0);
  request->send(200, "text/plain", "Buttons paused");
  buttonpause = 1;
  digitalWrite(LED_BUILTIN, 1);
}

void resumebuttons(AsyncWebServerRequest *request) {
  digitalWrite(LED_BUILTIN, 0);
  request->send(200, "text/plain", "Buttons resumed");
  buttonpause = 0;
  digitalWrite(LED_BUILTIN, 1);
}

// Serial stuff
#define SERIAL_BAUD 1200
#define BYTE_PERIOD 18 //18ms between the start of each byte in a word
byte messagereceived[30] = {0};
unsigned long nextbyteexpectedby = 0;
uint8_t nextbytetoreceive = 0;
int8_t messagereceivelengthexpected = 0;

// button stuff 
#define UP_BUTTON 16
#define DOWN_BUTTON 5
#define BACK_BUTTON 14
#define TICK_BUTTON 4
#define RELAY_U8 12
#define RELAY_U7 13
unsigned long buttondowntime = 0;
uint8_t buttondown = 0;
const unsigned long buttonpress = 75; //ms to press a button
const unsigned long buttonhold = 3500; //ms to hold a button
uint8_t tx_go = 0; //disable tx at start
uint8_t onreqscreen = 0;
int16_t lastrequestsent = 100;
int16_t lastrequestreceived = 0;
unsigned long requestsent = 0;
const unsigned long requesttimeout = 20000; //20secs should give enough time to manually press buttons for recovery
uint8_t responsereceived = 0;
int8_t requestincrement = 1;
unsigned long buttonwait = 15; //seconds before we start pressing buttons

void resetreceived() {
  yield();
  nextbytetoreceive = 0; //discard message so far
  memset(messagereceived, 0, sizeof(messagereceived)); //clear variable
  messagereceivelengthexpected = 0;
  nextbyteexpectedby = 0;
}

void procmessagereceived() {
  yield();
  if (messagereceived[0] >= 5 && messagereceived[0] <=29) {
    byte checksum = 0;
    String message = "1: 0x";
    for ( uint8_t i = 0; i < messagereceived[0]; i++) {
      message += String(messagereceived[i], HEX);
      message += ", ";
      checksum += messagereceived[i];
    }
    message += "end";
    if (checksum == 255) {
      processword();
    } else { //checksum error
      resetreceived();
    }
  } else { //word is too long or too short to be correct
    resetreceived();
  }
}

void processword() { //anything getting this far has already been verified as a word - correct length and checksum
  yield();
  String url;
  switch (messagereceived[3]) { //4th byte is message type
    case 152:
      //0x98 - Day/Time and power/demands back from the ASHP
      POWER = bitRead(messagereceived[8], 0);
      DHW = bitRead(messagereceived[9], 0);
      CH = bitRead(messagereceived[8], 2);
      url = "http://emonpi/input/post?node=heatpump&fulljson={%22hppowerstat%22:" + String(POWER) + ",%22dhw%22:" + String(DHW) + ",%22CH%22:" + String(CH) + "}&apikey=" + EMONAPI;
      break;
    case 153:
      //0x99 - Request from the remote to the ASHP
      lastrequestsent = messagereceived[5];
      break;
    case 154:
      //0x9A - From the ASHP, contains various data
      lastrequestreceived = messagereceived[5];
      responsereceived = 1;
      switch (messagereceived[5]) {
        case 100:
          CIRCWATERRETURN = messagereceived[7];
          url = "http://emonpi/input/post?node=heatpump&fulljson={%22circwaterreturn%22:" + String(CIRCWATERRETURN) + "}&apikey=" + EMONAPI;
          break;
        case 101:
          COMPFREQ = messagereceived[7];
          url = "http://emonpi/input/post?node=heatpump&fulljson={%22compfreq%22:" + String(COMPFREQ) + "}&apikey=" + EMONAPI;
          break;
        case 102:
          DISCHARGTEMP = messagereceived[7];
          url = "http://emonpi/input/post?node=heatpump&fulljson={%22refrdischarge%22:" + String(DISCHARGTEMP) + "}&apikey=" + EMONAPI;
          break;
        case 103:
          POWERCONS = messagereceived[7];
          url = "http://emonpi/input/post?node=heatpump&fulljson={%22cpowercons%22:" + String(POWERCONS) + "}&apikey=" + EMONAPI;
          break;
        case 104:
          FANSPEED = messagereceived[7];
          url = "http://emonpi/input/post?node=heatpump&fulljson={%22fanspeed%22:" + String(FANSPEED) + "}&apikey=" + EMONAPI;
          break;
        case 105:
          DEFRTEMP = messagereceived[7];
          url = "http://emonpi/input/post?node=heatpump&fulljson={%22defrtemp%22:" + String(DEFRTEMP) + "}&apikey=" + EMONAPI;
          break;
//        case 106:
//          OATREQ = wordreceived[7]; //ignore, we don't use this
//          break;
        case 107:
          PUMPSPEED = messagereceived[7];
          url = "http://emonpi/input/post?node=heatpump&fulljson={%22pumpspeed%22:" + String(PUMPSPEED) + "}&apikey=" + EMONAPI;
          break;
        case 108:
          SUCTIONTEMP = messagereceived[7];
          url = "http://emonpi/input/post?node=heatpump&fulljson={%22refrsuction%22:" + String(SUCTIONTEMP) + "}&apikey=" + EMONAPI;
          break;
        case 109:
          CIRCWATERFLOW = messagereceived[7];
          url = "http://emonpi/input/post?node=heatpump&fulljson={%22circwaterflow%22:" + String(CIRCWATERFLOW) + "}&apikey=" + EMONAPI;
          break;
      }
      break;
    case 156:
      //0x9C - From the ASHP, contains the outside air temp and pump, fan and compressor status
      OAT = messagereceived[6];
      PUMP = bitRead(messagereceived[8], 5);
      FAN = bitRead(messagereceived[8], 4);
      COMPRESSOR = bitRead(messagereceived[8], 6); //FAN and COMP may need swapping?
      COMPOVERRUN = bitRead(messagereceived[8], 7);
      url = "http://emonpi/input/post?node=heatpump&fulljson={%22oat%22:" + String(OAT) + ",%22pump%22:" + String(PUMP) + ",%22fan%22:" + String(FAN) + ",%22compressor%22:" + String(COMPRESSOR) + ",%22overrun%22:" + String(COMPOVERRUN) + "}&apikey=" + EMONAPI;
      break;
//    case 8: //0x08 - 'OK' status from the ASHP?
//    case 9: //0x09 - Ack from the remote?
//    case 151: //0x97 - Day/Time and power from the remote
//    case 161: //0xA1 - Unknown, but from the remote
//    case 163: //0xA3 - Also unknown from the remote, but the 5th byte appears to increment periodically
    default:
      break;
  }
  http.begin(url.c_str());
  http.GET();
  http.end();   
  resetreceived();
}

void pressButton(uint8_t pinnum, uint16_t presslength) {
  digitalWrite(LED_BUILTIN, LOW);
  if (buttonpause == 0) { //stop pressing stuff if we're paused
    digitalWrite(pinnum, HIGH);
    for (uint8_t i = 0; i < presslength / 75; i++) {
      yield();
      delay(75);
    }
    digitalWrite(pinnum, LOW);
    digitalWrite(LED_BUILTIN, HIGH);
    yield();
    delay(75);
  }
}

////// Setup

void setup() {

  // Initialize the Serial (for heatpump comms)
  Serial.begin(SERIAL_BAUD); //comms with ashp
  //GPIO 1 (TX) swap the pin to a GPIO.
  pinMode(1, FUNCTION_3);

  //init pins
  pinMode(UP_BUTTON, OUTPUT);
  pinMode(DOWN_BUTTON, OUTPUT);
  pinMode(BACK_BUTTON, OUTPUT);
  pinMode(TICK_BUTTON, OUTPUT);
  digitalWrite(UP_BUTTON, LOW);
  digitalWrite(DOWN_BUTTON, LOW);
  digitalWrite(BACK_BUTTON, LOW);
  digitalWrite(TICK_BUTTON, LOW);

  // Builtin leds of ESP
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // WiFi connection
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
  digitalWrite(LED_BUILTIN, HIGH);

  // Register host name in WiFi and mDNS
  String hostNameWifi = HOST_NAME;
  hostNameWifi.concat(".local");
  WiFi.hostname(hostNameWifi);

  server.on("/", handleRoot);
  server.on("/pause", pausebuttons);
  server.on("/resume", resumebuttons);
  server.on("/readdata", handledata);
  server.onNotFound(handleRoot);
  server.begin();

  // temp sensor
  digitalWrite(LED_BUILTIN, LOW);
  sensors.begin();
  sensors.requestTemperatures(); //get first temperature reading
  sensors.setWaitForConversion(false);  // makes it async
  digitalWrite(LED_BUILTIN, HIGH);

  // End of setup

  // Check if we've just reset, or if it's a full system boot.
  // It's more likely that the esp has reset, than the whole system has rebooted.
  // We can check this by sending a single request, and seeing if we get anything back.
  // So, send a TICK, wait a bit, and see if we've had a response back.
  // If we have, the esp has just reset, and we can continue with requests. (tx_go = 1)
  // If not, its a system boot, so we wait. (tx_go = 0)
  //
  // Send a tick...
  lastrequestsent = 0;
  responsereceived = 0;
  pressButton(TICK_BUTTON, buttonpress);
  requestsent = millis();
  uint8_t waiting = 1;
  //wait for a response
  while(waiting == 1) {
    if ((millis() - requestsent) >= requesttimeout) { //waiting too long, must be a full system boot
      waiting = 0; //stop waiting
      tx_go = 0; //wait until we start requests
      onreqscreen = 0; //not on request screen
    }
    else if ( responsereceived == 1 && lastrequestsent == lastrequestreceived ) { //we've received a response to a request, and wait until we get what we asked for - slow down a bit!
      waiting = 0; //stop waiting
      tx_go = 1; //just an esp reset, so continue requests
      onreqscreen = 1; //as above
    }
    //handle serial
    //read
    if (Serial.available() >= 1) { //something had come in
      nextbyteexpectedby = millis();
      messagereceived[nextbytetoreceive] = Serial.read();
      yield();
      if (nextbytetoreceive == 0) { //first byte is message length
        messagereceivelengthexpected = messagereceived[0];
        if (messagereceived[0] == 0) {
          resetreceived();
        } else if (messagereceived[0] > 25) {
          resetreceived();
        }
      }
      nextbytetoreceive++;
      if (nextbytetoreceive == messagereceivelengthexpected) { //word complete
        //process message here
        procmessagereceived();
      }
    }
    if (nextbytetoreceive < messagereceivelengthexpected) { //word incomplete
      if (millis() - nextbyteexpectedby > BYTE_PERIOD + 4 ) { //but no more bytes received in time, then word corrupted or incomplete (plus 4ms 'buffer')
        resetreceived();
      }
    }
  }
}

void loop()
{
  //led heartbeat
  if (millis() > blinktime + heartbeat) {
    digitalWrite(LED_BUILTIN, 0);
    blinktime = millis();
  }
  else if (millis() > blinktime + 20) {
    digitalWrite(LED_BUILTIN, 1);
  }

  if (tx_go == 1) { //we can request data
    if (onreqscreen == 0) { //but we're not yet on the request screen
      pressButton(BACK_BUTTON, buttonhold);
      delay(500);
      pressButton(TICK_BUTTON, buttonpress);
      lastrequestsent = 0;
      responsereceived = 0;
      requestsent = millis();
      onreqscreen = 1;
    } else { //we are on the request screen
      if ((millis() - requestsent) >= requesttimeout) { //waiting too long, send another request
        if ( lastrequestsent == 0 ) { //this should not be zero - we've sent a request
//        We might be on the time/day setting screen - hold the tick button to exit.
//        Doing that may take us _into_ the setting screen, but we'll come out the next time around.
          pressButton(TICK_BUTTON, buttonhold);
          onreqscreen = 0; //try again
        } else {
          pressButton(TICK_BUTTON, buttonpress);
          lastrequestsent = 0;
          responsereceived = 0;
          requestsent = millis();
        }
      } else if ( responsereceived == 1 && lastrequestsent == lastrequestreceived ) { //we've received a response to a request, and wait until we get what we asked for - slow down a bit!
        responsereceived = 0;
        if ( lastrequestreceived >= 150 ) { //ie, 50 or above on the screen (including 98, 99 etc if we've gone below 00)
          requestincrement = 1; //go up
        } else if ( lastrequestreceived >= 109 ) { // 09 or over on the screen
          requestincrement = 0; //go down
        } else if ( lastrequestreceived == 100 ) { // 00 on the screen
          requestincrement = 1; //go up
        }
        if ( requestincrement == 1 ) { // go up
          pressButton(UP_BUTTON, buttonpress);
        } else { //go down
          pressButton(DOWN_BUTTON, buttonpress);
        }
        pressButton(TICK_BUTTON, buttonpress);
        lastrequestsent = 0;
        responsereceived = 0;
        requestsent = millis();
      }
    }
  }

  //handle serial
  //read
  if (Serial.available() >= 1) { //something had come in
    nextbyteexpectedby = millis();
    messagereceived[nextbytetoreceive] = Serial.read();
    yield();
    if (nextbytetoreceive == 0) { //first byte is message length
      messagereceivelengthexpected = messagereceived[0];
      if (messagereceived[0] == 0) {
        resetreceived();
      } else if (messagereceived[0] > 25) {
        resetreceived();
      }
    }
    nextbytetoreceive++;
    if (nextbytetoreceive == messagereceivelengthexpected) { //word complete
      //process message here
      procmessagereceived();
    }
  }
  if (nextbytetoreceive < messagereceivelengthexpected) { //word incomplete
    if (millis() - nextbyteexpectedby > BYTE_PERIOD + 4 ) { //but no more bytes received in time, then word corrupted or incomplete (plus 4ms 'buffer')
      resetreceived();
    }
  }

  // Each second
  if ((millis() - mLastTime) >= 1000) {
    // Time
    mLastTime = millis();
    mTimeSeconds++;
    yield();

    if (mTimeSeconds % 5 == 0) { // Each 5 seconds
      // Cylinder temp
      cyltemp = sensors.getTempCByIndex(0);
      sensors.requestTemperatures();
      if (cyltemp < 70.0 && cyltemp >= 0.0) { //don't send erroneous data
        cyltempint = int(cyltemp);
        cyltempdec = int(cyltemp * 10) - 10 * cyltempint;
        String url = "http://emonpi/input/post?node=heatpump&fulljson={%22cyltemp%22:" + String(cyltempint) + "." + String(cyltempdec) + "}&apikey=" + EMONAPI;
        http.begin(url.c_str());
        http.GET();
        http.end();  
      }
    }
    if ((mTimeSeconds > buttonwait) && (tx_go == 0)) {
      tx_go = 1;
    }
  }

  // Give a time for ESP
  yield();

}
/////////// End
