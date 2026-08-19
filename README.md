# ☔ Anti-Theft Umbrella Stand

Arduino, Raspberry Pi, STM32를 이용한 **IoT 기반 도난방지 우산 관리 시스템**

RFID 인증을 통해 우산을 보관하고 회수하며,
우산의 건조 상태와 도난 여부를 감지하고 관리할 수 있도록 구현한 임베디드 시스템 프로젝트입니다.

---

## 📌 주요 기능

* RFID 기반 사용자 인증
* 우산 보관 및 회수
* Servo Motor를 이용한 잠금 / 해제
* IR Sensor 기반 우산 존재 여부 감지
* Water Sensor 기반 우산 건조 상태 측정
* DC Motor를 이용한 건조 기능
* 강제 우산 제거 시 도난 감지
* Buzzer / LED 알림
* Raspberry Pi 기반 TCP Server
* MariaDB를 이용한 사용자 / 슬롯 / 로그 관리
* STM32 LCD를 통한 우산 상태 표시

---

## 🏗 System Architecture

```text
┌─────────────────────────────┐
│           Arduino           │
│                             │
│  RFID                       │
│  IR Sensor                  │
│  Water Sensor               │
│  Servo Motor                │
│  DC Motor                   │
│  Buzzer / LED               │
└──────────────┬──────────────┘
               │
               │ Wi-Fi / TCP
               ▼
┌─────────────────────────────┐
│       Raspberry Pi          │
│                             │
│  TCP Server                 │
│  DB Client                  │
│  MariaDB                    │
│  Web Management             │
└──────────────┬──────────────┘
               │
               │ Bluetooth
               ▼
┌─────────────────────────────┐
│            STM32            │
│                             │
│  LCD Status Display         │
└─────────────────────────────┘
```

---

## 🔧 Hardware

### Arduino

* RFID Reader
* IR Sensor
* Water Sensor
* Servo Motor
* DC Motor
* Buzzer
* LED
* Wi-Fi Module

### Raspberry Pi

* TCP Server
* MariaDB
* Bluetooth Communication
* Web Server

### STM32

* LCD
* Bluetooth Module

---

## 💻 Software

### Arduino

우산꽂이의 실제 하드웨어를 제어합니다.

주요 역할:

```text
RFID 인증
    ↓
Slot 선택
    ↓
Servo Unlock
    ↓
우산 삽입 확인
    ↓
Servo Lock
    ↓
건조 상태 측정
    ↓
Raspberry Pi 전송
```

도난 감지는 잠금 상태에서 IR Sensor가 우산 없음 상태를 감지하면 수행합니다.

---

## 🌐 Network Communication

Arduino와 Raspberry Pi는 TCP Socket 통신을 사용합니다.

```text
Arduino
   │
   │ TCP Socket
   ▼
IoT TCP Server
   │
   ▼
DB Client
   │
   ▼
MariaDB
```

대표 메시지 형식:

```text
STORE@slot@uid@dry
RETRIEVE@slot@uid
WET@slot@dry
THEFT@slot
```

예:

```text
STORE@1@07FCEB05@0
RETRIEVE@1@07FCEB05
WET@1@45
THEFT@1
```

---

## 🗄 Database

MariaDB를 사용하여 우산 상태와 사용 기록을 관리합니다.

주요 테이블:

```text
user
slot
log
```

### slot

```text
slot_id
status
assigned_uid
dry_level
locked
updated_at
```

대표 상태:

```text
EMPTY
USING
DRYING
DRY_DONE
THEFT
```

---

## 📡 Bluetooth Communication

STM32와 Raspberry Pi는 Bluetooth를 이용해 통신합니다.

```text
STM32
   │
   │ Bluetooth
   ▼
Raspberry Pi
   │
   ▼
MariaDB
   │
   ▼
Status Query
   │
   ▼
Raspberry Pi
   │
   │ Bluetooth
   ▼
STM32 LCD
```

STM32는 Raspberry Pi를 통해 DB의 우산 상태 정보를 조회하고 LCD에 표시합니다.

---

## 🔄 System Flow

```text
RFID Card
    ↓
User Authentication
    ↓
Slot Unlock
    ↓
Umbrella Insert
    ↓
IR Sensor Detection
    ↓
Slot Lock
    ↓
Water Sensor
    ↓
Drying
    ↓
TCP Message
    ↓
Raspberry Pi
    ↓
MariaDB
    ↓
Web / STM32 LCD
```

---

## 🚨 Theft Detection

잠금 상태에서 우산이 강제로 제거되면 IR Sensor 상태를 이용해 도난을 감지합니다.

```text
Servo Locked
      +
IR Sensor = Umbrella Missing
      ↓
Theft Detected
      ↓
Buzzer Alarm
      ↓
TCP Message
      ↓
MariaDB
```

---

## 🛠 Troubleshooting

### 1. Client ID Buffer Overflow

초기 Client ID:

```text
UMB_STATION1
```

Server의 ID buffer 크기보다 길어 문제가 발생했습니다.

다음과 같이 수정했습니다.

```text
UMB_ARD
```

---

### 2. UID Mismatch

TCP 메시지에 `\r\n`이 포함되면서 UID 끝에 `\r`이 남아 DB 비교가 실패하는 문제가 발생했습니다.

기존:

```text
\r\n
```

수정:

```text
\n
```

---

### 3. Blocking으로 인한 RFID 인식률 저하

서버 응답을 기다리는 동안 Arduino의 `loop()`가 지연되어 RFID 인식률이 떨어지는 문제가 있었습니다.

불필요한 응답 대기를 제거하고 `millis()` 기반 Non-blocking 방식으로 수정했습니다.

---

## 📁 Repository Structure

```text
anti-theft-umbrella-stand/
│
├── README.md
│
├── arduino/
│   └── umbrella_station.ino
│
├── raspberrypi/
│   ├── iot_server.c
│   ├── iot_client_db.c
│   └── iot_client_bluetooth.c
│
├── stm32/
│   └── Core/
│
├── database/
│   └── schema.sql
│
├── web/
│   └── api.php
│
└── docs/
    └── images/
```

---

## 🔑 Tech Stack

`C` `Arduino` `STM32` `Raspberry Pi` `TCP/IP` `Bluetooth` `MariaDB` `Embedded System`
