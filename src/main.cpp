#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PN532_SPI.h>
#include <PN532.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

PN532_SPI pn532spi(SPI, 16); // D0 (GPIO16) as CS to avoid GPIO15 boot constraints
PN532 nfc(pn532spi);

// Helper to format balance to Rp XX.XXX
String formatRupiah(uint32_t amount) {
  String val = String(amount);
  String result = "";
  int len = val.length();
  int count = 0;
  for (int i = len - 1; i >= 0; i--) {
    result = val[i] + result;
    count++;
    if (count % 3 == 0 && i > 0) {
      result = "." + result;
    }
  }
  return "Rp " + result;
}

// Format card number to XXXX XXXX XXXX XXXX
String formatCardNumber(String raw) {
  String formatted = "";
  for (unsigned int i = 0; i < raw.length(); i++) {
    formatted += raw[i];
    if ((i + 1) % 4 == 0 && (i + 1) < raw.length()) {
      formatted += " ";
    }
  }
  return formatted;
}

void showMessage(const char* line1, const char* line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 4);
  display.println(line1);
  display.setCursor(0, 18);
  display.println(line2);
  display.display();
}

bool isAPDUHandlingSuccess(const uint8_t* response, uint8_t len) {
  if (len < 2) return false;
  uint8_t sw1 = response[len - 2];
  uint8_t sw2 = response[len - 1];
  return (sw1 == 0x90 || sw1 == 0x91) && sw2 == 0x00;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nPrepaid Card Reader Starting (Elechouse SPI)...");

  // Initialize OLED display using SDA=D2 (GPIO4) and SCL=D1 (GPIO5)
  Wire.begin(4, 5);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 OLED failed");
    while (1) { delay(10); }
  }
  
  display.clearDisplay();
  showMessage("Prepaid Reader", "Starting...");
  delay(1000);

  // Initialize PN532 via SPI
  SPI.begin();
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 not found");
    showMessage("Hardware Error", "PN532 not found");
    while (1) { delay(10); }
  }
  
  // Configure PN532 to read RFID tags
  nfc.SAMConfig();
  nfc.setPassiveActivationRetries(0x10);
  
  Serial.println("Ready to scan cards.");
  showMessage("Reader Ready", "Scan card now...");
}

bool checkEMoney() {
  uint8_t response[250];
  uint8_t responseLength = sizeof(response);
  
  // 1. Select DF EMoney
  uint8_t select_emoney[] = { 0x00, 0xA4, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
  if (!nfc.inDataExchange(select_emoney, sizeof(select_emoney), response, &responseLength)) {
    Serial.println("EMoney: Select DF inDataExchange failed");
    return false;
  }
  delay(15);
  if (!isAPDUHandlingSuccess(response, responseLength)) {
    Serial.print("EMoney: Select DF failed with SW: 0x");
    if(responseLength >= 2) {
      Serial.print(response[responseLength-2], HEX);
      Serial.println(response[responseLength-1], HEX);
    } else {
      Serial.println("unknown");
    }
    return false;
  }
  
  Serial.println("Detected: Mandiri E-Money");

  // 2. Read Card Number
  uint8_t read_number[] = { 0x00, 0xB3, 0x00, 0x00, 0x3F };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_number, sizeof(read_number), response, &responseLength)) {
    Serial.println("EMoney: Read Number inDataExchange failed");
    return false;
  }
  delay(15);
  if (responseLength < 10) return false;
  
  String cardNum = "";
  for (int i = 0; i < 8; i++) {
    if (response[i] < 0x10) cardNum += "0";
    cardNum += String(response[i], HEX);
  }
  cardNum.toUpperCase();
  Serial.println("Card Number: " + cardNum);

  // 3. Read Balance
  uint8_t read_balance[] = { 0x00, 0xB5, 0x00, 0x00, 0x0A };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_balance, sizeof(read_balance), response, &responseLength)) {
    Serial.println("EMoney: Read Balance inDataExchange failed");
    return false;
  }
  delay(15);
  if (responseLength < 6) return false;
  
  // Balance is 4-byte little-endian at offset 0
  uint32_t balance = ((uint32_t)response[3] << 24) | 
                     ((uint32_t)response[2] << 16) | 
                     ((uint32_t)response[1] << 8)  | 
                     response[0];
  
  Serial.println("Balance: " + String(balance));
  
  // Display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("E-MONEY");
  display.setCursor(0, 10);
  display.println(formatCardNumber(cardNum));
  display.setCursor(0, 22);
  display.setTextSize(1);
  display.println(formatRupiah(balance));
  display.display();
  
  return true;
}

bool checkTapCash() {
  uint8_t response[250];
  uint8_t responseLength = sizeof(response);
  
  // 1. Try ISO Select DF TapCash
  uint8_t select_tapcash_iso[] = { 0x00, 0xA4, 0x04, 0x00, 0x08, 0xA0, 0x00, 0x42, 0x4E, 0x49, 0x10, 0x00, 0x01 };
  bool selected = false;
  
  if (nfc.inDataExchange(select_tapcash_iso, sizeof(select_tapcash_iso), response, &responseLength) && isAPDUHandlingSuccess(response, responseLength)) {
    selected = true;
  } else {
    // Try native DESFire Select (99 99 99)
    uint8_t select_native_99[] = { 0x90, 0x5A, 0x00, 0x00, 0x03, 0x99, 0x99, 0x99, 0x00 };
    responseLength = sizeof(response);
    if (nfc.inDataExchange(select_native_99, sizeof(select_native_99), response, &responseLength) && (responseLength >= 1 && response[responseLength-1] == 0x00)) {
      selected = true;
    } else {
      // Try native DESFire Select (10 00 01 / 01 00 10)
      uint8_t select_native_10[] = { 0x90, 0x5A, 0x00, 0x00, 0x03, 0x10, 0x00, 0x01, 0x00 };
      responseLength = sizeof(response);
      if (nfc.inDataExchange(select_native_10, sizeof(select_native_10), response, &responseLength) && (responseLength >= 1 && response[responseLength-1] == 0x00)) {
        selected = true;
      }
    }
  }

  if (!selected) {
    Serial.println("TapCash: Select DF failed on all attempts");
    return false;
  }
  
  Serial.println("Detected: BNI TapCash");

  // 2. Read Balance and Card Info
  uint8_t read_tapcash[] = { 0x90, 0x32, 0x03, 0x00, 0x00 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_tapcash, sizeof(read_tapcash), response, &responseLength)) {
    Serial.println("TapCash: Read Info inDataExchange failed");
    return false;
  }
  delay(15);
  if (responseLength < 16) return false;

  // Balance is 3-byte big-endian at offset 2
  uint32_t balance = ((uint32_t)response[2] << 16) | 
                     ((uint32_t)response[3] << 8)  | 
                     response[4];
  
  // Card Number is 8 bytes at offset 8
  String cardNum = "";
  for (int i = 8; i < 16; i++) {
    if (response[i] < 0x10) cardNum += "0";
    cardNum += String(response[i], HEX);
  }
  cardNum.toUpperCase();

  Serial.println("Card Number: " + cardNum);
  Serial.println("Balance: " + String(balance));

  // Display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("TAPCASH");
  display.setCursor(0, 10);
  display.println(formatCardNumber(cardNum));
  display.setCursor(0, 22);
  display.println(formatRupiah(balance));
  display.display();

  return true;
}

bool checkBrizzi() {
  uint8_t response[250];
  uint8_t responseLength = sizeof(response);
  
  // 1. Select DF Brizzi
  uint8_t select_brizzi[] = { 0x90, 0x5A, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00 };
  if (!nfc.inDataExchange(select_brizzi, sizeof(select_brizzi), response, &responseLength)) {
    Serial.println("Brizzi: Select DF inDataExchange failed");
    return false;
  }
  delay(15);
  // DESFire success is response ending with status byte 0x00
  if (responseLength < 1 || response[responseLength - 1] != 0x00) {
    Serial.print("Brizzi: Select DF failed with SW: 0x");
    if(responseLength >= 1) {
      Serial.println(response[responseLength-1], HEX);
    } else {
      Serial.println("unknown");
    }
    return false;
  }
  
  Serial.println("Detected: BRI Brizzi");

  // 2. Read Card Info (Card Number)
  uint8_t read_brizzi_info[] = { 0x90, 0xBD, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_brizzi_info, sizeof(read_brizzi_info), response, &responseLength)) {
    Serial.println("Brizzi: Read Info inDataExchange failed");
    return false;
  }
  delay(15);
  if (responseLength < 12) return false;
  
  // Card number is 8 bytes starting at byte offset 3
  String cardNum = "";
  for (int i = 3; i < 11; i++) {
    if (response[i] < 0x10) cardNum += "0";
    cardNum += String(response[i], HEX);
  }
  cardNum.toUpperCase();
  Serial.println("Card Number: " + cardNum);

  // 3. Select Balance File
  uint8_t brizzi_select_bal[] = { 0x90, 0xBD, 0x00, 0x00, 0x07, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(brizzi_select_bal, sizeof(brizzi_select_bal), response, &responseLength)) {
    Serial.println("Brizzi: Select Balance inDataExchange failed");
    return false;
  }
  delay(15);

  // 4. Read Balance
  uint8_t brizzi_read_bal[] = { 0x90, 0x6C, 0x00, 0x00, 0x01, 0x00, 0x00 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(brizzi_read_bal, sizeof(brizzi_read_bal), response, &responseLength)) {
    Serial.println("Brizzi: Read Balance inDataExchange failed");
    return false;
  }
  delay(15);
  if (responseLength < 5) return false;

  // Balance is 4-byte little-endian at offset 0
  uint32_t balance = ((uint32_t)response[3] << 24) | 
                     ((uint32_t)response[2] << 16) | 
                     ((uint32_t)response[1] << 8)  | 
                     response[0];
  
  Serial.println("Balance: " + String(balance));

  // Display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("BRIZZI");
  display.setCursor(0, 10);
  display.println(formatCardNumber(cardNum));
  display.setCursor(0, 22);
  display.println(formatRupiah(balance));
  display.display();

  return true;
}

bool checkFlazz() {
  uint8_t response[250];
  uint8_t responseLength = sizeof(response);
  
  // 1. Select Flazz Application DF
  uint8_t select_flazz[] = { 0x00, 0xA4, 0x04, 0x00, 0x0B, 0xA0, 0x00, 0x00, 0x00, 0x18, 0x0F, 0x00, 0x00, 0x01, 0x80, 0x01 };
  if (!nfc.inDataExchange(select_flazz, sizeof(select_flazz), response, &responseLength)) return false;
  delay(15);
  if (!isAPDUHandlingSuccess(response, responseLength)) return false;
  
  Serial.println("Detected: BCA Flazz");

  // 2. Select EF 0200
  uint8_t select_ef[] = { 0x00, 0xA4, 0x01, 0x00, 0x02, 0x02, 0x00 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(select_ef, sizeof(select_ef), response, &responseLength)) return false;
  delay(15);
  if (!isAPDUHandlingSuccess(response, responseLength)) return false;

  // 3. Read EF 81 (Contains Card Number)
  uint8_t read_ef81[] = { 0x00, 0xB0, 0x81, 0x00, 0x8E };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_ef81, sizeof(read_ef81), response, &responseLength)) return false;
  delay(15);
  if (!isAPDUHandlingSuccess(response, responseLength)) return false;
  
  // Card number starts at byte offset 104 (0x68) for 16 bytes
  String cardNum = "";
  for(int i = 104; i < 120; i++) {
     if (response[i] < 0x10) cardNum += "0";
     cardNum += String(response[i], HEX);
  }
  cardNum.toUpperCase();
  Serial.println("Card Number: " + cardNum.substring(0, 16));

  // 4. Read EF 87 (Prep command)
  uint8_t read_ef87[] = { 0x00, 0xB0, 0x87, 0x00, 0x46 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_ef87, sizeof(read_ef87), response, &responseLength)) return false;
  delay(15);
  if (!isAPDUHandlingSuccess(response, responseLength)) return false;

  // 5. Read Balance
  uint8_t read_bal[] = { 0x80, 0x32, 0x00, 0x03, 0x04, 0x00, 0x00, 0x00, 0x00 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_bal, sizeof(read_bal), response, &responseLength)) return false;
  delay(15);
  if (!isAPDUHandlingSuccess(response, responseLength)) return false;

  // Balance is big-endian starting at byte index 1
  uint32_t balance = ((uint32_t)response[1] << 16) | ((uint32_t)response[2] << 8) | response[3];
  Serial.println("Balance: " + String(balance));
  
  // Display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("BCA FLAZZ");
  display.setCursor(0, 10);
  display.println(formatCardNumber(cardNum.substring(0, 16)));
  display.setCursor(0, 22);
  display.println(formatRupiah(balance));
  display.display();

  return true;
}

void loop() {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;
  
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 250);
  
  if (success) {
    Serial.print("Card detected! UID: ");
    for (uint8_t i = 0; i < uidLength; i++) {
      Serial.print(" 0x"); Serial.print(uid[i], HEX);
    }
    Serial.println();
    
    uint8_t buffLen;
    uint8_t* pBuf = nfc.getBuffer(&buffLen);
    uint8_t sak = pBuf[4]; // In readPassiveTargetID, the SAK is at index 4 of the internal buffer
    Serial.printf("SAK: 0x%02X\n", sak);
    
    if (sak == 0x08 || sak == 0x18) {
        Serial.println("WARNING: Card is Mifare Classic, which doesn't support APDU commands!");
    } else if (sak == 0x20 || sak == 0x28 || sak == 0x38) {
        Serial.println("Card is ISO-DEP (DESFire/Java Card). Proceeding with APDUs...");
    }

    showMessage("Processing...", "Keep card close");

    // Critical Timing: 40-50ms delay for Java Card boot-up
    delay(50);

    // Try detecting each supported card type
    if (checkFlazz()) {
      delay(4000);
    } else if (checkEMoney()) {
      delay(4000);
    } else if (checkTapCash()) {
      delay(4000);
    } else if (checkBrizzi()) {
      delay(4000);
    } else {
      Serial.println("Unsupported card type.");
      showMessage("Unsupported Card", "Cannot read data");
      delay(2000);
    }
    
    // Reset to ready screen
    showMessage("Reader Ready", "Scan card now...");
    
    // Clean up RF state so the card can be read again
    nfc.inRelease();
    nfc.setRFField(0, 0);
  }
  
  delay(200);
}
