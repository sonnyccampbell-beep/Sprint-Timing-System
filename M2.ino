// Created by Sonny Campbell
// Last modified 06/2026 
// Module 2: Gate A (Split / Start Gate with LCD) 
#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LiquidCrystal_I2C.h>

#define S2 23 // IR sensor input pin (Active LOW)

LiquidCrystal_I2C lcd(0x27, 16, 2); 

uint8_t GATE_B_MAC[] = {0x20, 0xE7, 0xC8, 0x9E, 0xB7, 0x18}; 

enum SystemState { STATE_DISCONNECTED, STATE_ARMED, STATE_TIMING, STATE_RESULT };
SystemState currentState = STATE_DISCONNECTED;

bool currentModeIsFlying = false; 
unsigned long startTime = 0;

typedef struct struct_message { char a[12]; } struct_message;
struct_message myData;

volatile bool newMsg = false;
char message[12] = {0};
portMUX_TYPE msgMux = portMUX_INITIALIZER_UNLOCKED; // Shared resource guard

unsigned long lastArmedRxMs = 0;
const unsigned long OUT_OF_RANGE_MS = 2000;
bool outOfRangeShown = false;

const unsigned long IR_DEBOUNCE_MS = 30;
const unsigned long IR_LOCKOUT_MS  = 1000;
unsigned long lastIrChangeMs = 0;
unsigned long lastIrTriggerMs = 0;
int lastIrRead = HIGH;

// UI Cache Tracking to prevent LCD Flicker
String currentL1 = "";
String currentL2 = "";

static void lcdShow(const char* line1, const char* line2) {
  if (currentL1 == line1 && currentL2 == (line2 ? line2 : "")) return;
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1);
  currentL1 = line1;
  if (line2) { 
    lcd.setCursor(0, 1); lcd.print(line2); 
    currentL2 = line2;
  } else {
    currentL2 = "";
  }
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
  portENTER_CRITICAL_ISR(&msgMux);
  strncpy(message, myData.a, sizeof(message));
  message[sizeof(message) - 1] = '\0';
  newMsg = true;
  portEXIT_CRITICAL_ISR(&msgMux);
}
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  myData.a[sizeof(myData.a) - 1] = '\0';
  portENTER_CRITICAL_ISR(&msgMux);
  strncpy(message, myData.a, sizeof(message));
  message[sizeof(message) - 1] = '\0';
  newMsg = true;
  portEXIT_CRITICAL_ISR(&msgMux);
}
#endif

void setup() {
  Serial.begin(115200);
  pinMode(S2, INPUT_PULLUP);
  
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) { while (true) delay(1000); }
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, GATE_B_MAC, 6);
  p.channel = 0; p.encrypt = false;
  esp_now_add_peer(&p);

  lcd.init();
  lcd.backlight();
  lcdShow("Gate A Init...", "Split Module");
  
  lastIrRead = digitalRead(S2);
  lastIrChangeMs = millis();
}

void loop() {
  unsigned long now = millis();

  // Process Radio Messages - Even if showing results, allowing instant system resets
  if (newMsg) {
    char msgLocal[12];
    portENTER_CRITICAL(&msgMux);
    strncpy(msgLocal, message, sizeof(msgLocal));
    newMsg = false;
    portEXIT_CRITICAL(&msgMux);
    msgLocal[sizeof(msgLocal) - 1] = '\0';

    // 1. Flying Run Heartbeat
    if (strcmp(msgLocal, "Fly_Armed") == 0) {
      lastArmedRxMs = now;
      currentModeIsFlying = true;
      if (currentState != STATE_RESULT && currentState != STATE_TIMING) {
        currentState = STATE_ARMED;
        outOfRangeShown = false;
        lcdShow("Mode: Flying Run", "Ready at Gate A");
      }
    }
    // 2. Track Reset from M1
    else if (strcmp(msgLocal, "Reset_Fly") == 0) {
      outOfRangeShown = false;
      currentModeIsFlying = true;
      currentState = STATE_ARMED;
      lcdShow("Track Reset", "Ready for Run");
    }
    // 3. Start Gun Heartbeat
    else if (strcmp(msgLocal, "Armed") == 0) {
      lastArmedRxMs = now;
      currentModeIsFlying = false;
      if (currentState != STATE_RESULT && currentState != STATE_TIMING) {
        currentState = STATE_ARMED;
        outOfRangeShown = false;
        lcdShow("Mode: Start Gun", "Awaiting Gun...");
      }
    }
    // 4. Start Gun Fire Signal
    else if (strcmp(msgLocal, "Interrupted") == 0) {
      currentModeIsFlying = false;
      outOfRangeShown = false;
      currentState = STATE_TIMING;
      startTime = now; 
      lcdShow("Timing Split...", "Run Active");
    }
  }

  // --- Beam Break Core Handling Logic ---
  bool beamTripped = irTriggeredDebounced();

  if (beamTripped) {
    // FIX Case A: Flying Run trigger event (Trips while armed before local timing begins)
    if (currentModeIsFlying && currentState == STATE_ARMED) {
      currentState = STATE_RESULT;
      
      strncpy(myData.a, "Start_Timer", sizeof(myData.a));
      esp_now_send(GATE_B_MAC, (uint8_t*)&myData, sizeof(myData));
      
      lcdShow("Run Started!", "Athlete Past A");
    } 
    // FIX Case B: Start Gun split capture (Trips mid-run while timing is active)
    else if (!currentModeIsFlying && currentState == STATE_TIMING) {
      currentState = STATE_RESULT;
      
      unsigned long elapsedMs = now - startTime;
      float seconds = elapsedMs * 0.001f;
      char buffer[12];
      snprintf(buffer, sizeof(buffer), "%.3f", seconds);

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Split Time: ");
      lcd.setCursor(0, 1); lcd.print(buffer); lcd.print("s");
      
      currentL1 = "Split Time: ";
      currentL2 = String(buffer) + "s";
    }
  }

  // Range Check Validation Loop
  bool inRange = (lastArmedRxMs != 0) && ((now - lastArmedRxMs) <= OUT_OF_RANGE_MS);
  if (currentState != STATE_TIMING && currentState != STATE_RESULT && !inRange) {
    currentState = STATE_DISCONNECTED;
    if (!outOfRangeShown) {
      outOfRangeShown = true;
      lcdShow("  Out of range  ", "Check M1 Link...");
    }
  }
  delay(1); 
}