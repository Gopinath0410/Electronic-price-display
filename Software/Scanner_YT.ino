#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "EspUsbHostKeybord.h"  // USB HID keyboard for ESP32-S3
#include "logo.h"               // Optional logo for splash screen

// ========================== Display Configuration ==========================
#define TFT_CS   10
#define TFT_DC   21
#define TFT_RST  47
#define TFT_SCK  12
#define TFT_MOSI 11
#define TFT_MISO -1

SPIClass spi(FSPI);  // FSPI for ESP32-S3
Adafruit_ST7789 tft = Adafruit_ST7789(&spi, TFT_CS, TFT_DC, TFT_RST);

// ========================== WiFi Configuration =============================
const char* ssid = "your ssid";
const char* password = "your password";
const char* firebase_host = "your firebase link";

// ========================== State Tracking ================================
String qrBuffer = "";
bool showingProduct = false;
unsigned long productShownAt = 0;
bool deviceConnected = false;
int dotCount = 0;

// ========================== UI Functions ===================================
void drawHeader() {
  tft.fillRect(0, 0, 320, 30, ST77XX_BLUE); // Top bar
  tft.setCursor(100, 5);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.print("G-Shop");
}

void showConnectingAnimation() {
  tft.fillRect(0, 40, 320, 30, ST77XX_BLACK);
  tft.setCursor(10, 50);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.print("Connecting to WiFi");

  for (int i = 0; i < dotCount; i++) tft.print(".");
  dotCount = (dotCount + 1) % 4;
  delay(500);
}

void showPrompt() {
  tft.fillRect(0, 40, 320, 30, ST77XX_BLACK);
  tft.setCursor(10, 40);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.println("Ready to scan QR:");

  tft.fillRect(0, 70, 320, 180, ST77XX_BLACK);
}

void showLoading() {
  tft.fillRect(0, 70, 320, 80, ST77XX_BLACK);
  tft.setCursor(10, 80);
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_YELLOW);
  tft.println("Loading...");
}

void showProductInfo(String product, int price) {
  tft.fillRect(0, 70, 320, 150, ST77XX_BLACK);
  tft.setCursor(10, 80);
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_GREEN);
  tft.print("Item: ");
  tft.println(product);

  tft.setCursor(10, 130);
  tft.setTextColor(ST77XX_CYAN);
  tft.print("Price: Rs ");
  tft.println(price);
}

void showNotFound() {
  tft.fillRect(0, 70, 320, 80, ST77XX_BLACK);
  tft.setCursor(10, 80);
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_RED);
  tft.println("Not Found");
}

void showNotFound(String barcode) {
  tft.fillRect(0, 70, 240, 150, ST77XX_BLACK);
  tft.setCursor(10, 80);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_RED);
  tft.println("Not Found");

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 120);
  tft.setTextSize(1);
  tft.print("Barcode: ");
  tft.println(barcode);
  tft.setCursor(10, 140);
  tft.println("Product not in list");
}

// ========================== Firebase Fetch ================================
void fetchProductInfo(String barcode) {
  String url = String(firebase_host) + "/products/" + barcode + ".json";
  HTTPClient http;
  http.begin(url);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Product info:");
    Serial.println(payload);

    // Handle null response explicitly
    if (payload == "null") {
      showNotFound(barcode);  // ⬅️ Pass barcode for display
      showingProduct = true;
      productShownAt = millis();
      http.end();
      return;
    }

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      String product = doc["product"] | "";
      int price = doc["price"] | 0;
      showProductInfo(product, price);
      showingProduct = true;
      productShownAt = millis();
    } else {
      Serial.println("Failed to parse JSON");
      showNotFound(barcode);
      showingProduct = true;
      productShownAt = millis();
    }
  } else {
    Serial.println("Product not found or HTTP error");
    showNotFound(barcode);
    showingProduct = true;
    productShownAt = millis();
  }

  http.end();
}


// ========================== USB HID Input Handler ==========================
class MyEspUsbHostKeybord : public EspUsbHostKeybord {
public:
  void onKey(usb_transfer_t *transfer) override {
    uint8_t *const p = transfer->data_buffer;
    bool shift = (p[0] & 0x22) != 0;

    for (int i = 2; i < 8; i++) {
      uint8_t key = p[i];
      if (key == 0) continue;

      char c = hidToAscii(key, shift);
      if (key == 40) {  // Enter key
        Serial.println("\nQR Code Scanned:");
        Serial.println(qrBuffer);
        showLoading();
        fetchProductInfo(qrBuffer);
        qrBuffer = "";
      } else if (c) {
        qrBuffer += c;
      }
    }
  }

  char hidToAscii(uint8_t keycode, bool shift) {
    const char ascii[] = {
      0, 0, 0, 0, 'a','b','c','d','e','f','g','h','i','j','k','l',
      'm','n','o','p','q','r','s','t','u','v','w','x','y','z',
      '1','2','3','4','5','6','7','8','9','0',
      '\n', 0, 0, '\t', ' ', '-', '=', '[', ']', '\\',
      '#',';', '\'', '`', ',', '.', '/', 0, 0, 0, 0, 0, 0, 0, 0
    };

    const char asciiShift[] = {
      0, 0, 0, 0, 'A','B','C','D','E','F','G','H','I','J','K','L',
      'M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
      '!','@','#','$','%','^','&','*','(',')',
      '\n', 0, 0, '\t', ' ', '_', '+', '{', '}', '|',
      '~',':','"','~','<','>','?', 0, 0, 0, 0, 0, 0, 0, 0
    };

    if (keycode >= sizeof(ascii)) return 0;
    return shift ? asciiShift[keycode] : ascii[keycode];
  }
};

MyEspUsbHostKeybord usbHost;

// ========================== Setup ===============================
void setup() {
  Serial.begin(115200);
  usbHost.begin();

  // Initialize display
  spi.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.init(240, 320);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  delay(100);
  tft.drawRGBBitmap(40, 0, epd_bitmap_3D_img, 240, 240);  // Optional logo
  delay(1000);
  tft.fillScreen(ST77XX_BLACK);

  drawHeader();

  // WiFi connect with animation
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    showConnectingAnimation();
  }

  tft.fillRect(0, 40, 320, 30, ST77XX_BLACK);
  tft.setCursor(10, 40);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_GREEN);
  tft.println("Connected!");
  delay(1000);

  showPrompt();

  Serial.println("\nConnected to WiFi!");
  Serial.println("Ready to scan QR:");
}

// ========================== Main Loop ===============================
void loop() {
  usbHost.task();

  bool currentlyConnected = usbHost.isReady;  // ✅ fixed

  if (currentlyConnected && !deviceConnected) {
    Serial.println("USB HID device connected.");
    showPrompt();
    deviceConnected = true;
  } else if (!currentlyConnected && deviceConnected) {
    Serial.println("USB HID device disconnected.");
    tft.fillScreen(ST77XX_BLACK);
    drawHeader();
    tft.setCursor(10, 50);
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(2);
    tft.println("USB not connected");
    deviceConnected = false;
  }

  if (showingProduct && millis() - productShownAt > 3000) {
    showPrompt();
    showingProduct = false;
  }
}
