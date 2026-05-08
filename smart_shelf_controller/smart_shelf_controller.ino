include <SPI.h>
#include <MFRC522.h>
#include <HX711.h>

// -------------------------
// PINS
// -------------------------
const int HX711_DT = 3;
const int HX711_SCK = 2;

const int RFID_SS = 5;
const int RFID_RST = 6;

// -------------------------
// SETTINGS
// -------------------------
const unsigned long INTERVAL = 2000; 

const float BOTTLE_WEIGHT = 500.0;
const float PRICE_PER_BOTTLE = 1.5;

// -------------------------
// OBJECTS
// -------------------------
HX711 scale;
MFRC522 rfid(RFID_SS, RFID_RST);

// -------------------------
// STATE
// -------------------------
String activeUser = "";
int lastBottleCount = 0;

unsigned long lastTime = 0;

// -------------------------
// RFID
// -------------------------
String getUID(const MFRC522::Uid &uid) {
  String s = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) s += "0";
    s += String(uid.uidByte[i], HEX);
  }
  s.toUpperCase();
  return s;
}

// -------------------------
// SETUP
// -------------------------
void setup() {
  Serial.begin(57600);
  while (!Serial);

  SPI.begin();
  rfid.PCD_Init();

  scale.begin(HX711_DT, HX711_SCK);

  delay(2000);

  // calibration
  scale.set_scale(93.15);
  scale.tare(20);

  Serial.println("SMART SHELF READY");
}

// -------------------------
// LOOP
// -------------------------
void loop() {

  // RFID
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    activeUser = getUID(rfid.uid);

    Serial.print("CARD|");
    Serial.println(activeUser);

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  // -------------------------
  // WEIGHT CHECK
  // -------------------------
  if (millis() - lastTime > INTERVAL) {
    lastTime = millis();

    float weight = scale.get_units(10);

    if (weight < 0) weight = 0;

    Serial.print("WEIGHT|");
    Serial.println(weight);

    // -------------------------
    // CONVERT TO BOTTLES
    // -------------------------
    int bottleCount = round(weight / BOTTLE_WEIGHT);

    Serial.print("BOTTLES|");
    Serial.println(bottleCount);

    // -------------------------
    // DETECT REMOVAL
    // -------------------------
    if (bottleCount < lastBottleCount) {

      int removed = lastBottleCount - bottleCount;

      if (activeUser != "") {
        float price = removed * PRICE_PER_BOTTLE;

        Serial.print("CHARGE|");
        Serial.print(removed);
        Serial.print("|");
        Serial.println(price);
      }
    }

    lastBottleCount = bottleCount;
  }
}