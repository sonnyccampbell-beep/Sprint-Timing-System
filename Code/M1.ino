// Created by Sonny Campbell
// Last modified 06/2026 
// Module 1: Master Controller (M1)

#include <WiFi.h>
#include <esp_now.h>

#define BUTTON_1     5
#define BUZZER_PIN 21

// Receiver MAC Addresses
uint8_t RX1_MAC[] = {0xD4, 0xE9, 0xF4, 0xC4, 0xA9, 0x10}; // Gate A (Start Gate)
uint8_t RX2_MAC[] = {0x20, 0xE7, 0xC8, 0x9E, 0xB7, 0x18}; // Gate B (Finish Gate + LCD)

typedef struct struct_message { char a[12]; } struct_message;
struct_message myData;

// State Variables
bool armed = true;
bool flyingRunMode = false;       // false = Start Gun Mode, true = Flying Run Mode
unsigned long btnPressedTime = 0; 
bool longPressTriggered = false;   
const unsigned long LONG_PRESS_DURATION = 1500; 

// Non-blocking Audio Notification Variables
unsigned long buzzerSequenceStart = 0;
bool buzzerSequenceActive = false;
int buzzerStep = 0;

// State Machine for Start Gun Countdown
enum StartState { IDLE, WAIT_PREP, BEEP1_ON, WAIT_RANDOM, BEEP2_ON, DONE_SEND };
StartState st = IDLE;

unsigned long t0 = 0;
unsigned long waitRandomMs = 0;

#if ESP_IDF_VERSION_MAJOR >= 5
void OnDataSent(const wifi_tx_info_t*, esp_now_send_status_t status) {
  // Production safe hook
}
#else
void OnDataSent(const uint8_t*, esp_now_send_status_t status) {
  // Production safe hook
}
#endif

static bool addPeer(const uint8_t *mac) {
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = 0;
  p.encrypt = false;
  esp_err_t e = esp_now_add_peer(&p);
  return (e == ESP_OK || e == ESP_ERR_ESPNOW_EXIST);
}

static void sendToBoth(const char *txt) {
  strncpy(myData.a, txt, sizeof(myData.a));
  myData.a[sizeof(myData.a) - 1] = '\0';
  
  esp_err_t err1 = esp_now_send(RX1_MAC, (uint8_t*)&myData, sizeof(myData));
  esp_err_t err2 = esp_now_send(RX2_MAC, (uint8_t*)&myData, sizeof(myData));
  
  if (err1 != ESP_OK || err2 != ESP_OK) {
    Serial.printf("Tx Warning: [A: 0x%X | B: 0x%X]\n", err1, err2);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) { while (true) delay(1000); }
  esp_now_register_send_cb(OnDataSent);

  addPeer(RX1_MAC);
  addPeer(RX2_MAC);

  randomSeed((uint32_t)esp_random());
  
  tone(BUZZER_PIN, 2000, 200);
  Serial.println("M1 System Initialized: Start Gun Active");
}

void loop() {
  unsigned long now = millis();

  // --- 1. Background Heartbeat Matrix ---
  static unsigned long lastHeartbeat = 0;
  if (now - lastHeartbeat >= 500) {
    lastHeartbeat = now;
    if (flyingRunMode) {
      sendToBoth("Fly_Armed"); 
    } else {
      if (armed) sendToBoth("Armed");
    }
  }

  // --- 2. Asynchronous Audio Notification Handler (Removes Blocking Delay) ---
  if (buzzerSequenceActive) {
    unsigned long elapsed = now - buzzerSequenceStart;
    if (buzzerStep == 1 && elapsed >= 80) {
      noTone(BUZZER_PIN);
      buzzerStep = 2;
    } 
    else if (buzzerStep == 2 && elapsed >= 180) {
      tone(BUZZER_PIN, 4000, 80);
      buzzerStep = 3;
      buzzerSequenceActive = false; // Sequence Complete
    }
  }

  // --- 3. Long Press / Short Press Button Logic ---
  static int lastBtnState = HIGH;
  int btnState = digitalRead(BUTTON_1);

  if (lastBtnState == HIGH && btnState == LOW) {
    btnPressedTime = now;
    longPressTriggered = false;
  }

  if (btnState == LOW && !longPressTriggered) {
    if (now - btnPressedTime >= LONG_PRESS_DURATION) {
      flyingRunMode = !flyingRunMode; 
      longPressTriggered = true;
      
      if (flyingRunMode) {
        // Trigger asynchronous dual-chirp sequence
        tone(BUZZER_PIN, 4000, 80);
        buzzerSequenceStart = now;
        buzzerStep = 1;
        buzzerSequenceActive = true;
        Serial.println("MODE CHANGE: FLYING RUN");
      } else {
        // Single sustained low tone (no async step tracking needed)
        tone(BUZZER_PIN, 2000, 400);
        Serial.println("MODE CHANGE: START GUN");
        st = IDLE; 
      }
    }
  }

  if (lastBtnState == LOW && btnState == HIGH) {
    if (!longPressTriggered) {
      if (flyingRunMode) {
        sendToBoth("Reset_Fly");
        tone(BUZZER_PIN, 3000, 100); 
        Serial.println("Sent Command: Reset_Fly");
      } else {
        if (armed && st == IDLE) {
          st = WAIT_PREP;
          t0 = now;
          Serial.println("Countdown Sequence Engaged...");
        }
      }
    }
  }
  lastBtnState = btnState;

  // --- 4. Non-Blocking Start Gun State Machine ---
  if (!flyingRunMode) {
    switch (st) {
      case IDLE:
        break;

      case WAIT_PREP:
        if (now - t0 >= 8000) { // Athlete preparation window (8 seconds)
          tone(BUZZER_PIN, 3100);
          st = BEEP1_ON;
          t0 = now;
        }
        break;

      case BEEP1_ON:
        if (now - t0 >= 100) {
          noTone(BUZZER_PIN);
          waitRandomMs = (unsigned long)random(2500, 3001); // 2.5s - 3.0s variation
          st = WAIT_RANDOM;
          t0 = now;
        }
        break;

      case WAIT_RANDOM:
        if (now - t0 >= waitRandomMs) {
          tone(BUZZER_PIN, 3400);
          st = BEEP2_ON;
          t0 = now;
        }
        break;

      case BEEP2_ON:
        if (now - t0 >= 100) {
          noTone(BUZZER_PIN);
          st = DONE_SEND;
        }
        break;

      case DONE_SEND:
        sendToBoth("Interrupted");
        st = IDLE;
        break;
    }
  }

  delay(1); // Consolidated down to 1ms cycle for highly fluid state machine iteration
}