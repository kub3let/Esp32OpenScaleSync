#include <Arduino.h>
#include <esp_sleep.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <Wifi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <StreamUtils.h>
#include <NTPClient.h>
#include <TimeLib.h>


// settings
const int TIME_TO_SLEEP = 8 * 60 * 60;

const IPAddress NTP_SERVER(192, 168, 1, 1);
const char* WIFI_SSID            = "wifi_ssid";
const char* WIFI_PASSWORD        = "wifi_password";

const char* MQTT_SERVER_IP       = "192.168.1.24";
const int   MQTT_SERVER_PORT     = 1883;

const char* MQTT_USER            = "mqtt";
const char* MQTT_PASSWORD        = "password";
const char* MQTT_TOPIC           = "openscalesync/measurement";

BLEAddress scaleBluetoothAddress("xx:xx:xx:xx:xx:xx");
BLEUUID scaleBluetoothService("0000ffe0-0000-1000-8000-00805f9b34fb");
BLEUUID scaleBluetoothCharacteristic("0000ffe1-0000-1000-8000-00805f9b34fb");
// settings end



#define uS_TO_S_FACTOR 1000000ULL
#define MQTT_VERSION MQTT_VERSION_3_1_1

WiFiUDP ntpUDP;
NTPClient ntpClient(ntpUDP, NTP_SERVER);

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

BLEScan* pBLEScan;
BLEClient* bleClient;
BLERemoteCharacteristic* pRemoteCharacteristic;
std::vector<uint8_t> receivedData;

char userName[4] = {0x0, 0x0, 0x0, 0x0};
uint8_t userId[8] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
uint8_t scaleBattery = 100;
uint8_t expectedChunks = 0;
uint8_t currentChunk = 1;

String jsonPayload;
const int jsonBufferSize = 1536;
bool scaleInitialized = false;
bool waitingForResponse = false;
bool measurementsAlreadyRequested = false;
bool measurementsDataComplete = false;
uint8_t needToSendChunkAck = 0x0;
const uint8_t START_BYTE = 0xe7;
const uint8_t START_BYTE_ALT = 0xe6;
const uint8_t META_INFO = 0xf0;
const uint8_t META_ACK = 0xf1;
const uint8_t INIT_COMMAND = 0x1;
const uint8_t SCALE_INFO_COMMAND = 0x4f;
const uint8_t USER_LIST_COMMAND = 0x33;
const uint8_t USER_LIST_PAYLOAD = 0x34;
const uint8_t USER_INFO_COMMAND = 0x36;
const uint8_t MEASUREMENT_COMMAND = 0x41;
const uint8_t MEASUREMENT_PAYLOAD = 0x42;
const uint8_t DELETE_MEASUREMENT_COMMAND = 0x43;
const uint8_t SET_TIMESTAMP_COMMAND = 0xe9;

bool connectToWifi() {
  unsigned long startTime = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to wifi... ");
    // connect to wifi
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
      // 15 sec timeout
      if (millis() - startTime > 15 * 1000) {
        Serial.println("\nError timeout reached");
        break;
      }

      delay(200);
    }
  }

  if (WiFi.status() == WL_CONNECTED && !ntpClient.isTimeSet()) {
    Serial.println("connected !");

    ntpClient.begin();
    ntpClient.update();
  
    Serial.println("Synced rtc via ntp, time: " + ntpClient.getFormattedTime());
    return true;
  }

  return false;
}

bool connectToMqtt() {
  unsigned long startTime = millis();

  if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
    String macaddress = WiFi.macAddress();
    macaddress.replace(":", "");
    macaddress.toLowerCase();
    
    Serial.print("Connecting to mqtt... ");
    delay(500);

    mqttClient.disconnect();
    mqttClient.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
    mqttClient.connect(macaddress.c_str(), MQTT_USER, MQTT_PASSWORD);

    while (!mqttClient.connected()) {
      // 30 sec timeout
      if (millis() - startTime > 30 * 1000) {
        Serial.println("\nError timeout reached");
        break;
      }

      delay(200);
    }
  }

  if (mqttClient.connected()) {
    Serial.println("connected !");
    mqttClient.setBufferSize(jsonBufferSize);
    return true;
  }
}

void publishMeasurementToMqtt() {
  connectToMqtt();

  if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
    Serial.println("Sending json payload via mqtt to " + String(MQTT_TOPIC));
    Serial.println(jsonPayload);

    if (!mqttClient.publish(MQTT_TOPIC, jsonPayload.c_str(), true)) {
      Serial.println("Error sending mqtt payload failed");
    }
  } else {
    Serial.println("Error wifi/mqtt not connected");
  }
}

void printHexDump(const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

void parseMeasurement() {
  size_t chunkSize = 22;

  Serial.println("Preparing mqtt payload...");

  StaticJsonDocument<jsonBufferSize> json;
  JsonArray measurements = json.createNestedArray("measurements");
  JsonObject metadata = json.createNestedObject("metadata");
  metadata["user"] = userName;
  metadata["battery"] = scaleBattery;

  for (size_t i = 0; i < receivedData.size(); i += chunkSize) {
    if (i + chunkSize > receivedData.size()) {
      Serial.println("Not enough data to complete the measurement");
      break;
    }

    size_t startIdx = i;

    int t = (receivedData[startIdx] << 24) | (receivedData[startIdx + 1] << 16) | (receivedData[startIdx + 2] << 8) | receivedData[startIdx + 3];
    float weight = ((receivedData[startIdx + 4] << 8) | receivedData[startIdx + 5]) * 0.05; // in g / 50
    int impedance = (receivedData[startIdx + 6] << 8) | receivedData[startIdx + 7];
    float fat = ((receivedData[startIdx + 8] << 8) | receivedData[startIdx + 9]) / 10.0;
    float water = ((receivedData[startIdx + 10] << 8) | receivedData[startIdx + 11]) / 10.0;
    float muscle = ((receivedData[startIdx + 12] << 8) | receivedData[startIdx + 13]) / 10.0;
    float bone = ((receivedData[startIdx + 14] << 8) | receivedData[startIdx + 15]) / 10.0;
    int bmr = (receivedData[startIdx + 16] << 8) | receivedData[startIdx + 17];
    int amr = (receivedData[startIdx + 18] << 8) | receivedData[startIdx + 19];
    float bmi = ((receivedData[startIdx + 20] << 8) | receivedData[startIdx + 21]) / 10.0;

    char dateTime[20];
    snprintf(dateTime, sizeof(dateTime), "%04d-%02d-%02d %02d:%02d:%02d", 
          year(t), month(t), day(t), hour(t), minute(t), second(t));

    // Serial.printf("Measurement %zu:\n", i / chunkSize + 1);
    // Serial.printf("Date: %s (UTC), Weight: %.2f kg, Impedance: %d\n", dateTime, weight, impedance);
    // Serial.printf("Fat: %.1f%%, Water: %.1f%%, Muscle: %.1f%%\n", fat, water, muscle);
    // Serial.printf("Bone: %.1fg, bmr: %d, amr: %d, bmi: %.1f\n", bone, bmr, amr, bmi);
    // Serial.println();

    JsonObject measurement = measurements.createNestedObject();
    measurement["timestamp"] = t;
    measurement["dateTime"] = dateTime;
    measurement["weight"] = weight;
    measurement["impedance"] = impedance;
    measurement["fat"] = fat;
    measurement["water"] = water;
    measurement["muscle"] = muscle;
    measurement["bone"] = bone;
    measurement["bmr"] = bmr;
    measurement["amr"] = amr;
    measurement["bmi"] = bmi;
  }

  serializeJson(json, jsonPayload);
}

void bleSendRaw(uint8_t* buffer, int length) {
  Serial.print("Sending payload: "); printHexDump(buffer, length);
  pRemoteCharacteristic->writeValue(buffer, length);
}

void acknowledgeChunk() {
    int bufferLength = 5;
    uint8_t ack[bufferLength] = {START_BYTE, META_ACK, needToSendChunkAck, expectedChunks, currentChunk};
    Serial.printf("Sending ack for chunk %d/%d\n", currentChunk, expectedChunks);
    needToSendChunkAck = 0x0;
    bleSendRaw(ack, bufferLength);
}

bool waitForResponse(int timeout = 5) {
  unsigned long startTime = millis();
  while (waitingForResponse) {
    if (millis() - startTime > timeout * 1000) {
      Serial.println("Error request timed out");
      break;
    }
    delay(5);
  }
  
  if (needToSendChunkAck) { 
    acknowledgeChunk();
    return false;
  }

  return true;
}

void initializeScale() {
  int bufferLength = 2;
  uint8_t req[bufferLength] = {START_BYTE_ALT, INIT_COMMAND};

  receivedData.clear();
  waitingForResponse = true;
  Serial.println("Scale Initialization");
  bleSendRaw(req, bufferLength);
  waitForResponse();
}

void updateScaleTime() {
  unsigned long timestamp = ntpClient.getEpochTime();

  int bufferLength = 5;
  uint8_t req[bufferLength];
  req[0] = SET_TIMESTAMP_COMMAND;
  req[1] = (timestamp >> 24) & 0xFF;
  req[2] = (timestamp >> 16) & 0xFF;
  req[3] = (timestamp >> 8) & 0xFF;
  req[4] = timestamp & 0xFF;

  waitingForResponse = false;
  Serial.println("Updating scale time");
  bleSendRaw(req, bufferLength);
  delay(300);
}

void requestScaleInfo() {
  int bufferLength = 10;
  uint8_t req[bufferLength] = {
    START_BYTE, SCALE_INFO_COMMAND,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0
  };

  receivedData.clear();
  waitingForResponse = true;
  Serial.println("Requesting scale info");
  bleSendRaw(req, bufferLength);
  waitForResponse();
}

void requestUserList() {
  int bufferLength = 2;
  uint8_t req[bufferLength] = {START_BYTE, USER_LIST_COMMAND};

  receivedData.clear();
  waitingForResponse = true;
  Serial.println("Requesting user list");
  bleSendRaw(req, bufferLength);
  waitForResponse();
}

void requestUserInfo() {
  int bufferLength = 10;
  uint8_t req[bufferLength] = {START_BYTE, USER_INFO_COMMAND};
  memcpy(&req[2], userId, 8);

  receivedData.clear();
  waitingForResponse = true;
  Serial.println("Requesting user info");
  bleSendRaw(req, bufferLength);
  waitForResponse();
}

void deleteSavedMeasurements() {
  int bufferLength = 10;
  uint8_t req[bufferLength] = {START_BYTE, DELETE_MEASUREMENT_COMMAND};
  memcpy(&req[2], userId, 8);

  receivedData.clear();
  waitingForResponse = true;
  Serial.println("Deleting saved measurements on scale");
  bleSendRaw(req, bufferLength);
  waitForResponse();
}

bool requestSavedMeasurements() {
  waitingForResponse = true;

  if (!measurementsAlreadyRequested) {
    int bufferLength = 10;
    uint8_t req[bufferLength] = {START_BYTE, MEASUREMENT_COMMAND};
    memcpy(&req[2], userId, 8);

    receivedData.clear();
    Serial.println("Requesting saved measurements");
    bleSendRaw(req, bufferLength);

    measurementsAlreadyRequested = true;
  }

  return waitForResponse();
}

// NOTE: DO NOT SEND TO BLE DEVICE WITHIN THIS CALLBACK PROGRAM STOPS WORKING
void bleNotifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  if (length < 1) return;

  Serial.print("Payload received: "); printHexDump(pData, length);

  uint8_t cmd = pData[1];
  uint8_t cmd2 = pData[2];

  if (pData[0] == START_BYTE_ALT && pData[1] == 0x00 && pData[2] == 0x20) {
    // scale init
    Serial.println("Received Init response");
    scaleInitialized = true;
    waitingForResponse = false;
  }

  if (pData[0] == START_BYTE && pData[1] == 0xf0 && pData[2] == 0x4f) {
    // scale info
    // <sb> f0 4f <??> <battery> <wthr> <fthr> <unit> <ue> <urwe> <ume> <version>
    scaleBattery = pData[4] & 0xFF;
    Serial.printf("Received Scale info, battery: %d%%\n", scaleBattery);
    waitingForResponse = false;
  }

  if (cmd == META_INFO && cmd2 == USER_LIST_COMMAND) {
    // user list count
    // <sb> f0 33 00 <count> <max>
    Serial.println("Received user list count");
    expectedChunks = pData[4] & 0xFF;
    currentChunk = 0;
  } else if (cmd == USER_LIST_PAYLOAD) {
    // user list data
    // <sb> 34 <count> <current> <uid> <name> <year>
    Serial.println("Received user list data");
    currentChunk = pData[3] & 0xFF;

    for (int i = 0; i < 8; i++) {
      userId[i] = pData[i + 4];
    }

    userName[0] = pData[12];
    userName[1] = pData[13];
    userName[2] = pData[14];
    userName[3] = 0x0; // Null-terminate the string
    Serial.printf("Got username: %s\n", userName);

    if (currentChunk >= expectedChunks) {
      Serial.println("All chunks received");
      needToSendChunkAck = USER_LIST_PAYLOAD;
      waitingForResponse = false;
    }
  }

  if (cmd == META_INFO && cmd2 == USER_INFO_COMMAND) {
    // user info data
    // <sb> f0 36 00 <name> <year> <month> <day> <height> <sex|activity>
    Serial.println("Received user info data");
    waitingForResponse = false;
  }

  if (cmd == META_INFO && cmd2 == MEASUREMENT_COMMAND) {
    // total measurement count
    // <sb> f0 41 <count> 00
    Serial.println("Received measurement count");
    expectedChunks = pData[3] & 0xFF;
    currentChunk = 0;

    if (expectedChunks == 0) {
      Serial.println("No measurements available");
      measurementsDataComplete = true;
      waitingForResponse = false;
    }
  } else if (cmd == MEASUREMENT_PAYLOAD) {
    // measurement data
    // <sb> 42 <count> <current> <11 bytes data>
    Serial.println("Received measurement data");
    currentChunk = pData[3] & 0xFF;

    receivedData.insert(receivedData.end(), pData + 4, pData + length);

    // request next measurement
    if (currentChunk >= expectedChunks) {
      Serial.println("All chunks received");
      measurementsDataComplete = true;
    }

    needToSendChunkAck = MEASUREMENT_PAYLOAD;
    waitingForResponse = false;
  } else if (cmd == META_INFO && cmd2 == DELETE_MEASUREMENT_COMMAND) {
    Serial.println("Received measurements deleted");
    waitingForResponse = false;
  }
}

bool connectToScale() {
  Serial.print("Connecting to BLE device... ");
  bleClient = BLEDevice::createClient();

  if (!bleClient->connect(scaleBluetoothAddress)) {
    Serial.println("Error failed to connect");
    return false;
  }

  Serial.println("connected !");

  // Serial.println("Discovering services");
  // std::map<std::string, BLERemoteService*>* services = bleClient->getServices();
  // if (services->empty()) {
  //   Serial.println("No services found!");
  //   return false;
  // }

  // for (auto const& servicePair : *services) {
  //   BLERemoteService* service = servicePair.second;
  //   Serial.print("Service UUID: ");
  //   Serial.println(service->getUUID().toString().c_str());
  //   std::map<std::string, BLERemoteCharacteristic*>* chars = service->getCharacteristics();
  //   for (auto const& charPair : *chars) {
  //     BLERemoteCharacteristic* characteristic = charPair.second;
  //     Serial.print("  Characteristic UUID: ");
  //     Serial.println(characteristic->getUUID().toString().c_str());
  //   }
  // }

  Serial.println("Registering with service");
  BLERemoteService* service = bleClient->getService(scaleBluetoothService);
  pRemoteCharacteristic = service->getCharacteristic(scaleBluetoothCharacteristic);

  if (pRemoteCharacteristic == nullptr) {
    Serial.println("Characteristic 0xffe1 not found!");
    return false;
  }

  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->registerForNotify(bleNotifyCallback);
    Serial.println("Service registered");

    return true;
  }

  return false;
}


// class BluetoothScanCallback: public BLEAdvertisedDeviceCallbacks {
//     void onResult(BLEAdvertisedDevice advertisedDevice) {
//       Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
//       // Advertised Device: Name: SBF75, Address: 34:15:13:1f:eb:2f, serviceUUID: 0000ffe0-0000-1000-8000-00805f9b34fb, serviceUUID: 0000a500-0000-1000-8000-00805f9b34fb, txPower: 4, rssi: -60
//     }
// };

// void bluetoothScan() {
//   pBLEScan = BLEDevice::getScan();
//   pBLEScan->setAdvertisedDeviceCallbacks(new BluetoothScanCallback());
//   pBLEScan->setActiveScan(true);
//   pBLEScan->setInterval(100);
//   pBLEScan->setWindow(99);
//   Serial.println("Scanning BLE");
//   BLEScanResults foundDevices = pBLEScan->start(5, false);
//   pBLEScan->clearResults(); // free memory
//   Serial.println("Scan done");
// }

void setup() {
  Serial.begin(115200);
  Serial.println("");
  Serial.println("OpenScaleSync Booting...");
  BLEDevice::init("OpenScaleSync");
  // bluetoothScan();

  if (connectToWifi() && connectToScale()) {
    initializeScale();

    if (scaleInitialized) {
      updateScaleTime();
      requestScaleInfo();
      requestUserList();
      //requestUserInfo();

      while (!measurementsDataComplete && !requestSavedMeasurements()) {
        delay(50);
      }

      if (receivedData.size() > 1) {
        parseMeasurement();
        publishMeasurementToMqtt();
        deleteSavedMeasurements();
      }
    }
  }

  Serial.println("Going to deep sleep, next wakeup in " + String(TIME_TO_SLEEP / 60 / 60) + " hours");
  Serial.flush();

  mqttClient.loop();
  mqttClient.disconnect();
  bleClient->disconnect();
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);

  // deep sleep
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {
  delay(1000);
}