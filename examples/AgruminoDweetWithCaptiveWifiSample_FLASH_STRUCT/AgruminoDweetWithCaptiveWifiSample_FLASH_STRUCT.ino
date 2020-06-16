/*
  AgruminoCaptiveWiSample.ino - Sample project for Agrumino board using the Agrumino library.
  Created by giuseppe.broccia@lifely.cc on October 2017.

  @see Agrumino.h for the documentation of the lib
*/
#include "Agrumino.h"           // Our super cool lib ;)
#include <ESP8266WiFi.h>        // https://github.com/esp8266/Arduino
#include <DNSServer.h>          // Installed from ESP8266 board
#include <ESP8266WebServer.h>   // Installed from ESP8266 board
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>        // https://github.com/bblanchon/ArduinoJson

// Time to sleep in second between the readings/data sending
#define SLEEP_TIME_SEC 20 // shoud be 3600 (1h)

#define N_SAMPLES 4u  // samples number before transmission

#define SECTOR_SIZE 4096u

// Web Server data, in our sample we use Dweet.io.
const char* WEB_SERVER_HOST = "dweet.io";
const String WEB_SERVER_API_SEND_DATA = "/dweet/quietly/for/"; // The Dweet name is created in the loop() method.

// Our super cool lib
Agrumino agrumino;


// Used for sending Json POST requests
StaticJsonBuffer<200 * N_SAMPLES> jsonBuffer;
// Used to create TCP connections and make Http calls
WiFiClient client;




typedef struct
{
  float temp;
  uint16_t soil;
  float lux;
  float batt;
  uint16_t battLevel;
  boolean usb;
  boolean charge;
}SensorData_t;

typedef struct
{
  SensorData_t  vector[N_SAMPLES];
}SensorDataVector_t;

typedef struct
{
  uint16_t index;
  SensorDataVector_t data;
}Fields_t;

typedef union flashMemory
{
  Fields_t Fields;
  uint8_t Bytes[sizeof(Fields_t)];
}flashMemory_t;

flashMemory_t *PtrFlashMemory = NULL;

uint32_t crc32b = 0;
uint16_t tmp = 0;
uint8_t crc8b = 0;

void setup() {

  Serial.begin(115200);

  // Setup our super cool lib
  agrumino.setup();

  // Turn on the board to allow the usage of the Led
  agrumino.turnBoardOn();

  // WiFiManager Logic taken from
  // https://github.com/kentaylor/WiFiManager/blob/master/examples/ConfigOnSwitch/ConfigOnSwitch.ino

  // With batteryCheck true will return true only if the Agrumino is attached to USB with a valid power
  boolean resetWifi = checkIfResetWiFiSettings(true);
  boolean hasWifiCredentials = WiFi.SSID().length() > 0;

  if (resetWifi || !hasWifiCredentials) {
    // Show Configuration portal

    // Blink and keep ON led as a feedback :)
    blinkLed(100, 5);
    agrumino.turnLedOn();

    WiFiManager wifiManager;

    // Customize the web configuration web page
    wifiManager.setCustomHeadElement("<h1>Agrumino</h1>");

    // Sets timeout in seconds until configuration portal gets turned off.
    // If not specified device will remain in configuration mode until
    // switched off via webserver or device is restarted.
    // wifiManager.setConfigPortalTimeout(600);

    // Starts an access point and goes into a blocking loop awaiting configuration
    String ssidAP = "Agrumino-AP-" + getChipId();
    boolean gotConnection = wifiManager.startConfigPortal(ssidAP.c_str());

    Serial.print("\nGot connection from Config Portal: ");
    Serial.println(gotConnection);

    agrumino.turnLedOff();

    // ESP.reset() doesn't work on first reboot
    agrumino.deepSleepSec(1);
    return;

  } else {
    // Try to connect to saved network
    // Force to station mode because if device was switched off while in access point mode it will start up next time in access point mode.
    agrumino.turnLedOn();
    WiFi.mode(WIFI_STA);
    WiFi.waitForConnectResult();
  }

  boolean connected = (WiFi.status() == WL_CONNECTED);

  if (connected) {
    // If you get here you have connected to the WiFi :D
    Serial.print("\nConnected to WiFi as ");
    Serial.print(WiFi.localIP());
    Serial.println(" ...yeey :)\n");
  } else {
    // No WiFi connection. Skip a cycle and retry later
    Serial.print("\nNot connected!\n");
    // ESP.reset() doesn't work on first reboot
    agrumino.deepSleepSec(SLEEP_TIME_SEC);
  }

  // Initialize EEPROM and pointer
  Serial.println("Data struct to EEPROM test program");
  EEPROM.begin(SECTOR_SIZE);
  
  PtrFlashMemory = (flashMemory_t *)EEPROM.getDataPtr(); // Assigning pointer address to flash memory block dumped in RAM
  Serial.println("Memory assignement successful!");
  
  // Calculate checksum and validate memory area
  crc32b = calculateCRC32(PtrFlashMemory->Bytes, sizeof(Fields_t));
  crc8b = EEPROM.read(SECTOR_SIZE-1); // Read crc at the end of sector
  Serial.println("Calculated CRC32=" + String(crc32b&0xff) + " readed=" + String(crc8b));
  
  if(((uint8_t)crc32b&0xff) == crc8b) 
    Serial.println("CRC32 pass!");
  else
  {
    Serial.println("CRC32 fail! I clean memory!");
    cleanMemory();
  }
  
}

void loop() {
  Serial.println("#########################\n");

  agrumino.turnBoardOn();
  agrumino.turnLedOn();

  // index is the last memorized struct of sensor data
  tmp = PtrFlashMemory->Fields.index;
  if(tmp > N_SAMPLES-1) { // Circular buffer behaviour
    tmp = 0;
    PtrFlashMemory->Fields.index = tmp;
  }


    Serial.println("*****   CURRENT INDEX IS:  " + String(tmp));

    PtrFlashMemory->Fields.data.vector[tmp].temp = agrumino.readTempC();
    PtrFlashMemory->Fields.data.vector[tmp].soil = agrumino.readSoil();
    PtrFlashMemory->Fields.data.vector[tmp].lux = agrumino.readLux();
    PtrFlashMemory->Fields.data.vector[tmp].batt = agrumino.readBatteryVoltage();
    PtrFlashMemory->Fields.data.vector[tmp].battLevel = agrumino.readBatteryLevel();
    PtrFlashMemory->Fields.data.vector[tmp].usb = agrumino.isAttachedToUSB();
    PtrFlashMemory->Fields.data.vector[tmp].charge = agrumino.isBatteryCharging();

    PtrFlashMemory->Fields.index++; // Increment index

    Serial.println("temperature:       " + String(PtrFlashMemory->Fields.data.vector[tmp].temp) + "°C");
    Serial.println("soilMoisture:      " + String(PtrFlashMemory->Fields.data.vector[tmp].soil) + "%");
    Serial.println("illuminance :      " + String(PtrFlashMemory->Fields.data.vector[tmp].lux) + " lux");
    Serial.println("batteryVoltage :   " + String(PtrFlashMemory->Fields.data.vector[tmp].batt) + " V");
    Serial.println("batteryLevel :     " + String(PtrFlashMemory->Fields.data.vector[tmp].battLevel) + "%");
    Serial.println("isAttachedToUSB:   " + String(PtrFlashMemory->Fields.data.vector[tmp].usb));
    Serial.println("isBatteryCharging: " + String(PtrFlashMemory->Fields.data.vector[tmp].charge));
    Serial.println(); 

  
  // Calculate checksum
  crc32b = calculateCRC32(PtrFlashMemory->Bytes, sizeof(Fields_t));
  crc8b = (uint8_t)crc32b&0xff;
  Serial.println("Setting CRC32=" + String(crc8b));
  EEPROM.write(SECTOR_SIZE-1, crc8b); // Put crc at the end of sector
  
  // Commit sensor data at current index
  if (EEPROM.commit()) {
    Serial.println("EEPROM successfully committed");
  } else {
    Serial.println("ERROR! EEPROM commit failed");
  }  

  
  if(tmp == N_SAMPLES - 1) {
      // Change this if you whant to change your thing name
      // We use the chip Id to avoid name clashing
      String dweetThingName = "Agrumino-" + getChipId();
      
      Serial.println("Now I'm sending " + String(N_SAMPLES) + " json(s) to Dweet");
      for (uint8_t i = 0; i < N_SAMPLES; i++) {
        // Send data to our web service
        sendData(dweetThingName, PtrFlashMemory->Fields.data.vector[i].temp, PtrFlashMemory->Fields.data.vector[i].soil, PtrFlashMemory->Fields.data.vector[i].lux, PtrFlashMemory->Fields.data.vector[i].batt, PtrFlashMemory->Fields.data.vector[i].battLevel, PtrFlashMemory->Fields.data.vector[i].usb, PtrFlashMemory->Fields.data.vector[i].charge);
        delay(5000);
      }  
      // I sent all data so I can clean my memory
      cleanMemory();
  }



  
  // Blink when the business is done for giving an Ack to the user
  blinkLed(200, 2);

  // Board off before delay/sleep to save battery :)
  agrumino.turnBoardOff();

  // delaySec(SLEEP_TIME_SEC); // The ESP8266 stays powered, executes the loop repeatedly
  agrumino.deepSleepSec(SLEEP_TIME_SEC); // ESP8266 enter in deepSleep and after the selected time starts back from setup() and then loop()
}

//////////////////
// HTTP methods //
//////////////////

void sendData(String dweetName, float temp, int soil, float lux, float batt, unsigned int battLevel, boolean usb, boolean charge) {

  String bodyJsonString = getSendDataBodyJsonString(temp,  soil,  lux,  batt, battLevel, usb, charge );

  // Use WiFiClient class to create TCP connections, we try until the connection is estabilished
  while (!client.connect(WEB_SERVER_HOST, 80)) {
    Serial.println("connection failed\n");
    delay(1000);
  }
  Serial.println("connected to " + String(WEB_SERVER_HOST) + " ...yeey :)\n");

  Serial.println("###################################");
  Serial.println("### Your Dweet.io thing name is ###");
  Serial.println("###   --> " + dweetName + " <--  ###");
  Serial.println("###################################\n");

  // Print the HTTP POST API data for debug
  Serial.println("Requesting POST: " + String(WEB_SERVER_HOST) + WEB_SERVER_API_SEND_DATA + dweetName);
  Serial.println("Requesting POST: " + bodyJsonString);

  // This will send the request to the server
  client.println("POST " + WEB_SERVER_API_SEND_DATA + dweetName + " HTTP/1.1");
  client.println("Host: " + String(WEB_SERVER_HOST) + ":80");
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(bodyJsonString.length()));
  client.println();
  client.println(bodyJsonString);

  delay(10);

  int timeout = 300; // 100 ms per loop so 30 sec.
  while (!client.available()) {
    // Waiting for server response
    delay(100);
    Serial.print(".");
    timeout--;
    if (timeout <= 0) {
      Serial.print("Err. client.available() timeout reached!");
      return;
    }
  }

  String response = "";

  while (client.available() > 0) {
    char c = client.read();
    response = response + c;
  }

  // Remove bad chars from response
  response.trim();
  response.replace("/n", "");
  response.replace("/r", "");

  Serial.println("\nAPI Update successful! Response: \n");
  Serial.println(response);
}

// Returns the Json body that will be sent to the send data HTTP POST API
String getSendDataBodyJsonString(float temp, int soil, float lux, float batt, unsigned int battLevel, boolean usb, boolean charge) {
  JsonObject& jsonPost = jsonBuffer.createObject();
  jsonPost["temp"] = String(temp);
  jsonPost["soil"] = String(soil);
  jsonPost["lux"]  = String(lux);
  jsonPost["battVolt"] = String(batt);
  jsonPost["battLevel"] = String(battLevel);
  jsonPost["battCharging"] = String(charge);
  jsonPost["usbConnected"]  = String(usb);

  String jsonPostString;
  jsonPost.printTo(jsonPostString);
  
  return jsonPostString;
}


/////////////////////
// Utility methods //
/////////////////////

void blinkLed(int duration, int blinks) {
  for (int i = 0; i < blinks; i++) {
    agrumino.turnLedOn();
    delay(duration);
    agrumino.turnLedOff();
    if (i < blinks) {
      delay(duration); // Avoid delay in the latest loop ;)
    }
  }
}

void delaySec(int sec) {
  delay (sec * 1000);
}

const String getChipId() {
  // Returns the ESP Chip ID, Typical 7 digits
  return String(ESP.getChipId());
}

// If the Agrumino S1 button is pressed for 5 secs then reset the wifi saved settings.
// If "checkBattery" is true the method return true only if the USB is connected.
boolean checkIfResetWiFiSettings(boolean checkBattery) {
  int delayMs = 100;
  int remainingsLoops = (5 * 1000) / delayMs;
  Serial.print("\nCheck if reset WiFi settings: ");
  while (remainingsLoops > 0
         && agrumino.isButtonPressed()
         && (agrumino.isAttachedToUSB() || !checkBattery) // The Agrumino must be attached to USB
        ) {
    // Blink the led every sec as confirmation
    if (remainingsLoops % 10 == 0) {
      agrumino.turnLedOn();
    }
    delay(delayMs);
    agrumino.turnLedOff();
    remainingsLoops--;
    Serial.print(".");
  }
  agrumino.turnLedOff();
  boolean success = (remainingsLoops == 0);
  Serial.println(success ? " YES!" : " NO");
  Serial.println();
  return success;
}



uint32_t calculateCRC32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xffffffff;
  int i;
  Serial.println("length=" + String(length));
  for(i=0;i<length;i++)
  {
    uint8_t c = data[i];
    for (uint32_t i = 0x80; i > 0; i >>= 1) {
      bool bit = crc & 0x80000000;
      if (c & i) {
        bit = !bit;
      }
      crc <<= 1;
      if (bit) {
        crc ^= 0x04c11db7;
      }
    }
  }
  return crc;
}

void cleanMemory() {
  Serial.println("RAM memory manual initialization");
  PtrFlashMemory->Fields.index = 0;
  for (uint8_t i = 0; i < N_SAMPLES; i++) {
    PtrFlashMemory->Fields.data.vector[i].temp = 0.0f;
    PtrFlashMemory->Fields.data.vector[i].soil = 0u;
    PtrFlashMemory->Fields.data.vector[i].lux = 0.0f;
    PtrFlashMemory->Fields.data.vector[i].batt = 0.0f;
    PtrFlashMemory->Fields.data.vector[i].battLevel = 0u;
    PtrFlashMemory->Fields.data.vector[i].usb = false;
    PtrFlashMemory->Fields.data.vector[i].charge = false;
  }
}
