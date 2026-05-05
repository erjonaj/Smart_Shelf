#include <SPI.h>
#include <MFRC522.h>
#include <HX711.h>
#include <stdlib.h>

// -------------------------------
// Pin configuration (MKR1000)
// -------------------------------
const int HX711_DT_PIN = 3;
const int HX711_SCK_PIN = 2;
const int RFID_SS_PIN = 5;
const int RFID_RST_PIN = 6;
const unsigned long SERIAL_BAUD_RATE = 57600;

const float DEFAULT_CALIBRATION_FACTOR = 1.0f;
const float WEIGHT_DELTA_TRIGGER = 2.0f;
const unsigned long WEIGHT_HEARTBEAT_MS = 1000;
const unsigned long CARD_DEBOUNCE_MS = 1200;
const byte HX711_SAMPLES_PER_READ = 8;
const byte CALIBRATION_SAMPLES = 15;
const float WEIGHT_EMA_ALPHA = 0.25f;

HX711 scale;
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

float calibration_factor = DEFAULT_CALIBRATION_FACTOR;
float lastSentWeight = 0.0f;
unsigned long lastWeightSendAt = 0;
bool warnedScaleNotReady = false;

float filteredWeight = 0.0f;
bool hasFilteredWeight = false;

String lastCardUid = "";
unsigned long lastCardAt = 0;

char commandBuffer[64];
byte commandIndex = 0;
bool commandOverflowed = false;

void sendEventPrefix(const char *type) {
  Serial.print(type);
  Serial.print("|");
}

void sendSystemMessage(const String &message) {
  sendEventPrefix("SYS");
  Serial.println(message);
}

void sendCardEvent(const String &uid) {
  sendEventPrefix("CARD");
  Serial.println(uid);
}

void sendWeightEvent(float weightValue) {
  sendEventPrefix("WEIGHT");
  Serial.println(weightValue, 2);
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

float readStableWeight() {
  float sample = scale.get_units(HX711_SAMPLES_PER_READ);

  if (!hasFilteredWeight) {
    filteredWeight = sample;
    hasFilteredWeight = true;
  } else {
    filteredWeight = (WEIGHT_EMA_ALPHA * sample) + ((1.0f - WEIGHT_EMA_ALPHA) * filteredWeight);
  }

  return filteredWeight;
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

bool tryParsePositiveFloat(const char *valueText, float *outValue) {
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

  if (parsedValue <= 0.0f) {
    return false;
  }

  *outValue = parsedValue;
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
    hasFilteredWeight = false;
    lastSentWeight = readStableWeight();
    lastWeightSendAt = millis();
    sendWeightEvent(lastSentWeight);
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

  if (strncmp(command, "CALW|", 5) == 0) {
    float knownWeight;
    if (!tryParsePositiveFloat(command + 5, &knownWeight)) {
      sendSystemMessage("CALW_FAIL_INVALID_WEIGHT");
      return;
    }
    if (!scale.is_ready()) {
      sendSystemMessage("CALW_FAIL_SCALE_NOT_READY");
      return;
    }

    float previousScale = calibration_factor;
    scale.set_scale(1.0f);
    float rawReading = scale.get_units(CALIBRATION_SAMPLES);

    if (rawReading == 0.0f) {
      scale.set_scale(previousScale);
      sendSystemMessage("CALW_FAIL_ZERO_READING");
      return;
    }

    calibration_factor = rawReading / knownWeight;
    scale.set_scale(calibration_factor);
    sendEventPrefix("CAL");
    Serial.println(calibration_factor, 6);

    hasFilteredWeight = false;
    lastSentWeight = readStableWeight();
    lastWeightSendAt = millis();
    sendWeightEvent(lastSentWeight);
    sendSystemMessage("CALW_OK");
    return;
  }

  if (strcmp(command, "READ") == 0) {
    if (scale.is_ready()) {
      float weight = readStableWeight();
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

void handleCardScan(const String &uid) {
  unsigned long now = millis();
  bool isDuplicate = (uid == lastCardUid) && (now - lastCardAt) < CARD_DEBOUNCE_MS;
  if (isDuplicate) {
    return;
  }

  lastCardUid = uid;
  lastCardAt = now;
  sendCardEvent(uid);
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);

  SPI.begin();
  rfid.PCD_Init();

  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  scale.set_scale(calibration_factor);

  delay(1500);
  if (scale.is_ready()) {
    scale.tare(15);
    hasFilteredWeight = false;
    lastSentWeight = readStableWeight();
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
    handleCardScan(uid);

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
  float currentWeight = readStableWeight();
  float delta = currentWeight - lastSentWeight;

  bool changedEnough = (delta >= WEIGHT_DELTA_TRIGGER) || (delta <= -WEIGHT_DELTA_TRIGGER);
  bool heartbeatElapsed = (now - lastWeightSendAt) >= WEIGHT_HEARTBEAT_MS;

  if (changedEnough || heartbeatElapsed) {
    sendWeightEvent(currentWeight);
    lastSentWeight = currentWeight;
    lastWeightSendAt = now;
  }
}
