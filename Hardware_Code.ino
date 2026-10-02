#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>

// ---------- Settings ----------
const char* WIFI_SSID  = "YOUR_WIFI_NAME";
const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
const char* SERVER_URL = "YOUR_N8N_WEBHOOK_URL";

const int MAX_RETRIES = 3;
const int HTTP_TIMEOUT = 8000;                       // ms
const unsigned long LOCK_OPEN_TIME = 6000;           // total time the lock stays open
const unsigned long MESSAGE_PHASE_DURATION = 3000;   // welcome message time
const unsigned long DOUBLE_STAR_WINDOW = 700;        // max gap between two '*'

// ---------- Pins ----------
#define LCD_SDA 21
#define LCD_SCL 22
#define RFID_SS 5
#define RFID_RST 4
#define RFID_SCK 18
#define RFID_MISO 19
#define RFID_MOSI 23
#define RELAY_PIN 2

// ---------- Devices ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 rfid(RFID_SS, RFID_RST);

const byte ROWS = 4, COLS = 3;
char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};
byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---------- State ----------
enum State { MAIN_MENU, ENTER_FIRST_ID, ENTER_SECOND_ID, WAIT_RFID_REGISTER, WAIT_RFID_SIGNIN };
State state = MAIN_MENU;

String firstID = "";
String secondID = "";
unsigned long lastStarTime = 0;
bool lastKeyWasStar = false;

// ---------- LCD ----------
void showMessage(String line1, String line2 = "") {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1.substring(0, 16));
  if (line2.length() > 0) {
    lcd.setCursor(0, 1);
    lcd.print(line2.substring(0, 16));
  }
}

void showError(String line1, String line2) {
  showMessage(line1, line2);
  delay(2500);
}

void showMainMenu() {
  showMessage("1:Register", "2:Sign In");
}

// ---------- Double star (**) = go back ----------
bool isDoubleStar() {
  unsigned long now = millis();
  if (lastKeyWasStar && now - lastStarTime <= DOUBLE_STAR_WINDOW) {
    lastKeyWasStar = false;
    return true;
  }
  lastStarTime = now;
  lastKeyWasStar = true;
  return false;
}

void resetStar() {
  lastKeyWasStar = false;
  lastStarTime = 0;
}

void goBack() {
  if (state == ENTER_SECOND_ID) {
    secondID = "";
    state = ENTER_FIRST_ID;
    showMessage("Enter Member ID", firstID);
  } else {
    firstID = "";
    secondID = "";
    state = MAIN_MENU;
    showMainMenu();
  }
  resetStar();
}

// ---------- Reset ----------
void resetRFID() {
  delay(500);
  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  delay(50);
  rfid.PCD_Init();
  delay(150);
}

void resetLCD() {
  delay(100);
  Wire.begin(LCD_SDA, LCD_SCL);
  delay(50);
  lcd.init();
  lcd.backlight();
  lcd.clear();
}

void resetSystem() {
  firstID = "";
  secondID = "";
  resetRFID();
  resetLCD();
  state = MAIN_MENU;
  resetStar();
  showMainMenu();
}

// ---------- WiFi & HTTP ----------
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.disconnect(true);
  delay(300);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
  }
  return WiFi.status() == WL_CONNECTED;
}

// Sends a POST request to n8n, retrying on connection errors
bool postToN8n(const String& body, String& response) {
  for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    if (!connectWiFi()) {
      delay(1000);
      continue;
    }

    WiFiClientSecure client;
    client.setInsecure();  // skips certificate check (fine for testing with ngrok)
    HTTPClient http;

    if (!http.begin(client, SERVER_URL)) {
      delay(1000);
      continue;
    }

    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT);
    int code = http.POST(body);
    response = http.getString();
    http.end();

    if (code >= 200 && code < 300) return true;
    if (code > 0) return false;   // server error, no retry
    delay(1000 * attempt);        // connection error, retry
  }
  return false;
}

// Sends a JSON request and parses the JSON reply
bool request(JsonDocument& req, JsonDocument& res) {
  String body, reply;
  serializeJson(req, body);
  Serial.println("Request: " + body);

  if (!postToN8n(body, reply)) {
    showError("Connection Error", "Try Again");
    return false;
  }
  Serial.println("Reply: " + reply);

  if (reply.length() == 0) {
    showError("Empty Reply", "Check n8n");
    return false;
  }
  if (deserializeJson(res, reply)) {
    showError("Invalid Reply", "Check n8n");
    return false;
  }
  return true;
}

// ---------- Lock ----------
// showAccessMessage = true  -> Register: keep "Access Granted" on screen
// showAccessMessage = false -> Sign in: welcome message first, then "Door is Open"
void openLock(bool showAccessMessage = true) {
  digitalWrite(RELAY_PIN, HIGH);

  if (showAccessMessage) {
    showMessage("Access Granted", "Opening Lock");
    delay(LOCK_OPEN_TIME);
  } else {
    delay(MESSAGE_PHASE_DURATION);
    showMessage("Door is Open", "Please Enter");
    delay(LOCK_OPEN_TIME > MESSAGE_PHASE_DURATION ? LOCK_OPEN_TIME - MESSAGE_PHASE_DURATION : 0);
  }

  digitalWrite(RELAY_PIN, LOW);
  resetRFID();
}

// ---------- n8n actions ----------
// Step 1 of registration: check that the member ID exists
bool checkMember(String memberID) {
  StaticJsonDocument<256> req;
  StaticJsonDocument<512> res;
  req["action"] = "enroll";
  req["stage"] = "check";
  req["member_id"] = memberID;

  if (!request(req, res)) return false;

  bool success = res["success"] | false;
  String status = res["status"] | "";

  if (status == "not_found") {
    showError("ID Not Found", "Try Again");
  } else if (status == "already_registered") {
    showError("Already", "Registered");
  } else if (status == "ready_for_rfid" && success) {
    showMessage("Scan RFID Card");
    state = WAIT_RFID_REGISTER;
    resetStar();
    return true;
  } else {
    showError("Unknown Reply", "Check n8n");
  }
  return false;
}

// Step 2 of registration: link the RFID card to the member
bool registerRFID(String memberID, String uid) {
  StaticJsonDocument<256> req;
  StaticJsonDocument<512> res;
  req["action"] = "enroll";
  req["stage"] = "register";
  req["member_id"] = memberID;
  req["rfid_uid"] = uid;

  if (!request(req, res)) return false;

  bool success = res["success"] | false;
  String status = res["status"] | "";

  if (status == "registered" && success) {
    showMessage("Registered!", "Opening Lock");
    delay(1000);
    openLock();
    return true;
  }

  if (status == "rfid_already_used" || status == "already_registered") {
    showError("RFID Already", "Used");
  } else if (status == "not_found") {
    showError("ID Not Found", "Try Again");
  } else {
    showError("Register Error", "Check n8n");
  }
  return false;
}

// Sign in with an RFID card
bool signIn(String uid) {
  StaticJsonDocument<256> req;
  StaticJsonDocument<512> res;
  req["action"] = "sign_in";
  req["rfid_uid"] = uid;

  if (!request(req, res)) return false;

  bool success = res["success"] | false;
  String status = res["status"] | "";
  String name = res["member_name"] | "";
  int days = res["days_remaining"] | 0;

  if (success && status == "active") {
    showMessage("Welcome " + name, String(days) + " Days Left");
    openLock(false);
    return true;
  }

  if (status == "expired") {
    showMessage("Welcome " + name, "0 Days Left");
    delay(2000);
  } else if (status == "not_registered") {
    showMessage("RFID Not", "Registered");
    delay(2000);
  } else {
    showError("Sign In Error", "Check n8n");
  }
  return false;
}

// ---------- RFID ----------
// Returns the card UID as a hex string, or "" if no card
String readRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return "";

  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  Serial.println("Card UID: " + uid);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  delay(300);
  return uid;
}

// ---------- Keypad input screens ----------
void enterFirstID(char key) {
  if (key == '*') {
    firstID = "";
    showMessage("Enter Member ID", "Press #");
  } else if (key >= '0' && key <= '9') {
    if (firstID.length() < 10) {
      firstID += key;
      showMessage("Member ID:", firstID);
    }
  } else if (key == '#') {
    if (firstID.length() == 0) {
      showMessage("Enter Member ID");
      delay(1500);
      return;
    }
    secondID = "";
    showMessage("Enter ID Again", "Press #");
    state = ENTER_SECOND_ID;
  }
}

void enterSecondID(char key) {
  if (key == '*') {
    secondID = "";
    showMessage("Enter ID Again", "Press #");
  } else if (key >= '0' && key <= '9') {
    if (secondID.length() < 10) {
      secondID += key;
      showMessage("Confirm ID:", secondID);
    }
  } else if (key == '#') {
    if (secondID.length() == 0) {
      showMessage("Enter ID Again");
      delay(1500);
      return;
    }
    if (firstID != secondID) {
      showError("ID Mismatch", "Try Again");
      resetSystem();
      return;
    }
    showMessage("Checking ID...");
    if (!checkMember(firstID)) resetSystem();
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  showMessage("Gym Access", "Starting...");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  rfid.PCD_Init();
  delay(100);

  connectWiFi();
  delay(1000);
  showMainMenu();
}

// ---------- Loop ----------
void loop() {
  char key = keypad.getKey();

  // ** goes back one step (not on the main menu)
  if (key == '*') {
    if (state != MAIN_MENU && isDoubleStar()) {
      goBack();
      return;
    }
  } else if (key) {
    resetStar();
  }

  switch (state) {
    case MAIN_MENU:
      if (key == '1') {
        firstID = "";
        secondID = "";
        showMessage("Enter Member ID", "Press #");
        state = ENTER_FIRST_ID;
      } else if (key == '2') {
        showMessage("Scan Your Card");
        state = WAIT_RFID_SIGNIN;
      }
      break;

    case ENTER_FIRST_ID:
      enterFirstID(key);
      break;

    case ENTER_SECOND_ID:
      enterSecondID(key);
      break;

    case WAIT_RFID_REGISTER: {
      String uid = readRFID();
      if (uid.length() > 0) {
        showMessage("Registering...");
        registerRFID(firstID, uid);
        resetSystem();
      }
      break;
    }

    case WAIT_RFID_SIGNIN: {
      String uid = readRFID();
      if (uid.length() > 0) {
        showMessage("Checking...", "Please Wait");
        signIn(uid);
        resetSystem();
      }
      break;
    }
  }

  delay(20);
}
