// Created by Sonny Campbell
// Last modified 06/2026 
// Module 3: Gate B (Finish Gate with LCD)

#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LiquidCrystal_I2C.h>

#define S2 23  // IR sensor input pin (Active LOW)

LiquidCrystal_I2C lcd(0x27, 16, 2); 

bool armed = false;
bool timing = false;
bool showingResult = false;   
unsigned long startTime = 0;

typedef struct struct_message { char a[12]; } struct_message;
struct_message myData;

volatile bool newMsg = false;
char message[12] = {0};

unsigned long lastArmedRxMs = 0;
const unsigned long OUT_OF_RANGE_MS = 2000;  
bool outOfRangeShown = false;

const unsigned long IR_DEBOUNCE_MS = 30;
const unsigned long IR_LOCKOUT_MS  = 1000;
unsigned long lastIrChangeMs = 0;
unsigned long lastIrTriggerMs = 0;
int lastIrRead = HIGH; 

static void lcdShow(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1);
  if (line2) { lcd.setCursor(0, 1); lcd.print(line2); }
}

bool irTriggeredDebounced() {
  int r = digitalRead(S2);
  unsigned long now = millis();
  if (r != lastIrRead) { lastIrRead = r; lastIrChangeMs = now; }
  if ((now - lastIrChangeMs) < IR_DEBOUNCE_MS) return false;
  if (r == LOW && (now - lastIrTriggerMs) > IR_LOCKOUT_MS) {
    lastIrTriggerMs = now;
    return true;
  }
  return false;
}

#if ESP_IDF_VERSION_MAJOR >= 5
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  myData.a[sizeof(myData.a) - 1] = '\0';
  strncpy(message, myData.a, sizeof(message));
  message[sizeof(message) - 1] = '\0';
  newMsg = true;
}
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  myData.a[sizeof(myData.a) - 1] = '\0';
  strncpy(message, myData.a, sizeof(message));
  message[sizeof(message) - 1] = '\0';
  newMsg = true;
}
#endif

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) { while (true) delay(1000); }
  esp_now_register_recv_cb(OnDataRecv);

  lcd.init();
  lcd.backlight();
  lcdShow("Gate B Initialized", "Finish Module");

  pinMode(S2, INPUT_PULLUP);
  lastIrRead = digitalRead(S2);
  lastIrChangeMs = millis();
}

void loop() {
  unsigned long now = millis();

  if (newMsg) {
    char msgLocal[12];
    strncpy(msgLocal, message, sizeof(msgLocal));
    msgLocal[sizeof(msgLocal) - 1] = '\0';
    newMsg = false;

    // 1. Flying Run Mode Heartbeat from M1
    if (strcmp(msgLocal, "Fly_Armed") == 0) {
      lastArmedRxMs = now;
      if (!showingResult && !timing) {
        armed = true; outOfRangeShown = false;
        lcdShow("Mode: Flying Run", "Awaiting Gate A");
      }
    }
    // 2. Track Reset from M1
    else if (strcmp(msgLocal, "Reset_Fly") == 0) {
      showingResult = false; timing = false; outOfRangeShown = false;
      lcdShow("Track Reset", "Ready for Run");
    }
    // 3. Remote Trigger from Gate A (Sprinting Start)
    else if (strcmp(msgLocal, "Start_Timer") == 0) {
      if (!timing) {
        showingResult = false; outOfRangeShown = false; armed = true; timing = true;
        startTime = now; 
        lcdShow("Timing Run...", "Sprinting!");
      }
    }
    // 4. Start Gun Heartbeat from M1
    else if (strcmp(msgLocal, "Armed") == 0) {
      lastArmedRxMs = now;
      if (!showingResult && !timing) {
        armed = true; outOfRangeShown = false;
        lcdShow("Mode: Start Gun", "Ready to Finish");
      }
    }
    // 5. Start Gun Fire Signal from M1
    else if (strcmp(msgLocal, "Interrupted") == 0) {
      if (showingResult) showingResult = false;
      bool inRange = (lastArmedRxMs != 0) && ((now - lastArmedRxMs) <= OUT_OF_RANGE_MS);
      if (inRange && !timing) {
        armed = true; timing = true;
        startTime = now; // Start tracking total time locally
        lcdShow("Timing Final...", "");
      }
    }
  }

  // Finish Line Sensor Tripped
  if (timing && irTriggeredDebounced()) {
    timing = false;
    armed = false;
    outOfRangeShown = false;

    unsigned long elapsedMs = now - startTime;
    float seconds = elapsedMs * 0.001f;
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%.3f", seconds);

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Final Time: ");
    lcd.setCursor(0, 1); lcd.print(buffer); lcd.print("s");

    showingResult = true; 
  }

  if (showingResult) { delay(5); return; }

  // Range Check
  bool inRange = (lastArmedRxMs != 0) && ((now - lastArmedRxMs) <= OUT_OF_RANGE_MS);
  if (!timing && !inRange) {
    armed = false;
    if (!outOfRangeShown) {
      outOfRangeShown = true;
      lcdShow("  Out of range  ", "Check M1 Link...");
    }
  }
  delay(5);
}