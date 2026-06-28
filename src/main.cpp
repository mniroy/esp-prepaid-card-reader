#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <PN532_SPI.h>
#include <PN532.h>

// ── ST7735 Pin Definitions (D1 Mini) ──────────────────────────
// Hardware SPI MUST be used since PN532 shares the same pins.
// Software SPI would break the hardware SPI peripheral routing.
#define TFT_CS   4   // D2 (GPIO4)  — TFT Chip Select
#define TFT_DC   5   // D1 (GPIO5)  — TFT Data/Command
#define TFT_RST  2   // D4 (GPIO2)  — TFT Reset (built-in pull-up, boot-safe)

// 3-arg constructor = Hardware SPI (uses D5 for SCK, D7 for MOSI automatically)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// ── PN532 ──────────────────────────────────────────────────────
// CS: D0 (GPIO16) — avoids GPIO15 boot constraints
PN532_SPI pn532spi(SPI, 16);
PN532 nfc(pn532spi);

// ── Colors ────────────────────────────────────────────────────
#define C_BG       ST7735_BLACK
#define C_WHITE    ST7735_WHITE
#define C_YELLOW   ST7735_YELLOW
#define C_CYAN     ST7735_CYAN
#define C_GREEN    ST7735_GREEN
#define C_RED      ST7735_RED
#define C_MAGENTA  ST7735_MAGENTA
#define C_ORANGE   0xFD20u

// ── Helpers ───────────────────────────────────────────────────
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

void drawHeader(const char* title, uint16_t color) {
  tft.fillScreen(C_BG);
  tft.fillRoundRect(0, 0, 128, 28, 4, color);
  tft.setTextColor(ST7735_BLACK);
  tft.setTextSize(2);
  int16_t tx = (128 - (int16_t)strlen(title) * 12) / 2;
  if (tx < 2) tx = 2;
  tft.setCursor(tx, 6);
  tft.print(title);
}

void showMessage(const char* line1, const char* line2) {
  tft.fillScreen(C_BG);
  tft.setTextColor(C_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(4, 60);
  tft.println(line1);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(1);
  tft.setCursor(4, 90);
  tft.println(line2);
}

void showCardInfo(const char* cardType, uint16_t headerColor,
                  const String& cardNum, uint32_t balance) {
  drawHeader(cardType, headerColor);

  tft.setTextColor(C_CYAN);
  tft.setTextSize(1);
  tft.setCursor(4, 36);
  tft.print("CARD NO:");

  String formatted = formatCardNumber(cardNum);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(1);
  tft.setCursor(4, 48);
  tft.print(formatted.substring(0, 9));
  tft.setCursor(4, 60);
  tft.print(formatted.substring(9));

  tft.drawFastHLine(0, 76, 128, C_CYAN);

  tft.setTextColor(C_CYAN);
  tft.setTextSize(1);
  tft.setCursor(4, 82);
  tft.print("BALANCE:");

  String bal = formatRupiah(balance);
  tft.setTextColor(C_GREEN);
  if (bal.length() > 9) {
    tft.setTextSize(1);
    tft.setCursor(4, 96);
  } else {
    tft.setTextSize(2);
    tft.setCursor(4, 94);
  }
  tft.print(bal);
}

bool isAPDUHandlingSuccess(const uint8_t* response, uint8_t len) {
  if (len < 2) return false;
  uint8_t sw1 = response[len - 2];
  uint8_t sw2 = response[len - 1];
  return (sw1 == 0x90 || sw1 == 0x91) && sw2 == 0x00;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nPrepaid Card Reader Starting...");

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(C_BG);

  showMessage("Prepaid", "Reader Starting...");
  delay(800);

  SPI.begin();
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 not found");
    showMessage("HW Error", "PN532 not found");
    while (1) { delay(10); }
  }

  nfc.SAMConfig();
  nfc.setPassiveActivationRetries(0x10);

  Serial.println("Ready to scan cards.");
  showMessage("Reader Ready", "Scan card now...");
}

bool checkEMoney() {
  uint8_t response[250];
  uint8_t responseLength = sizeof(response);

  uint8_t select_emoney[] = { 0x00, 0xA4, 0x04, 0x00, 0x08,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
  if (!nfc.inDataExchange(select_emoney, sizeof(select_emoney), response, &responseLength)) return false;
  delay(15);
  if (!isAPDUHandlingSuccess(response, responseLength)) return false;
  Serial.println("Detected: Mandiri E-Money");

  uint8_t read_number[] = { 0x00, 0xB3, 0x00, 0x00, 0x3F };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_number, sizeof(read_number), response, &responseLength)) return false;
  delay(15);
  if (responseLength < 10) return false;

  String cardNum = "";
  for (int i = 0; i < 8; i++) {
    if (response[i] < 0x10) cardNum += "0";
    cardNum += String(response[i], HEX);
  }
  cardNum.toUpperCase();
  Serial.println("Card Number: " + cardNum);

  uint8_t read_balance[] = { 0x00, 0xB5, 0x00, 0x00, 0x0A };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_balance, sizeof(read_balance), response, &responseLength)) return false;
  delay(15);
  if (responseLength < 6) return false;

  uint32_t balance = ((uint32_t)response[3] << 24) |
                     ((uint32_t)response[2] << 16) |
                     ((uint32_t)response[1] << 8)  |
                     response[0];
  Serial.println("Balance: " + String(balance));

  showCardInfo("E-MONEY", C_ORANGE, cardNum, balance);
  return true;
}

bool checkTapCash() {
  uint8_t response[250];
  uint8_t responseLength = sizeof(response);

  uint8_t select_tapcash_iso[] = { 0x00, 0xA4, 0x04, 0x00, 0x08,
                                    0xA0, 0x00, 0x42, 0x4E, 0x49, 0x10, 0x00, 0x01 };
  bool selected = false;

  if (nfc.inDataExchange(select_tapcash_iso, sizeof(select_tapcash_iso), response, &responseLength)
      && isAPDUHandlingSuccess(response, responseLength)) {
    selected = true;
  } else {
    uint8_t select_native_99[] = { 0x90, 0x5A, 0x00, 0x00, 0x03, 0x99, 0x99, 0x99, 0x00 };
    responseLength = sizeof(response);
    if (nfc.inDataExchange(select_native_99, sizeof(select_native_99), response, &responseLength)
        && (responseLength >= 1 && response[responseLength-1] == 0x00)) {
      selected = true;
    } else {
      uint8_t select_native_10[] = { 0x90, 0x5A, 0x00, 0x00, 0x03, 0x10, 0x00, 0x01, 0x00 };
      responseLength = sizeof(response);
      if (nfc.inDataExchange(select_native_10, sizeof(select_native_10), response, &responseLength)
          && (responseLength >= 1 && response[responseLength-1] == 0x00)) {
        selected = true;
      }
    }
  }

  if (!selected) return false;
  Serial.println("Detected: BNI TapCash");

  uint8_t read_tapcash[] = { 0x90, 0x32, 0x03, 0x00, 0x00 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_tapcash, sizeof(read_tapcash), response, &responseLength)) return false;
  delay(15);
  if (responseLength < 16) return false;

  uint32_t balance = ((uint32_t)response[2] << 16) |
                     ((uint32_t)response[3] << 8)  |
                     response[4];

  String cardNum = "";
  for (int i = 8; i < 16; i++) {
    if (response[i] < 0x10) cardNum += "0";
    cardNum += String(response[i], HEX);
  }
  cardNum.toUpperCase();
  Serial.println("Card: " + cardNum + " Bal: " + String(balance));

  showCardInfo("TAPCASH", C_MAGENTA, cardNum, balance);
  return true;
}

bool checkBrizzi() {
  uint8_t response[250];
  uint8_t responseLength = sizeof(response);

  uint8_t select_brizzi[] = { 0x90, 0x5A, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00 };
  if (!nfc.inDataExchange(select_brizzi, sizeof(select_brizzi), response, &responseLength)) return false;
  delay(15);
  if (responseLength < 1 || response[responseLength - 1] != 0x00) return false;
  Serial.println("Detected: BRI Brizzi");

  uint8_t read_brizzi_info[] = { 0x90, 0xBD, 0x00, 0x00, 0x07,
                                  0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_brizzi_info, sizeof(read_brizzi_info), response, &responseLength)) return false;
  delay(15);
  if (responseLength < 12) return false;

  String cardNum = "";
  for (int i = 3; i < 11; i++) {
    if (response[i] < 0x10) cardNum += "0";
    cardNum += String(response[i], HEX);
  }
  cardNum.toUpperCase();
  Serial.println("Card Number: " + cardNum);

  uint8_t brizzi_select_bal[] = { 0x90, 0xBD, 0x00, 0x00, 0x07,
                                   0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(brizzi_select_bal, sizeof(brizzi_select_bal), response, &responseLength)) return false;
  delay(15);

  uint8_t brizzi_read_bal[] = { 0x90, 0x6C, 0x00, 0x00, 0x01, 0x00, 0x00 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(brizzi_read_bal, sizeof(brizzi_read_bal), response, &responseLength)) return false;
  delay(15);
  if (responseLength < 5) return false;

  uint32_t balance = ((uint32_t)response[3] << 24) |
                     ((uint32_t)response[2] << 16) |
                     ((uint32_t)response[1] << 8)  |
                     response[0];
  Serial.println("Balance: " + String(balance));

  showCardInfo("BRIZZI", C_CYAN, cardNum, balance);
  return true;
}

bool checkFlazz() {
  uint8_t response[250];
  uint8_t responseLength = sizeof(response);

  uint8_t select_flazz[] = { 0x00, 0xA4, 0x04, 0x00, 0x0B,
                               0xA0, 0x00, 0x00, 0x00, 0x18, 0x0F, 0x00, 0x00, 0x01, 0x80, 0x01 };
  if (!nfc.inDataExchange(select_flazz, sizeof(select_flazz), response, &responseLength)) return false;
  delay(15);
  if (!isAPDUHandlingSuccess(response, responseLength)) return false;
  Serial.println("Detected: BCA Flazz");

  uint8_t select_ef[] = { 0x00, 0xA4, 0x01, 0x00, 0x02, 0x02, 0x00 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(select_ef, sizeof(select_ef), response, &responseLength)) return false;
  delay(15);
  if (!isAPDUHandlingSuccess(response, responseLength)) return false;

  uint8_t read_ef81[] = { 0x00, 0xB0, 0x81, 0x00, 0x8E };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_ef81, sizeof(read_ef81), response, &responseLength)) return false;
  delay(15);
  if (!isAPDUHandlingSuccess(response, responseLength)) return false;

  String cardNum = "";
  for (int i = 104; i < 120; i++) {
    if (response[i] < 0x10) cardNum += "0";
    cardNum += String(response[i], HEX);
  }
  cardNum.toUpperCase();
  Serial.println("Card Number: " + cardNum.substring(0, 16));

  uint8_t read_ef87[] = { 0x00, 0xB0, 0x87, 0x00, 0x46 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_ef87, sizeof(read_ef87), response, &responseLength)) return false;
  delay(15);
  if (!isAPDUHandlingSuccess(response, responseLength)) return false;

  uint8_t read_bal[] = { 0x80, 0x32, 0x00, 0x03, 0x04, 0x00, 0x00, 0x00, 0x00 };
  responseLength = sizeof(response);
  if (!nfc.inDataExchange(read_bal, sizeof(read_bal), response, &responseLength)) return false;
  delay(15);
  if (!isAPDUHandlingSuccess(response, responseLength)) return false;

  uint32_t balance = ((uint32_t)response[1] << 16) | ((uint32_t)response[2] << 8) | response[3];
  Serial.println("Balance: " + String(balance));

  showCardInfo("BCA FLAZZ", C_YELLOW, cardNum.substring(0, 16), balance);
  return true;
}

void loop() {
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;

  uint8_t success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 250);

  if (success) {
    Serial.print("Card detected! UID:");
    for (uint8_t i = 0; i < uidLength; i++) {
      Serial.print(" 0x"); Serial.print(uid[i], HEX);
    }
    Serial.println();

    uint8_t buffLen;
    uint8_t* pBuf = nfc.getBuffer(&buffLen);
    uint8_t sak = pBuf[4];
    Serial.printf("SAK: 0x%02X\n", sak);

    if (sak == 0x08 || sak == 0x18) {
      Serial.println("WARNING: Mifare Classic — no APDU support");
    } else if (sak == 0x20 || sak == 0x28 || sak == 0x38) {
      Serial.println("ISO-DEP card — proceeding with APDUs...");
    }

    showMessage("Processing...", "Keep card close");
    delay(50);

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
      showMessage("Unsupported", "Cannot read card");
      delay(2000);
    }

    showMessage("Reader Ready", "Scan card now...");

    nfc.inRelease();
    nfc.setRFField(0, 0);
  }

  delay(200);
}
