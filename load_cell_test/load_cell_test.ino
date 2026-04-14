#include "HX711.h"

// Define the pins for the HX711 communication
const int LOADCELL_DT_PIN = 3;  // DT pin on HX711 to D3 on MKR1000
const int LOADCELL_SCK_PIN = 2; // SCK pin on HX711 to D2 on MKR1000

HX711 scale;

// ---------------------------------------------------
// CALIBRATION FACTOR
// You MUST calibrate this value for your specific 20kg load cell!
// To calibrate:
// 1. Leave this at 1.0 initially.
// 2. Upload code, let it tare (zero out).
// 3. Place a known weight (e.g., exactly 1kg) on the load cell.
// 4. Note the raw value printed on the serial monitor.
// 5. Divide the raw value by your known weight. 
// 6. Update this variable with that result, and re-upload.
// ---------------------------------------------------
float calibration_factor = 1.0; 

void setup() {
  Serial.begin(9600);
  Serial.println("Smart Shelf - 20kg Load Cell Test");
  Serial.println("Initializing the scale...");

  scale.begin(LOADCELL_DT_PIN, LOADCELL_SCK_PIN);

  Serial.println("Tare in progress... Please remove all weights from the scale.");
  delay(2000); // Give the user a moment to clear the scale
  
  scale.set_scale(calibration_factor);
  scale.tare(); // Reset the scale to 0
  
  Serial.println("Tare complete. Ready to take readings.");
}

void loop() {
  if (scale.is_ready()) {
    // 1. Get raw reading without scale factor vs with scale factor
    long raw_value = scale.get_value(10); // Raw reading (tare subtracted, but no scale)
    float weight = scale.get_units(10);   // Final reading (tare subtracted AND divided by scale)
    
    Serial.print("Raw Value: ");
    Serial.print(raw_value);
    Serial.print("\t|\tScaled Weight: ");
    Serial.print(weight, 2); 
    Serial.println(" (units depend on your calibration factor)");
    
  } else {
    Serial.println("Error: HX711 not found or not ready.");
  }

  // Poll the sensor every 1 second
  delay(1000);
}
