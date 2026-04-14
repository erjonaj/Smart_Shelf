# Smart Shelf (MKR1000 + RFID + HX711 + Web Serial)

This project implements a smart shelf flow:

1. Customer taps RFID card.
2. Browser app opens or activates that customer tab.
3. Weight changes from the shelf are converted into product quantity changes.
4. Customer total is updated live.

## Folder Structure

- `smart_shelf_controller/smart_shelf_controller.ino` -> Arduino firmware for MKR1000, RC522 RFID, HX711
- `frontend/index.html` -> Browser UI
- `frontend/styles.css` -> UI styling
- `frontend/app.js` -> Web Serial logic, registration, billing sessions

## Hardware Wiring

### HX711 -> MKR1000

- `HX711 DT` -> `D3`
- `HX711 SCK` -> `D2`
- `HX711 VCC` -> `5V` (or module-safe VCC)
- `HX711 GND` -> `GND`

### RC522 RFID (SPI) -> MKR1000

- `SDA/SS` -> `D5`
- `RST` -> `D6`
- `MOSI` -> `MOSI`
- `MISO` -> `MISO`
- `SCK` -> `SCK`
- `3.3V` -> `3.3V`
- `GND` -> `GND`

If your wiring differs, update pin constants in `smart_shelf_controller/smart_shelf_controller.ino`.

## Arduino Libraries

Install these in Arduino IDE Library Manager:

- `HX711` (Bogde style HX711 library)
- `MFRC522`

## Serial Protocol

Firmware sends one line per event:

- `CARD|<UID_HEX>`
- `WEIGHT|<float>`
- `SYS|<message>`
- `PONG|OK`
- `CAL|<factor>`

Browser sends commands:

- `PING`
- `TARE`
- `READ`
- `CAL|<factor>`

Baud rate: `115200`

## Calibration

1. Keep `calibration_factor` in firmware at `1.0` for first run.
2. Upload firmware and connect frontend.
3. Tare with an empty shelf.
4. Place a known weight and read output.
5. Compute and set correct factor, then send it from UI or hardcode in firmware.

## Run the Frontend

Web Serial requires a secure context (`https://` or `http://localhost`).

From project root:

```bash
cd frontend
python3 -m http.server 5500
```

Then open:

- `http://localhost:5500`

Click **Connect Serial**, select your MKR1000 port, and start scanning cards.

## How Billing Works

- First unknown card tap opens registration modal.
- Known card tap activates customer tab.
- When shelf weight drops by threshold, items are added to active customer tab.
- When shelf weight increases by threshold, items are treated as returns for active tab.

The app assumes one product type per shelf zone (configured in UI with unit weight and price).
