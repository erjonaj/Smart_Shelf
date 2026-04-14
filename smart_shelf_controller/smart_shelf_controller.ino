#include <SPI.h>
#include <MFRC522.h>
#include "HX711.h"
#include <stdlib.h>

// -------------------------------
// Pin configuration (MKR1000)
// -------------------------------
const int HX711_DT_PIN = 3;
const int HX711_SCK_PIN = 2;
const int RFID_SS_PIN = 5;
const int RFID_RST_PIN = 6;

HX711 scale;
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

// Set this after calibrating your load cell with a known weight.
float calibration_factor = 1.0;

const float WEIGHT_DELTA_TRIGGER = 2.0;
const unsigned long WEIGHT_HEARTBEAT_MS = 1000;
const unsigned long CARD_DEBOUNCE_MS = 1500;

float lastSentWeight = 0.0;
unsigned long lastWeightSendAt = 0;
String lastCardUid = "";
unsigned long lastCardAt = 0;
bool warnedScaleNotReady = false;
bool commandOverflowed = false;

char commandBuffer[64];
byte commandIndex = 0;

void sendEventPrefix(const char *type) {
  Serial.print(type);
  Serial.print("|");
}

void sendSystemMessage(const char *message) {
  sendEventPrefix("SYS");
  Serial.println(message);
}

String uidToHexString(const MFRC522::Uid &uid) {
  String out = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) {
      out += "0";
    }
    out += String(uid.uidByte[i], HEX);
  }
  out.toUpperCase();
  return out;
}

void sendWeightEvent(float weightValue) {
  sendEventPrefix("WEIGHT");
  Serial.println(weightValue, 2);
}

bool tryParseCalibrationFactor(const char *valueText, float *outFactor) {
  char *endPtr;
  float parsedValue = strtof(valueText, &endPtr);

  if (endPtr == valueText) {
    return false;
  }

  while (*endPtr == ' ') {
    endPtr++;
  }

  if (*endPtr != '\0') {
    return false;
  }

  if (parsedValue == 0.0f) {
    return false;
  }

  *outFactor = parsedValue;
  return true;
}

void handleCommand(const char *command) {
  if (strcmp(command, "PING") == 0) {
    sendEventPrefix("PONG");
    Serial.println("OK");
    return;
  }

  if (strcmp(command, "TARE") == 0) {
    if (!scale.is_ready()) {
      sendSystemMessage("TARE_FAIL_SCALE_NOT_READY");
      return;
    }
    scale.tare(15);
    sendSystemMessage("TARE_OK");
    return;
  }

  if (strncmp(command, "CAL|", 4) == 0) {
    float parsedFactor;
    if (!tryParseCalibrationFactor(command + 4, &parsedFactor)) {
      sendSystemMessage("CAL_FAIL_INVALID_VALUE");
      return;
    }
    calibration_factor = parsedFactor;
    scale.set_scale(calibration_factor);
    sendEventPrefix("CAL");
    Serial.println(calibration_factor, 6);
    return;
  }

  if (strcmp(command, "READ") == 0) {
    if (scale.is_ready()) {
      float weight = scale.get_units(5);
      sendWeightEvent(weight);
    } else {
      sendSystemMessage("READ_FAIL_SCALE_NOT_READY");
    }
    return;
  }

  sendSystemMessage("UNKNOWN_COMMAND");
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\n' || c == '\r') {
      if (commandOverflowed) {
        sendSystemMessage("CMD_TOO_LONG");
        commandIndex = 0;
        commandOverflowed = false;
      } else if (commandIndex > 0) {
        commandBuffer[commandIndex] = '\0';
        handleCommand(commandBuffer);
        commandIndex = 0;
      }
      continue;
    }

    if (commandIndex < sizeof(commandBuffer) - 1) {
      commandBuffer[commandIndex++] = c;
    } else {
      commandOverflowed = true;
    }
  }
}

void setup() {
  Serial.begin(115200);

  SPI.begin();
  rfid.PCD_Init();

  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  scale.set_scale(calibration_factor);

  delay(1500);
  if (scale.is_ready()) {
    scale.tare(15);
    lastSentWeight = scale.get_units(8);
    lastWeightSendAt = millis();
    sendWeightEvent(lastSentWeight);
    sendSystemMessage("BOOT_OK");
  } else {
    sendSystemMessage("BOOT_SCALE_NOT_READY");
  }
}

void loop() {
  readSerialCommands();

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uid = uidToHexString(rfid.uid);
    unsigned long now = millis();

    bool isDuplicate = (uid == lastCardUid) && (now - lastCardAt < CARD_DEBOUNCE_MS);
    if (!isDuplicate) {
      sendEventPrefix("CARD");
      Serial.println(uid);
      lastCardUid = uid;
      lastCardAt = now;
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  if (!scale.is_ready()) {
    if (!warnedScaleNotReady) {
      sendSystemMessage("SCALE_NOT_READY");
      warnedScaleNotReady = true;
    }
    return;
  }

  warnedScaleNotReady = false;

  unsigned long now = millis();
  float currentWeight = scale.get_units(5);
  float delta = currentWeight - lastSentWeight;

  bool changedEnough = (delta >= WEIGHT_DELTA_TRIGGER) || (delta <= -WEIGHT_DELTA_TRIGGER);
  bool heartbeatElapsed = (now - lastWeightSendAt) >= WEIGHT_HEARTBEAT_MS;

  if (changedEnough || heartbeatElapsed) {
    sendWeightEvent(currentWeight);
    lastSentWeight = currentWeight;
    lastWeightSendAt = now;
  }
}
