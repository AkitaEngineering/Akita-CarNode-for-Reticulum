// AkitaCarNode_Reticulum.ino

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <LoRa.h>
#include <WiFi.h>
#include <Reticulum.h>

// --- Configuration ---
#define LORA_BAND 915E6 // Adjust for your region
#define LORA_SS 18
#define LORA_RST 14
#define LORA_DI0 26

#define GPS_RX 34
#define GPS_TX 12

static BLEUUID serviceUUID("YOUR_OBDII_SERVICE_UUID"); // Replace with your OBD-II service UUID
static BLEUUID characteristicUUID("YOUR_OBDII_CHARACTERISTIC_UUID"); // Replace with your OBD-II characteristic UUID

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

String obdCommand = "010C\r"; // Example: Engine RPM
String obdResponse = "";

float rpm = 0;
float speed = 0;
float latitude = 0;
float longitude = 0;
float altitude = 0;
float sats = 0;
float temp = 0;

SoftwareSerial gpsSerial(GPS_RX, GPS_TX);
TinyGPSPlus gps;

// --- Reticulum Configuration ---
#define RETICULUM_WIFI true // Set to false for LoRa Reticulum
#define WIFI_SSID "YOUR_WIFI_SSID" // Replace with your WiFi SSID
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD" // Replace with your WiFi password

Reticulum reticulum;
Link* link;

// --- BLE Callbacks ---
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    connected = true;
    Serial.println("Connected to OBD-II");
  }
  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("Disconnected from OBD-II");
  }
};

bool connectToServer() {
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient* pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  if (!pClient->connect(myDevice)) {
    Serial.println("Failed to connect, starting scan");
    return false;
  }
  Serial.println("Connected to server");

  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID);
    pClient->disconnect();
    return false;
  }
  Serial.println("Found our service");

  pRemoteCharacteristic = pRemoteService->getCharacteristic(characteristicUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(characteristicUUID);
    pClient->disconnect();
    return false;
  }
  Serial.println("Found our characteristic");

  return true;
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(serviceUUID)) {
      Serial.print("Found our device! address: ");
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = false;
    }
  }
};

// --- Reticulum Setup ---
void setupReticulum() {
  if (RETICULUM_WIFI) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("WiFi connected");
    reticulum.interfaces.addWifi(WiFi.localIP().toString().c_str());
  } else {
    SPI.begin(5, 19, 27, 18);
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DI0);
    if (!LoRa.begin(LORA_BAND)) {
      Serial.println("Starting LoRa failed!");
      while (1);
    }
    Serial.println("LoRa Initialized");
    reticulum.interfaces.addLora(LORA_BAND);
  }
  reticulum.begin();
  Serial.println("Reticulum initialized");

  reticulum.announce();
  Serial.println("Node announced on Reticulum network.");
}

// --- Reticulum Send Data ---
void sendReticulumData(String data) {
  if (link == NULL) {
    link = reticulum.links.create(reticulum.identity.destination());
  }
  if (link->status() == Link::CONNECTED) {
    reticulum.links.transmit(link, data.c_str(), data.length());
    Serial.println("Reticulum data sent");
  } else {
    Serial.println("Link not connected, attempting to reconnect");
    link->reconnect();
  }
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600);

  setupReticulum();

  BLEDevice::init("AkitaCarNode");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}

// --- Loop ---
void loop() {
  if (doConnect) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
      connected = true;
    } else {
      Serial.println("We have failed to connect to the server; restart scan.");
      BLEDevice::getScan()->start(0);
    }
    doConnect = false;
  }

  if (connected) {
    pRemoteCharacteristic->writeValue(obdCommand.c_str(), obdCommand.length());
    delay(100);

    if (pRemoteCharacteristic->canRead()) {
      std::string value = pRemoteCharacteristic->readValue();
      obdResponse = String(value.c_str());
      Serial.print("OBD-II Response: ");
      Serial.println(obdResponse);

      if (obdResponse.startsWith("41 0C")) {
        String hexRPM = obdResponse.substring(6, 11);
        long rpmHex = strtol(hexRPM.c_str(), NULL, 16);
        rpm = rpmHex / 4.0;
        Serial.print("RPM: ");
        Serial.println(rpm);
      }
      if (obdResponse.startsWith("41 0D")) {
        String hexSpeed = obdResponse.substring(6, 8);
        long speedHex = strtol(hexSpeed.c_str(), NULL, 16);
        speed = speedHex;
        Serial.print("Speed: ");
        Serial.println(speed);
      }
      if (obdResponse.startsWith("41 05")) {
        String hexTemp = obdResponse.substring(6, 8);
        long tempHex = strtol(hexTemp.c_str(), NULL, 16);
        temp = tempHex - 40;
        Serial.print("Temperature: ");
        Serial.println(temp);
      }
    }
  } else if (doScan) {
    BLEDevice::getScan()->start(0);
  }

  while (gpsSerial.available() > 0) {
    if (gps.encode(gpsSerial.read())) {
      if (gps.location.isValid()) {
        latitude = gps.location.lat();
        longitude = gps.location.lng();
        altitude = gps.altitude.meters();
        sats = gps.satellites.value();
        Serial.print("GPS: Lat="); Serial.print(latitude, 6);
        Serial.print(" Long="); Serial.print(longitude, 6);
        Serial.print(" Alt="); Serial.print(altitude);
        Serial.print(" Sats="); Serial.println(sats);
      }
    }
  }

  String data = String(rpm) + "," + String(speed) + "," + String(latitude, 6) + "," + String(longitude, 6) + "," + String(temp);
  sendReticulumData(data);

  delay(5000); // Send data every 5 seconds
}
