
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <SoftwareSerial.h>
#include "VoiceRecognitionV3.h"

// -------------------- Pin Definitions --------------------
#define RST_PIN 9
#define SS_PIN 10
#define VR_RX 2
#define VR_TX 3
#define VR_BAUD 9600

const int pinGauge       = 6;
const int pinSignalLeft  = 5;
const int pinSignalRight = 7;
const int pinHeadlight   = A0;
const int switchLowBeam  = A1;
const int switchHighBeam = A2;
const int switchMainHeadlight = A3;
const int switchLeft     = 8;
const int switchRight    = 4;

// -------------------- Voice Command IDs --------------------
#define CMD_GAUGE_ON  0
#define CMD_GAUGE_OFF 1
#define CMD_LOGOUT    2

// -------------------- Modules --------------------
MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
VR myVR(VR_RX, VR_TX);

// -------------------- System State --------------------
bool voiceModuleOK = false;
bool isAuthenticated = false;
bool voiceCommandSuccess = false;
bool enableIndicators = false;
bool enableHeadlight = false;

int voiceFailCount = 0;
const int maxFailAttempts = 3;
const unsigned long voiceTimeout = 10000;

uint8_t records[] = {CMD_GAUGE_ON, CMD_GAUGE_OFF, CMD_LOGOUT};
uint8_t buf[64];

// Authorized UIDs
byte authorizedUIDs[2][4] = {
  {0x66, 0x51, 0xCD, 0x1F},
  {0xFB, 0xE7, 0x32, 0x02}
};

// -------------------- Setup --------------------
void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(pinGauge, OUTPUT);
  pinMode(pinSignalLeft, OUTPUT);
  pinMode(pinSignalRight, OUTPUT);
  pinMode(pinHeadlight, OUTPUT);

  pinMode(switchLeft, INPUT_PULLUP);
  pinMode(switchRight, INPUT_PULLUP);
  pinMode(switchLowBeam, INPUT_PULLUP);
  pinMode(switchHighBeam, INPUT_PULLUP);
  pinMode(switchMainHeadlight, INPUT_PULLUP);

  digitalWrite(pinGauge, HIGH);
  digitalWrite(pinSignalLeft, HIGH);
  digitalWrite(pinSignalRight, HIGH);
  digitalWrite(pinHeadlight, HIGH);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("System Initializing");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Checking Voice...");
  myVR.begin(VR_BAUD);

  if (myVR.clear() == 0 && myVR.load(records, sizeof(records)) >= 0) {
    lcd.setCursor(0, 1);
    lcd.print("Voice Ready");
    voiceModuleOK = true;
  } else {
    lcd.setCursor(0, 1);
    lcd.print("Voice Fail");
  }

  delay(2000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Waiting for RFID");
}

// -------------------- Main Loop --------------------
void loop() {
  if (enableIndicators) {
    handleIndicatorSwitch();
    handleHeadlightSwitch();
  }

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  if (isAuthenticated) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Logged Out");
    resetSystem();
    delay(2000);
    resetLCD();
    return;
  }

  if (isAuthorized(rfid.uid.uidByte)) {
    isAuthenticated = true;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RFID Verified");

    blinkSignalLights(1);

    lcd.setCursor(0, 1);
    lcd.print("Say command...");

    if (voiceModuleOK) {
      listenForVoiceCommand();
    } else {
      delay(2000);
    }
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Access Denied!");
    Serial.println("Unauthorized card");
    delay(2000);
  }

  resetLCD();
}

// -------------------- Voice Listening --------------------
void listenForVoiceCommand() {
  bool voiceRecognized = false;
  unsigned long startTime = millis();

  while (millis() - startTime < voiceTimeout) {
    if (myVR.recognize(buf, 50) > 0) {
      voiceRecognized = true;
      break;
    }
  }

  lcd.clear();

  if (voiceRecognized) {
    uint8_t cmd = buf[1];
    Serial.print("Voice CMD ID: ");
    Serial.println(cmd);
    voiceFailCount = 0;
    handleVoiceCommand(cmd);
  } else {
    voiceFailCount++;
    lcd.setCursor(0, 0);
    lcd.print("Voice Timeout");

    if (voiceFailCount >= maxFailAttempts) {
      lcd.setCursor(0, 1);
      lcd.print("Check Module");
      delay(3000);
      voiceFailCount = 0;
    } else {
      delay(2000);
    }
  }
}

// -------------------- Handle Commands --------------------
void handleVoiceCommand(uint8_t cmd) {
  lcd.clear();
  voiceCommandSuccess = false;

  switch (cmd) {
    case CMD_GAUGE_ON:
      digitalWrite(pinGauge, LOW);
      lcd.print("Gauge ON");
      voiceCommandSuccess = true;
      break;

    case CMD_GAUGE_OFF:
      digitalWrite(pinGauge, HIGH);
      lcd.print("Gauge OFF");
      voiceCommandSuccess = true;
      break;

    case CMD_LOGOUT:
      resetSystem();
      lcd.print("System Logged Out");
      voiceCommandSuccess = true;
      isAuthenticated = false;
      delay(2000);
      resetLCD();
      return; // Exit early to skip enabling signal/headlight
      break;

    default:
      lcd.print("Unknown Cmd");
      break;
  }

  delay(1000);

  if (voiceCommandSuccess && cmd != CMD_LOGOUT) {
    blinkSignalLights(2);
    enableIndicators = true;
    enableHeadlight = true;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Signal Enabled");
    lcd.setCursor(0, 1);
    lcd.print("Headlight Ready");
    delay(2000);
  }
}

// -------------------- Turn Signal Logic --------------------
void handleIndicatorSwitch() {
  bool leftPressed = digitalRead(switchLeft) == LOW;
  bool rightPressed = digitalRead(switchRight) == LOW;

  if (leftPressed && !rightPressed) {
    digitalWrite(pinSignalLeft, LOW);
    digitalWrite(pinSignalRight, HIGH);
  } else if (rightPressed && !leftPressed) {
    digitalWrite(pinSignalLeft, HIGH);
    digitalWrite(pinSignalRight, LOW);
  } else {
    digitalWrite(pinSignalLeft, HIGH);
    digitalWrite(pinSignalRight, HIGH);
  }
}

// -------------------- Headlight Logic --------------------
void handleHeadlightSwitch() {
  if (!enableHeadlight) return;

  bool mainSwitch = digitalRead(switchMainHeadlight) == LOW;
  bool lowBeam = digitalRead(switchLowBeam) == LOW;
  bool highBeam = digitalRead(switchHighBeam) == LOW;

  if (mainSwitch) {
    digitalWrite(pinHeadlight, LOW);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Headlight ON");

    if (highBeam) {
      lcd.setCursor(0, 1);
      lcd.print("Low Beam");
    } else if (lowBeam) {
      lcd.setCursor(0, 1);
      lcd.print("High Beam");
    } else {
      lcd.setCursor(0, 1);
      lcd.print("No Beam Sel.");
    }

    delay(1000);
  } else {
    digitalWrite(pinHeadlight, HIGH);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Headlight OFF");
    delay(1500);
    resetLCD();
  }
}

// -------------------- Signal Blinking Function --------------------
void blinkSignalLights(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pinSignalLeft, LOW);
    digitalWrite(pinSignalRight, LOW);
    delay(300);
    digitalWrite(pinSignalLeft, HIGH);
    digitalWrite(pinSignalRight, HIGH);
    delay(300);
  }
}

// -------------------- UID Auth --------------------
bool isAuthorized(byte *uid) {
  for (int i = 0; i < 2; i++) {
    bool match = true;
    for (int j = 0; j < 4; j++) {
      if (uid[j] != authorizedUIDs[i][j]) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

// -------------------- System Reset --------------------
void resetSystem() {
  isAuthenticated = false;
  enableIndicators = false;
  enableHeadlight = false;
  digitalWrite(pinSignalLeft, HIGH);
  digitalWrite(pinSignalRight, HIGH);
  digitalWrite(pinHeadlight, HIGH);
  digitalWrite(pinGauge, HIGH);
}

// -------------------- LCD Reset --------------------
void resetLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Waiting for RFID");
}
