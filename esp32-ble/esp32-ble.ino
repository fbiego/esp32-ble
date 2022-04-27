/*
  MIT License

  Copyright (c) 2022 Felix Biego

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include "FS.h"
//#include "FFat.h"
#include "SPIFFS.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

#define BUILTINLED 2

#define FORMAT_SPIFFS_IF_FAILED true
#define FORMAT_FFAT_IF_FAILED true

#define USE_SPIFFS  //comment to use FFat

#ifdef USE_SPIFFS
#define FLASH SPIFFS
#else
#define FLASH FFat
#endif

#define SERVICE_UUID              "fb1e4001-54ae-4a28-9f74-dfccb248601d"
#define CHARACTERISTIC_UUID_RX    "fb1e4002-54ae-4a28-9f74-dfccb248601d"
#define CHARACTERISTIC_UUID_TX    "fb1e4003-54ae-4a28-9f74-dfccb248601d"

static BLECharacteristic* pCharacteristicTX;
static BLECharacteristic* pCharacteristicRX;

static bool deviceConnected = false, sendSize = true;
static bool sendFileList = false, uploadFile = false;
static String filePath = "";

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;

    }
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      pServer->startAdvertising();
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {

    //    void onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t code) {
    //      Serial.print("Status ");
    //      Serial.print(s);
    //      Serial.print(" on characteristic ");
    //      Serial.print(pCharacteristic->getUUID().toString().c_str());
    //      Serial.print(" with code ");
    //      Serial.println(code);
    //    }

    void onNotify(BLECharacteristic *pCharacteristic) {
      uint8_t* pData;
      std::string value = pCharacteristic->getValue();
      int len = value.length();
      pData = pCharacteristic->getData();
      if (pData != NULL) {
        //        Serial.print("Notify callback for characteristic ");
        //        Serial.print(pCharacteristic->getUUID().toString().c_str());
        //        Serial.print(" of data length ");
        //        Serial.println(len);
        Serial.print("TX  ");
        for (int i = 0; i < len; i++) {
          Serial.printf("%02X ", pData[i]);
        }
        Serial.println();
      }
    }

    void onWrite(BLECharacteristic *pCharacteristic) {
      uint8_t* pData;
      std::string value = pCharacteristic->getValue();
      int len = value.length();
      pData = pCharacteristic->getData();
      if (pData != NULL) {
        //        Serial.print("Write callback for characteristic ");
        //        Serial.print(pCharacteristic->getUUID().toString().c_str());
        //        Serial.print(" of data length ");
        //        Serial.println(len);
        //        Serial.print("RX  ");
        //        for (int i = 0; i < len; i++) {
        //          Serial.printf("%02X ", pData[i]);
        //        }
        //        Serial.println();

        if (pData[0] == 0xEF) {
          FLASH.format();
          sendSize = true;
        }


      }

    }


};


void initBLE() {
  BLEDevice::init("ESP32 BLE");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristicTX = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY );
  pCharacteristicRX = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pCharacteristicRX->setCallbacks(new MyCallbacks());
  pCharacteristicTX->setCallbacks(new MyCallbacks());
  pCharacteristicTX->addDescriptor(new BLE2902());
  pCharacteristicTX->setNotifyProperty(true);
  pService->start();


  // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Characteristic defined! Now you can read it in your phone!");
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE sketch");
  pinMode(BUILTINLED, OUTPUT);

#ifdef USE_SPIFFS
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
#else
  if (!FFat.begin()) {
    Serial.println("FFat Mount Failed");
    if (FORMAT_FFAT_IF_FAILED) FFat.format();
    return;
  }
#endif


  initBLE();

}

void loop() {


  if (deviceConnected) {
    digitalWrite(BUILTINLED, HIGH);


    if (sendSize) {
      unsigned long x = FLASH.totalBytes();
      unsigned long y = FLASH.usedBytes();
      uint8_t fSize[] = {0xEF, (uint8_t) (x >> 16), (uint8_t) (x >> 8), (uint8_t) x, (uint8_t) (y >> 16), (uint8_t) (y >> 8), (uint8_t) y};
      pCharacteristicTX->setValue(fSize, 7);
      pCharacteristicTX->notify();
      delay(50);
      sendSize = false;
    }

    if (sendFileList) {
      sendFileList = false;
      sendList(FLASH);
    }

    if (uploadFile) {
      uploadFile = false;
      sendFile(filePath);
    }


    // your loop code here when connected to ble
  } else {
    digitalWrite(BUILTINLED, LOW);
  }

  // or here
}

void sendList(fs::FS &fs) {

  File root = fs.open("/");
  if (!root) {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  uint8_t x = 0;
  while (file) {

    String nm = file.name();
    int nLen = nm.length() + 1;
    uint32_t sz = file.size();
    uint8_t dat[5 + nLen];
    dat[0] = 0xCA;
    dat[1] = x;
    dat[2] = (byte)(sz >> 16 & 0xFF);
    dat[3] = (byte)(sz >> 8 & 0xFF);
    dat[4] = (byte)(sz & 0xFF);
    nm.getBytes(dat + 5, nLen);
    Serial.print("  FILE: ");
    Serial.print(file.name());
    Serial.print("\tSIZE: ");
    Serial.println(file.size());
    x++;
    pCharacteristicTX->setValue(dat, 5 + nLen);
    pCharacteristicTX->notify();
    delay(50);
    file = root.openNextFile();
  }
  uint8_t en[2] = {0xCB, x};
  pCharacteristicTX->setValue(en, 2);
  pCharacteristicTX->notify();

}

void sendFile(String path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = FLASH.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return;
  }
  uint32_t sz = file.size();
  String nm = file.name();
  int nLen = nm.length() + 1;
  uint8_t dat[5 + nLen];
  dat[0] = 0xCE;
  dat[1] = 0;
  dat[2] = (byte)(sz >> 16 & 0xFF);
  dat[3] = (byte)(sz >> 8 & 0xFF);
  dat[4] = (byte)(sz & 0xFF);
  nm.getBytes(dat + 5, nLen);
  pCharacteristicTX->setValue(dat, 5 + nLen);
  pCharacteristicTX->notify();
  delay(50);

  Serial.println("- read from file:");
  int y = 0;
  int z = 0;
  uint8_t buf[203];
  while (file.available()) {
    buf[y + 3] = file.read();
    y++;
    z++;
    if (y >= 200) {
      y = 0;
      buf[0] = 0xCF;
      buf[1] = (byte)(z >> 16 & 0xFF);
      buf[2] = (byte)(z >> 8 & 0xFF);
      buf[3] = (byte)(z & 0xFF);
      //send
      pCharacteristicTX->setValue(buf, 203);
      pCharacteristicTX->notify();
      delay(50);
    }
  }
  if (y != 0) {
    buf[0] = 0xCF;
    buf[1] = (byte)(z >> 16 & 0xFF);
    buf[2] = (byte)(z >> 8 & 0xFF);
    buf[3] = (byte)(z & 0xFF);
    pCharacteristicTX->setValue(buf, y + 3);
    pCharacteristicTX->notify();
    delay(50);
  }

  uint8_t en[2] = {0xCE, 1};
  pCharacteristicTX->setValue(en, 2);
  pCharacteristicTX->notify();
  delay(50);
}
