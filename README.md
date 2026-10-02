# Gym Access System

A simple access control system for an iron gym. Members register on a website, link an RFID card, and open the door by scanning the card. The door only opens if the subscription is active.

**Live website:** https://1hulk-gym.netlify.app

## How it works

1. The member picks a plan and registers on the website, then gets a **membership number**.
2. At the gate, the member enters the number on the keypad (twice, to confirm) and scans an RFID card. The card is now linked to the member.
3. Next time, the member just scans the card. The ESP32 asks n8n, n8n checks **Supabase**, and the door opens if the subscription is still valid.
4. Members can renew their subscription from the website using their phone number.

Website / ESP32  →  n8n webhook  →  Supabase (database)

## Tech stack

- **Website:** HTML, CSS, JavaScript (hosted on Netlify)
- **Automation:** n8n
- **Database:** Supabase
- **Hardware:** ESP32 programmed in C++ (Arduino)

## Hardware

- ESP32
- RC522 RFID reader
- 16x2 LCD with I2C
- 4x3 keypad
- 5V relay
- 12V electric lock

| Part | ESP32 pins |
|------|------------|
| LCD (I2C) | SDA 21, SCL 22 |
| RFID RC522 | SS 5, RST 4, SCK 18, MISO 19, MOSI 23 |
| Relay | 2 |
| Keypad rows | 13, 12, 14, 27 |
| Keypad columns | 26, 25, 33 |

## Keypad controls

- `1` Register a new card, `2` Sign in
- `#` Confirm
- `*` Clear the current input
- `**` (double press) Go back

## Folder structure

```
gym-access-system/
├── website/
│   └── index.html
├── hardware/
│   └── gym_access.ino
└── README.md
```

## Setup

**Website**
1. Open `website/index.html` and set `N8N` to your n8n webhook URL.
2. Upload the file to Netlify (or any static hosting).

**ESP32**
1. Install these libraries in Arduino IDE: `ArduinoJson` (v6), `MFRC522`, `LiquidCrystal_I2C`, `Keypad`.
2. Open `hardware/gym_access.ino` and set `WIFI_SSID`, `WIFI_PASS` and `SERVER_URL`.
3. Select your ESP32 board and upload.

**n8n and Supabase**
1. Create a Supabase project with a members table (name, phone, email, plan, RFID UID, subscription end date).
2. Create an n8n workflow with a webhook that handles these actions: `subscription`, `Renew subscription`, `enroll` and `sign_in`.

## Note

Do not upload your real WiFi password or webhook URL to GitHub. Keep the placeholders in the public code.

## Author

Mohammad Salous, Computer Engineering student at An-Najah National University.
[GitHub](https://github.com/Mohammad-Salous) · [LinkedIn](https://www.linkedin.com/in/mohammad-salous)
