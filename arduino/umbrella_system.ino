/*
  ==========================================================
  Umbrella System v3.0 Final - TCP Socket Mode
  ==========================================================
  핀 배치
    D2 IR1   | D3 IR2   | D4 IR3
    D5 SV1   | D6 SV2   | D7 SV3
    D8 BUZ   | D9 RRST  | D10 RSS
    D11 MOSI | D12 MISO | D13 SCK
    A0 WATER | A1 WiFi TX | A2 WiFi RX
    A3 MOTOR | A4 SDA   | A5 SCL
  ==========================================================
*/

#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <MsTimer2.h>
#include "WiFiEsp.h"
#include "SoftwareSerial.h"

// ── WiFi / TCP 서버 설정 ─────────────────────────────
#define AP_SSID       "KCCI601"
#define AP_PASS       "@kcci601@"
#define SERVER_NAME   "10.10.16.63"
#define SERVER_PORT   5000

// ID는 서버 ID_SIZE(10) 기준 최대 9자
#define CLIENT_ID     "UMB_ARD"   // 아두이노 로그인 ID
#define DB_CLIENT_ID  "UMB_DB"    // DB 클라이언트 ID

// ── 핀 정의 ──────────────────────────────────────────
#define IR1_PIN      2
#define IR2_PIN      3
#define IR3_PIN      4
#define SERVO1_PIN   5
#define SERVO2_PIN   6
#define SERVO3_PIN   7
#define BUZZER_PIN   8
#define RFID_RST     9
#define RFID_SS      10
#define WATER_PIN    A0
#define WIFI_TX_PIN  A1
#define WIFI_RX_PIN  A2
#define MOTOR_PIN    A3

// ── 상수 ─────────────────────────────────────────────
#define SLOT_COUNT             3
#define SERVO_OPEN_DEG         90
#define SERVO_CLOSE_DEG        0
#define LOCK_DELAY_SEC         10
#define LCD_FULL_SEC           10
#define RETRIEVE_TIMEOUT_SEC   30
#define WATER_SAMPLE_SEC       10
#define SERVO_MOVE_MS          700
#define DRY_SEND_INTERVAL      30
#define TCP_RECV_SIZE          60
#define ARR_CNT                5

// ── 수위 임계값 (히스테리시스) ────────────────────────
#define WATER_HIGH_ON   600
#define WATER_HIGH_OFF  550
#define WATER_MID_ON    350
#define WATER_MID_OFF   250
#define WATER_LOW_ON    150
#define WATER_LOW_OFF   80
#define WATER_DRY       50

// ── 슬롯 상태 ────────────────────────────────────────
typedef enum {
  SLOT_EMPTY,
  SLOT_OPEN,
  SLOT_WAITING,
  SLOT_LOCKED,
  SLOT_RETRIEVING
} SLOT_STATE;

typedef struct {
  SLOT_STATE state;
  char uid[20];
  int  dryPercent;     // -1 = --% 표시
  int  peakWaterVal;   // 10초 샘플링 기준값
  unsigned long stateTimer;
  bool theftDetected;
  bool servoMoving;    // non-blocking detach용
  unsigned long servoTimer;
  unsigned long lastDrySent;
} SLOT_INFO;

// ── 전역 객체 ────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522           rfid(RFID_SS, RFID_RST);
Servo              servos[SLOT_COUNT];
SoftwareSerial     wifiSerial(WIFI_RX_PIN, WIFI_TX_PIN);
WiFiEspClient      client;

// ── 전역 변수 ────────────────────────────────────────
SLOT_INFO slots[SLOT_COUNT];
const int IR_PINS[SLOT_COUNT]    = { IR1_PIN,    IR2_PIN,    IR3_PIN };
const int SERVO_PINS[SLOT_COUNT] = { SERVO1_PIN, SERVO2_PIN, SERVO3_PIN };

volatile bool timerIsrFlag = false;
volatile bool ms100Flag    = false;
unsigned long secCount     = 0;

bool fullDisplay           = false;
unsigned long fullDisplayTimer = 0;

bool theftAlarmActive      = false;
unsigned long theftAlarmTimer = 0;
bool buzzerMuted           = false;

unsigned long motorTimer   = 0;
bool motorPhase            = false;
int motorOnTime            = 0;
int motorOffTime           = 0;
int currentMotorLevel      = 0;

char lcdLine1[17];
char lcdLine2[17];

SLOT_STATE prevSlotState[SLOT_COUNT];
bool prevIR[SLOT_COUNT];
bool wifiConnected = false;

// ==========================================================
void setup() {
  Serial.begin(115200);
  Serial.println(F("===================================="));
  Serial.println(F("  Umbrella System v3.0 Final"));
  Serial.println(F("  TCP Socket Mode"));
  Serial.println(F("===================================="));

  // RFID 초기화
  SPI.begin();
  rfid.PCD_Init();
  rfid.PCD_SetAntennaGain(rfid.RxGain_max);
  Serial.println(F("[RFID]  Init OK"));

  // LCD 초기화
  lcd.init();
  lcd.backlight();
  Serial.println(F("[LCD]   Init OK"));

  // WiFi 초기화
  // INPUT_PULLUP: A2 플로팅 방지 → SPI 간섭 차단 (RFID 인식률 유지)
  pinMode(WIFI_RX_PIN, INPUT_PULLUP);
  pinMode(WIFI_TX_PIN, OUTPUT);
  digitalWrite(WIFI_TX_PIN, HIGH);

  wifiSerial.begin(38400);
  WiFi.init(&wifiSerial);

  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println(F("[WiFi]  Module NOT found -> offline"));
  } else {
    Serial.print(F("[WiFi]  Connecting to "));
    Serial.println(AP_SSID);
    lcdDisplay(0, 0, "WiFi Connecting.");
    lcdDisplay(0, 1, AP_SSID);

    if (WiFi.begin(AP_SSID, AP_PASS) == WL_CONNECTED) {
      wifiConnected = true;
      Serial.println(F("[WiFi]  Connected!"));
      Serial.print(F("[WiFi]  IP = "));
      Serial.println(WiFi.localIP());
    } else {
      Serial.println(F("[WiFi]  FAILED -> offline"));
    }
  }

  // 슬롯 초기화
  for (int i = 0; i < SLOT_COUNT; i++) {
    pinMode(IR_PINS[i], INPUT);

    slots[i].state         = SLOT_EMPTY;
    memset(slots[i].uid, 0, sizeof(slots[i].uid));
    slots[i].dryPercent    = -1;
    slots[i].peakWaterVal  = 0;
    slots[i].stateTimer    = 0;
    slots[i].theftDetected = false;
    slots[i].servoMoving   = false;
    slots[i].servoTimer    = 0;
    slots[i].lastDrySent   = 0;
    prevSlotState[i]       = SLOT_EMPTY;
    prevIR[i]              = false;

    servos[i].attach(SERVO_PINS[i]);
    servos[i].write(SERVO_CLOSE_DEG);
    delay(600);
    servos[i].detach();
    Serial.print(F("[SLOT")); Serial.print(i + 1); Serial.println(F("] Ready"));
  }

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MOTOR_PIN,  OUTPUT);
  noTone(BUZZER_PIN);
  digitalWrite(MOTOR_PIN, LOW);

  MsTimer2::set(100, timerIsr);
  MsTimer2::start();

  lcdDisplay(0, 0, "  Umbrella Sys  ");
  lcdDisplay(0, 1, "   Ready! :)    ");
  delay(1000);
  updateLCD();

  Serial.println(F("[SYS]   Ready! Tap a card."));
  Serial.println(F("===================================="));

  connectTcpServer();
}

void connectTcpServer(void) {
  if (!wifiConnected) {
    Serial.println(F("[TCP]   WiFi offline, skip connect"));
    return;
  }
  if (client.connected()) {
    Serial.println(F("[TCP]   Already connected"));
    return;
  }

  client.stop();

  Serial.print(F("[TCP]   Connecting "));
  Serial.print(SERVER_NAME);
  Serial.print(F(":"));
  Serial.print(SERVER_PORT);
  Serial.print(F(" ..."));

  if (!client.connect(SERVER_NAME, SERVER_PORT)) {
    Serial.println(F(" FAILED"));
    return;
  }
  Serial.println(F(" OK"));

  char login[30];
  snprintf(login, sizeof(login), "[%s:PASSWD]\n", CLIENT_ID);
  client.print(login);
  client.flush();

  Serial.print(F("[TCP]   Login: "));
  Serial.print(login);

  unsigned long t = millis();
  while (millis() - t < 600) {
    if (client.available()) {
      while (client.available()) client.read();
      break;
    }
  }
}
// ==========================================================
void loop() {

  // 서버에서 온 TCP 명령 처리
  // 클라이언트 입력: [UMB_ARD]SLOT@1@OPEN
  // 아두이노 수신:   [YGY_ADM]SLOT@1@OPEN
  if (client.available()) {
    socketEvent();
  }

  // RFID 감지
  if (rfid.PICC_IsNewCardPresent()) {
    if (rfid.PICC_ReadCardSerial()) {
      char uid[20] = "";
      for (byte i = 0; i < rfid.uid.size; i++) {
        char hex[3];
        sprintf(hex, "%02X", rfid.uid.uidByte[i]);
        strcat(uid, hex);
      }
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      processRFID(uid);
    } else {
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }
  }

  // 100ms마다: 건조% 계산 + LCD 갱신
  if (ms100Flag) {
    ms100Flag = false;
    for (int i = 0; i < SLOT_COUNT; i++) {
      if (slots[i].state == SLOT_LOCKED)
        updateDryPercent(i);
    }
    updateLCD();
  }

  // 1초마다: 슬롯 상태 전환 + 모터 레벨 + WET DB 전송
  if (timerIsrFlag) {
    timerIsrFlag = false;
    for (int i = 0; i < SLOT_COUNT; i++) {
      processSlot(i);
      // LOCKED 슬롯 10초 샘플링 후 10초마다 건조% DB 전송
      if (slots[i].state == SLOT_LOCKED &&
          slots[i].dryPercent >= 0 &&
          secCount - slots[i].stateTimer >= WATER_SAMPLE_SEC &&
          secCount - slots[i].lastDrySent >= DRY_SEND_INTERVAL) {
        sendToServer("wet", i + 1, "", slots[i].dryPercent);
        slots[i].lastDrySent = secCount;
      }
    }
    if (fullDisplay && (secCount - fullDisplayTimer >= LCD_FULL_SEC))
      fullDisplay = false;
    updateMotorLevel();
  }

  // 도난 알람 (non-blocking)
  updateTheftAlarm();

  // 모터 ON/OFF 타이밍 (non-blocking)
  updateMotorTiming();

  // 서보 detach 타이머 (non-blocking)
  updateServoDetach();
}

// ── RFID 처리 ────────────────────────────────────────
void processRFID(char* uid) {
  Serial.println(F("------------------------------------"));
  Serial.print(F("[RFID]  UID = ")); Serial.println(uid);

  for (int i = 0; i < SLOT_COUNT; i++) {
    if (strlen(slots[i].uid) > 0) {
      Serial.print(F("[DEBUG] Slot")); Serial.print(i + 1);
      Serial.print(F(" state=")); Serial.print(slots[i].state);
      Serial.print(F(" uid=")); Serial.println(slots[i].uid);
    }
  }

  // CASE 1: 등록된 UID → 회수
  // 서보 먼저 열고 DB 기록은 IR로 우산 제거 확인 후 전송
  for (int i = 0; i < SLOT_COUNT; i++) {
    if ((slots[i].state == SLOT_LOCKED || slots[i].state == SLOT_WAITING)
        && strcmp(slots[i].uid, uid) == 0) {
      Serial.print(F("[RFID]  Match! Slot ")); Serial.print(i + 1);
      Serial.println(F(" -> RETRIEVE"));
      startRetrieve(i);   // 서보 즉시 열기 (네트워크 호출 전)
      updateLCD();
      return;
    }
  }

  // CASE 2: 빈 슬롯 → 보관
  // 서보 먼저 열고 DB 기록은 IR 10초 확인 + 잠금 후 전송
  for (int i = 0; i < SLOT_COUNT; i++) {
    if (slots[i].state == SLOT_EMPTY) {
      Serial.print(F("[RFID]  New -> Slot ")); Serial.print(i + 1);
      Serial.println(F(" assigned"));
      strcpy(slots[i].uid, uid);
      slots[i].state      = SLOT_OPEN;
      slots[i].stateTimer = secCount;
      openSlot(i);        // 서보 즉시 열기 (네트워크 호출 전)
      updateLCD();
      return;
    }
  }

  // CASE 3: 만석
  Serial.println(F("[RFID]  All slots FULL!"));
  fullDisplay      = true;
  fullDisplayTimer = secCount;
  tone(BUZZER_PIN, 1000, 300);
  updateLCD();
}

// ── 슬롯 상태 처리 (1초마다) ───────────────────────────
void processSlot(int idx) {
  bool ir = (digitalRead(IR_PINS[idx]) == LOW); // LOW = 우산 있음

  if (ir != prevIR[idx]) {
    Serial.print(F("[IR")); Serial.print(idx + 1); Serial.print(F("]  "));
    Serial.print(prevIR[idx] ? F("감지->없음") : F("없음->감지"));
    Serial.print(F("  raw=")); Serial.println(digitalRead(IR_PINS[idx]));
    prevIR[idx] = ir;
  }

  switch (slots[idx].state) {

    case SLOT_EMPTY:
      break;

    case SLOT_OPEN:
      if (ir) {
        slots[idx].state      = SLOT_WAITING;
        slots[idx].stateTimer = secCount;
      }
      break;

    case SLOT_WAITING:
      if (!ir) {
        // 우산 다시 빠짐 → OPEN 복귀
        slots[idx].state      = SLOT_OPEN;
        slots[idx].stateTimer = secCount;
      } else if (secCount - slots[idx].stateTimer >= LOCK_DELAY_SEC) {
        // 10초 IR 유지 확인 → 서보 잠금
        lockSlot(idx);
        slots[idx].state         = SLOT_LOCKED;
        slots[idx].peakWaterVal  = 0;
        slots[idx].dryPercent    = -1;
        slots[idx].stateTimer    = secCount;  // 샘플링 타이머 시작
        slots[idx].lastDrySent   = secCount;
        // 우산 삽입 확인 완료 → DB 보관 기록 전송
        sendToServer("store", idx + 1, slots[idx].uid, 0);
        Serial.print(F("[WATER] Slot ")); Serial.print(idx + 1);
        Serial.println(F(" sampling start (10s)"));
      }
      break;

    case SLOT_LOCKED:
      if (!ir) {
        // 서보 잠긴 상태에서 IR 소실 → 도난
        if (!slots[idx].theftDetected) {
          slots[idx].theftDetected = true;
          theftAlarmActive         = true;
          theftAlarmTimer          = millis();
          buzzerMuted              = false;
          sendToServer("theft", idx + 1, "", 0);
          Serial.print(F("[!!THEFT!!] Slot ")); Serial.println(idx + 1);
        }
      } else {
        slots[idx].theftDetected = false;
      }
      break;

    case SLOT_RETRIEVING:
      if (!ir) {
        // IR로 우산 제거 확인 완료 → DB 회수 기록 전송
        sendToServer("retrieve", idx + 1, slots[idx].uid, 0);
        lockSlot(idx);
        clearSlotData(idx);
      } else if (secCount - slots[idx].stateTimer >= RETRIEVE_TIMEOUT_SEC) {
        // 30초 타임아웃 → 다시 잠금
        lockSlot(idx);
        slots[idx].state = SLOT_LOCKED;
        Serial.print(F("[SLOT")); Serial.print(idx + 1);
        Serial.println(F("] Retrieve timeout -> Relocked"));
      }
      break;
  }

  if (slots[idx].state != prevSlotState[idx]) {
    const char* nm[] = {"EMPTY", "OPEN", "WAITING", "LOCKED", "RETRIEVING"};
    Serial.print(F("[SLOT")); Serial.print(idx + 1); Serial.print(F("]  "));
    Serial.print(nm[prevSlotState[idx]]);
    Serial.print(F(" -> "));
    Serial.println(nm[slots[idx].state]);
    prevSlotState[idx] = slots[idx].state;
  }
}

void sendToServer(const char* action, int slot, const char* uid, int dry) {
  if (!wifiConnected) {
    Serial.println(F("[TCP]   WiFi offline, skip"));
    return;
  }

  // 끊겼을 때만 재연결 + 로그인
  if (!client.connected()) {
    client.stop();

    Serial.print(F("[TCP]   Connecting "));
    Serial.print(SERVER_NAME); Serial.print(F(":")); Serial.print(SERVER_PORT);
    Serial.print(F(" ..."));

    if (!client.connect(SERVER_NAME, SERVER_PORT)) {
      Serial.println(F(" FAILED"));
      return;
    }
    Serial.println(F(" OK"));

    char login[30];
    snprintf(login, sizeof(login), "[%s:PASSWD]\n", CLIENT_ID);
    client.print(login);
    client.flush();
    Serial.print(F("[TCP]   Login: ")); Serial.print(login);

    unsigned long t = millis();
    while (millis() - t < 600) {
      if (client.available()) {
        while (client.available()) client.read();  // 읽고 버림
        break;
      }
    }
  }

  char msg[120];
  if (strcmp(action, "store") == 0)
    snprintf(msg, sizeof(msg), "[%s]STORE@%d@%s@%d\n", DB_CLIENT_ID, slot, uid, dry);
  else if (strcmp(action, "retrieve") == 0)
    snprintf(msg, sizeof(msg), "[%s]RETRIEVE@%d@%s\n", DB_CLIENT_ID, slot, uid);
  else if (strcmp(action, "wet") == 0)
    snprintf(msg, sizeof(msg), "[%s]WET@%d@%d\n", DB_CLIENT_ID, slot, dry);
  else if (strcmp(action, "theft") == 0)
    snprintf(msg, sizeof(msg), "[%s]THEFT@%d\n", DB_CLIENT_ID, slot);
  else {
    Serial.println(F("[TCP]   Unknown action"));
    return;
  }

  client.print(msg);
  client.flush();
  Serial.print(F("[TCP]   Send: ")); Serial.print(msg);
}

// ── 수위 평균 읽기 (5회 평균, 노이즈 제거) ──────────────
int readWaterAvg() {
  long sum = 0;
  for (int i = 0; i < 5; i++) sum += analogRead(WATER_PIN);
  return (int)(sum / 5);
}

// ── 건조% 계산 (100ms마다) ─────────────────────────────
void updateDryPercent(int idx) {
  int cur = readWaterAvg();

  // 잠금 후 10초: 피크값 수집 → --% 유지
  if (secCount - slots[idx].stateTimer < WATER_SAMPLE_SEC) {
    if (cur > slots[idx].peakWaterVal) {
      slots[idx].peakWaterVal = cur;
      Serial.print(F("[WATER] Slot ")); Serial.print(idx + 1);
      Serial.print(F(" peak=")); Serial.println(cur);
    }
    slots[idx].dryPercent = -1;
    return;
  }

  // 10초 후: peak(0%) → WATER_DRY(99%) 매핑
  if (slots[idx].peakWaterVal <= WATER_DRY) {
    slots[idx].dryPercent = 0;  // 물이 거의 없었음
    return;
  }

  int pct = map(cur, slots[idx].peakWaterVal, WATER_DRY, 0, 99);
  slots[idx].dryPercent = constrain(pct, 0, 99);
}

// ── 모터 세기 결정 (히스테리시스) ──────────────────────
void updateMotorLevel() {
  bool hasLocked = false;
  for (int i = 0; i < SLOT_COUNT; i++)
    if (slots[i].state == SLOT_LOCKED) { hasLocked = true; break; }

  int w = readWaterAvg();
  int newLevel = currentMotorLevel;

  if (!hasLocked) {
    newLevel = 0;
  } else {
    switch (currentMotorLevel) {
      case 0:
        if      (w > WATER_HIGH_ON) newLevel = 3;
        else if (w > WATER_MID_ON)  newLevel = 2;
        else if (w > WATER_LOW_ON)  newLevel = 1;
        break;
      case 1:
        if      (w > WATER_HIGH_ON)  newLevel = 3;
        else if (w > WATER_MID_ON)   newLevel = 2;
        else if (w < WATER_LOW_OFF)  newLevel = 0;
        break;
      case 2:
        if      (w > WATER_HIGH_ON) newLevel = 3;
        else if (w < WATER_MID_OFF) newLevel = 1;
        break;
      case 3:
        if (w < WATER_HIGH_OFF) newLevel = 2;
        break;
    }
  }

  if (newLevel != currentMotorLevel) {
    const char* lv[] = {"OFF", "LOW(0.2/0.8s)", "MID(0.5/0.5s)", "HIGH(ON)"};
    Serial.print(F("[MOTOR] "));
    Serial.print(lv[currentMotorLevel]);
    Serial.print(F(" -> "));
    Serial.print(lv[newLevel]);
    Serial.print(F("  water=")); Serial.println(w);

    currentMotorLevel = newLevel;
    switch (currentMotorLevel) {
      case 0:
        motorOnTime = 0; motorOffTime = 0;
        digitalWrite(MOTOR_PIN, LOW);
        break;
      case 1:
        motorOnTime = 200; motorOffTime = 800;
        motorTimer = millis(); motorPhase = true;
        digitalWrite(MOTOR_PIN, HIGH);
        break;
      case 2:
        motorOnTime = 500; motorOffTime = 500;
        motorTimer = millis(); motorPhase = true;
        digitalWrite(MOTOR_PIN, HIGH);
        break;
      case 3:
        motorOnTime = 1000; motorOffTime = 0;
        digitalWrite(MOTOR_PIN, HIGH);
        break;
    }
  }
}

// ── 모터 ON/OFF 타이밍
void updateMotorTiming() {
  if (motorOffTime == 0) return;

  unsigned long now = millis();
  if (motorPhase) {
    if (now - motorTimer >= (unsigned long)motorOnTime) {
      digitalWrite(MOTOR_PIN, LOW);
      motorPhase = false; motorTimer = now;
    }
  } else {
    if (now - motorTimer >= (unsigned long)motorOffTime) {
      digitalWrite(MOTOR_PIN, HIGH);
      motorPhase = true; motorTimer = now;
    }
  }
}

// ── 서보 detach 타이머
void updateServoDetach() {
  for (int i = 0; i < SLOT_COUNT; i++) {
    if (slots[i].servoMoving) {
      if (millis() - slots[i].servoTimer >= SERVO_MOVE_MS) {
        servos[i].detach();
        slots[i].servoMoving = false;
        Serial.print(F("[SERVO")); Serial.print(i + 1);
        Serial.println(F("] DETACHED"));
      }
    }
  }
}

// ── 도난 알람
void updateTheftAlarm() {
  if (!theftAlarmActive) return;
  bool any = false;
  for (int i = 0; i < SLOT_COUNT; i++)
    if (slots[i].theftDetected) { any = true; break; }

  if (!any) {
    theftAlarmActive = false;
    buzzerMuted = false;
    noTone(BUZZER_PIN);
    Serial.println(F("[ALARM] Cleared"));
    return;
  }

  // 관리자가 BUZ@OFF를 보낸 경우,
  // 도난 상태는 유지하되 부저 소리만 멈춘다.
  if (buzzerMuted) {
    noTone(BUZZER_PIN);
    return;
  }

  if (millis() - theftAlarmTimer >= 1500) {
    tone(BUZZER_PIN, 2500, 800);
    theftAlarmTimer = millis();
  }
}

// ── LCD 업데이트 ─────────────────────────────────────
void updateLCD() {
  if (fullDisplay) {
    lcdDisplay(0, 0, "  SLOT IS FULL! ");
    lcdDisplay(0, 1, "  PLEASE WAIT.. ");
    return;
  }

  char s[3];
  for (int i = 0; i < 3; i++) {
    s[i] = (slots[i].state == SLOT_LOCKED ||
            slots[i].state == SLOT_WAITING ||
            slots[i].state == SLOT_RETRIEVING) ? 'X' : 'O';
  }

  char d[3][5];
  for (int i = 0; i < 3; i++) {
    if (slots[i].state == SLOT_LOCKED && slots[i].dryPercent >= 0)
      snprintf(d[i], 5, "%02d%%", slots[i].dryPercent);
    else
      strncpy(d[i], "--%", 5);
  }

  snprintf(lcdLine1, 17, "1:%c|2:%c|3:%c  ", s[0], s[1], s[2]);
  snprintf(lcdLine2, 17, "%s|%s|%s   ",      d[0], d[1], d[2]);
  lcdDisplay(0, 0, lcdLine1);
  lcdDisplay(0, 1, lcdLine2);
}

// ── 서보 열기
void openSlot(int idx) {
  servos[idx].attach(SERVO_PINS[idx]);
  servos[idx].write(SERVO_OPEN_DEG);
  slots[idx].servoMoving = true;
  slots[idx].servoTimer  = millis();
  Serial.print(F("[SERVO")); Serial.print(idx + 1);
  Serial.println(F("] OPEN 90deg"));
}

// ── 서보 닫기
void lockSlot(int idx) {
  servos[idx].attach(SERVO_PINS[idx]);
  servos[idx].write(SERVO_CLOSE_DEG);
  slots[idx].servoMoving = true;
  slots[idx].servoTimer  = millis();
  Serial.print(F("[SERVO")); Serial.print(idx + 1);
  Serial.println(F("] CLOSED 0deg"));
}

// ── 회수 시작 ────────────────────────────────────────
void startRetrieve(int idx) {
  openSlot(idx);
  slots[idx].state      = SLOT_RETRIEVING;
  slots[idx].stateTimer = secCount;
}

// ── 슬롯 데이터 초기화 ───────────────────────────────
void clearSlotData(int idx) {
  slots[idx].state         = SLOT_EMPTY;
  memset(slots[idx].uid, 0, sizeof(slots[idx].uid));
  slots[idx].dryPercent    = -1;
  slots[idx].peakWaterVal  = 0;
  slots[idx].theftDetected = false;
  slots[idx].lastDrySent   = 0;
  // servoMoving 은 여기서 초기화 안 함
  // lockSlot() 직후 호출되므로 updateServoDetach가 700ms 후 처리
  Serial.print(F("[SLOT")); Serial.print(idx + 1);
  Serial.println(F("] Cleared -> EMPTY"));
}

// ── LCD 헬퍼 (16자 고정 패딩) ──────────────────────────
void lcdDisplay(int x, int y, const char* str) {
  int len = 16 - strlen(str);
  lcd.setCursor(x, y);
  lcd.print(str);
  for (int i = len; i > 0; i--)
    lcd.write(' ');
}

void socketEvent() {
  int i = 0;
  char* pToken;
  char* pArray[ARR_CNT] = { 0 };
  char recvBuf[TCP_RECV_SIZE] = { 0 };
  int len;

  len = client.readBytesUntil('\n', recvBuf, TCP_RECV_SIZE - 1);
  if (len <= 0) {
    return;
  }

  recvBuf[len] = '\0';

  // 혹시 남은 \r 제거
  for (int j = 0; j < len; j++) {
    if (recvBuf[j] == '\r' || recvBuf[j] == '\n') {
      recvBuf[j] = '\0';
      break;
    }
  }

  client.flush();

  Serial.print(F("[TCP RX] "));
  Serial.println(recvBuf);

  pToken = strtok(recvBuf, "[@]");
  while (pToken != NULL) {
    pArray[i] = pToken;
    if (++i >= ARR_CNT)
      break;
    pToken = strtok(NULL, "[@]");
  }

  // 서버 로그인 응답 처리: 예) [UMB_ARD] New connected!
  if (pArray[1] != NULL && !strncmp(pArray[1], " New connected", 4)) {
    Serial.println(F("[TCP]   New connected msg ignored"));
    return;
  }

  // 최소 형식 검사
  if (pArray[0] == NULL || pArray[1] == NULL) {
    Serial.println(F("[TCP]   Invalid format"));
    return;
  }

  // 관리자 계정만 허용
  if (strcmp(pArray[0], "YGY_ADM") != 0) {
    Serial.print(F("[TCP]   Not admin: "));
    Serial.println(pArray[0]);
    return;
  }

  // SLOT 명령 처리
  if (!strcmp(pArray[1], "SLOT")) {
    if (pArray[2] == NULL || pArray[3] == NULL) {
      Serial.println(F("[SLOT]  Format error"));
      return;
    }

    // 전체 슬롯 제어: [UMB_ARD]SLOT@ALL@OPEN / CLOSE
    if (!strcmp(pArray[2], "ALL")) {
      if (!strcmp(pArray[3], "OPEN")) {
        for (int s = 0; s < SLOT_COUNT; s++) openSlot(s);
        Serial.println(F("[ADMIN] SLOT ALL OPEN"));
      }
      else if (!strcmp(pArray[3], "CLOSE")) {
        for (int s = 0; s < SLOT_COUNT; s++) lockSlot(s);
        Serial.println(F("[ADMIN] SLOT ALL CLOSE"));
      }
      else {
        Serial.println(F("[SLOT]  Bad action"));
      }
      return;
    }

    // 개별 슬롯 제어: [UMB_ARD]SLOT@1@OPEN / CLOSE
    int slot = atoi(pArray[2]);
    if (slot < 1 || slot > SLOT_COUNT) {
      Serial.println(F("[SLOT]  Bad slot"));
      return;
    }
    int idx = slot - 1;

    if (!strcmp(pArray[3], "OPEN")) {
      openSlot(idx);
      Serial.print(F("[ADMIN] SLOT ")); Serial.print(slot); Serial.println(F(" OPEN"));
    }
    else if (!strcmp(pArray[3], "CLOSE")) {
      lockSlot(idx);
      Serial.print(F("[ADMIN] SLOT ")); Serial.print(slot); Serial.println(F(" CLOSE"));
    }
    else {
      Serial.println(F("[SLOT]  Bad action"));
    }
  }
  else if (!strcmp(pArray[1], "BUZ")) {
    if (pArray[2] == NULL) {
      Serial.println(F("[BUZ]   Format error"));
      return;
    }
    if (!strcmp(pArray[2], "OFF")) {
      buzzerMuted = true;
      theftAlarmActive = false;
      noTone(BUZZER_PIN);
      Serial.println(F("[ADMIN] BUZZER OFF"));
    }
    else {
      Serial.println(F("[BUZ]   Bad action"));
    }
  }
  else {
    Serial.print(F("[TCP]   Unknown cmd: "));
    Serial.println(pArray[1]);
  }
}

// ── 타이머 ISR (100ms마다 ms100Flag, 1초마다 timerIsrFlag) ──
void timerIsr() {
  static unsigned long cnt = 0;
  cnt++;
  ms100Flag = true;

  if (!(cnt % 10)) {
    timerIsrFlag = true;
    secCount++;
  }
}
