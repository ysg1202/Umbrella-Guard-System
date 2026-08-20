-- IoT 우산 보관 시스템 DB 스키마
-- 3개 테이블(user, slot, log)로 구성.
-- RFID UID를 키로 사용자·슬롯 상태·이벤트 로그를 연결하고,
-- STORE·RETRIEVE·WET·THEFT 4개 액션을 각각 SQL 트랜잭션으로 처리한다.

CREATE DATABASE IF NOT EXISTS iotdb;
USE iotdb;

CREATE TABLE `user` (
    user_id        INT AUTO_INCREMENT PRIMARY KEY,
    card_uid       VARCHAR(50) UNIQUE NOT NULL,
    user_label     VARCHAR(50),
    user_type      VARCHAR(20) DEFAULT 'NORMAL',
    registered_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE `slot` (
    slot_id        INT PRIMARY KEY,
    status         VARCHAR(20) DEFAULT 'EMPTY',  -- EMPTY/USING/DRYING/DRY_DONE/THEFT
    assigned_uid   VARCHAR(50),
    dry_level      INT DEFAULT 0,
    wet_level      INT DEFAULT 0,
    locked         TINYINT DEFAULT 1,
    updated_at     DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE `log` (
    log_id     INT AUTO_INCREMENT PRIMARY KEY,
    timestamp  DATETIME DEFAULT CURRENT_TIMESTAMP,
    slot_id    INT,
    card_uid   VARCHAR(50),
    action     VARCHAR(30),  -- STORE / PICKUP / THEFT / DRY_UPDATE / DRY_DONE / AUTH_FAIL
    detail     VARCHAR(100)
);

-- 초기 슬롯 3개 생성 (3슬롯 기준 시스템)
INSERT INTO `slot` (slot_id, status) VALUES (1, 'EMPTY'), (2, 'EMPTY'), (3, 'EMPTY');
