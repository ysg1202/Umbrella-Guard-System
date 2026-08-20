/*
 * iot_client_bluetooth.c (YGY_BLT) - 블루투스 중계 클라이언트
 *
 * 컴파일:
 *   gcc -o iot_client_bluetooth iot_client_bluetooth.c \
 *       -lmysqlclient -lbluetooth -lpthread
 *
 * 실행:
 *   sudo ./iot_client_bluetooth 10.10.16.63 5000 YGY_BLT
 *
 * 동작:
 *   ① KMG_LIN에서 [YGY_BLT]UMB@ALL → 서버가 YGY_BLT로 라우팅
 *      → recv_msg가 수신 → DB 조회 → BT로 STM32 전송
 *   ② STM32에서 [YGY_BLT]UMB@ALL BT 전송
 *      → send_msg가 수신 → DB 조회 → BT로 STM32 응답
 *
 * BT 전송 형식:
 *   [UMB]1:EM@2:DR@3:EM\n
 *   [SLOT]1@DRYING@45@06-01 11:38\n
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <mysql/mysql.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#define BUF_SIZE   256
#define NAME_SIZE  20
#define ARR_CNT    6

/* ── DB 설정 ────────────────────────────────────── */
#define DB_HOST "localhost"
#define DB_USER "iot"
#define DB_PASS "pwiot"
#define DB_NAME "iotdb"

/* ── BT MAC 주소 ───────────────────────────────────
 * 실제 STM32측 블루투스 모듈 MAC 주소로 교체해서 사용
 */
#define BT_ADDR "00:18:E4:40:00:06"

/* ── 구조체 ─────────────────────────────────────── */
typedef struct {
    int sockfd;
    int btfd;
    char sendid[NAME_SIZE];
} DEV_FD;

/* ── 전역 ───────────────────────────────────────── */
char name[NAME_SIZE] = "[Default]";
MYSQL *g_conn = NULL;
pthread_mutex_t g_db_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_bt_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── 함수 선언 ──────────────────────────────────── */
void *send_msg(void *arg);
void *recv_msg(void *arg);
void  process_cmd(int btfd, char *raw_msg);
void  query_umb_all(char *out, int len);
void  query_slot_one(int slot_id, char *out, int len);
void  error_handling(char *msg);

/* ================================================= */
int main(int argc, char *argv[])
{
    DEV_FD dev_fd;
    struct sockaddr_in serv_addr;
    struct sockaddr_rc bt_addr = {0};
    pthread_t snd_thread, rcv_thread;
    void *thread_return;
    char msg[BUF_SIZE];

    if (argc != 4) {
        printf("Usage: %s <IP> <port> <name>\n", argv[0]);
        exit(1);
    }
    sprintf(name, "%s", argv[3]);

    /* ── MySQL 초기화 ────────────────────────────── */
    g_conn = mysql_init(NULL);
    printf("[DB] Connecting to %s/%s ...\n", DB_HOST, DB_NAME);
    if (!mysql_real_connect(g_conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
        fprintf(stderr, "[DB] Error: %s\n", mysql_error(g_conn));
        exit(1);
    }
    printf("[DB] Connected!\n");

    /* ── TCP 서버 연결 ───────────────────────────── */
    dev_fd.sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (dev_fd.sockfd == -1) error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(dev_fd.sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    sprintf(msg, "[%s:PASSWD]", name);
    write(dev_fd.sockfd, msg, strlen(msg));
    printf("[TCP] Login as %s\n", name);

    /* ── BT RFCOMM 연결 ──────────────────────────── */
    dev_fd.btfd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (dev_fd.btfd == -1) {
        perror("[BT] socket");
        exit(1);
    }

    bt_addr.rc_family = AF_BLUETOOTH;
    bt_addr.rc_channel = (uint8_t)1;
    str2ba(BT_ADDR, &bt_addr.rc_bdaddr);

    printf("[BT] Connecting to %s ...\n", BT_ADDR);
    if (connect(dev_fd.btfd, (struct sockaddr *)&bt_addr, sizeof(bt_addr)) == -1) {
        perror("[BT] connect");
        exit(1);
    }
    printf("[BT] Connected!\n");
    printf("[SYS] Ready.\n");
    printf("      KMG_LIN에서 [YGY_BLT]UMB@ALL 또는 [YGY_BLT]SLOT@1 전송\n\n");

    pthread_create(&rcv_thread, NULL, recv_msg, (void *)&dev_fd);
    pthread_create(&snd_thread, NULL, send_msg, (void *)&dev_fd);

    pthread_join(snd_thread, &thread_return);

    close(dev_fd.sockfd);
    close(dev_fd.btfd);
    mysql_close(g_conn);
    return 0;
}

/* ────────────────────────────────────────────────
 * send_msg: BT → (UMB/SLOT면 DB조회 후 BT응답, 아니면 서버 중계)
 * ──────────────────────────────────────────────── */
void *send_msg(void *arg)
{
    DEV_FD *dev_fd = (DEV_FD *)arg;
    fd_set initset, newset;
    struct timeval tv;
    char msg[BUF_SIZE];
    int  ret, total = 0;

    FD_ZERO(&initset);
    FD_SET(dev_fd->btfd, &initset);

    while (1) {
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        newset = initset;
        ret = select(dev_fd->btfd + 1, &newset, NULL, NULL, &tv);

        if (FD_ISSET(dev_fd->btfd, &newset)) {
            ret = read(dev_fd->btfd, msg + total, BUF_SIZE - total);
            if (ret > 0) {
                total += ret;
            } else if (ret == 0) {
                dev_fd->sockfd = -1;
                return NULL;
            }

            /* \n 까지 받았으면 처리 */
            if (msg[total - 1] == '\n') {
                msg[total] = '\0';
                total = 0;

                printf("[BT] Recv: %s", msg);

                /* UMB 또는 SLOT 명령이면 로컬 처리 (서버 중계 안 함) */
                if (strstr(msg, "UMB@ALL") || strstr(msg, "SLOT@")) {
                    process_cmd(dev_fd->btfd, msg);
                } else {
                    /* 그 외 메시지는 서버로 중계 */
                    if (write(dev_fd->sockfd, msg, strlen(msg)) <= 0) {
                        dev_fd->sockfd = -1;
                        return NULL;
                    }
                }
            }
        } else {
            continue;
        }

        if (ret == 0 && dev_fd->sockfd == -1)
            return NULL;
    }
}

/* ────────────────────────────────────────────────
 * recv_msg: 서버 → (UMB/SLOT면 DB조회 후 BT응답, 아니면 무시)
 * KMG_LIN이 [YGY_BLT]UMB@ALL 전송 시 서버가 이쪽으로 라우팅
 * 수신 형식: [KMG_LIN]UMB@ALL
 * ──────────────────────────────────────────────── */
void *recv_msg(void *arg)
{
    DEV_FD *dev_fd = (DEV_FD *)arg;
    char name_msg[NAME_SIZE + BUF_SIZE + 1];
    int  str_len;

    while (1) {
        memset(name_msg, 0, sizeof(name_msg));
        str_len = read(dev_fd->sockfd, name_msg, sizeof(name_msg) - 1);
        if (str_len <= 0) {
            dev_fd->sockfd = -1;
            return NULL;
        }
        name_msg[str_len] = '\0';
        printf("[TCP] Recv: %s", name_msg);

        /* UMB 또는 SLOT 명령이면 DB 조회 후 BT 전송 */
        if (strstr(name_msg, "UMB@ALL") || strstr(name_msg, "SLOT@")) {
            process_cmd(dev_fd->btfd, name_msg);
        }
        /* 서버 시스템 메시지는 무시 */
    }
}

/* ────────────────────────────────────────────────
 * process_cmd: 메시지 파싱 → DB 조회 → BT 전송
 * ──────────────────────────────────────────────── */
void process_cmd(int btfd, char *raw_msg)
{
    char  tmp[NAME_SIZE + BUF_SIZE] = {0};
    char  bt_buf[BUF_SIZE] = {0};
    char *pToken;
    char *pArray[ARR_CNT] = {0};
    int   i = 0;

    strncpy(tmp, raw_msg, sizeof(tmp) - 1);
    tmp[strcspn(tmp, "\n")] = '\0';

    /* 구분자: [ @ ] 로 파싱
     * [KMG_LIN]UMB@ALL  → pArray[0]="KMG_LIN", [1]="UMB",  [2]="ALL"
     * [YGY_BLT]SLOT@1   → pArray[0]="YGY_BLT", [1]="SLOT", [2]="1"
     */
    pToken = strtok(tmp, "[@]");
    while (pToken != NULL) {
        pArray[i] = pToken;
        if (++i >= ARR_CNT) break;
        pToken = strtok(NULL, "[@]");
    }

    if (i < 2 || pArray[1] == NULL) return;

    /* 시스템 메시지 무시 */
    if (!strncmp(pArray[1], " New", 4)) return;
    if (!strncmp(pArray[1], " Alr", 4)) return;

    /* ── UMB@ALL: 전체 슬롯 상태 ───────────────── */
    if (!strcmp(pArray[1], "UMB") &&
        pArray[2] != NULL && !strcmp(pArray[2], "ALL")) {

        query_umb_all(bt_buf, sizeof(bt_buf));
        printf("[BT] Send: %s", bt_buf);

        pthread_mutex_lock(&g_bt_mutex);
        write(btfd, bt_buf, strlen(bt_buf));
        pthread_mutex_unlock(&g_bt_mutex);
    }
    /* ── SLOT@N: 특정 슬롯 상세 ────────────────── */
    else if (!strcmp(pArray[1], "SLOT") && pArray[2] != NULL) {
        int slot_id = atoi(pArray[2]);

        if (slot_id >= 1 && slot_id <= 3) {
            query_slot_one(slot_id, bt_buf, sizeof(bt_buf));
            printf("[BT] Send: %s", bt_buf);

            pthread_mutex_lock(&g_bt_mutex);
            write(btfd, bt_buf, strlen(bt_buf));
            pthread_mutex_unlock(&g_bt_mutex);
        }
    }
}

/* ────────────────────────────────────────────────
 * query_umb_all: 전체 슬롯 조회
 * 결과: [UMB]1:EM@2:DR@3:EM\n
 * 상태 코드: EM=EMPTY, US=USING, DR=DRYING, DN=DRY_DONE, TF=THEFT
 * ──────────────────────────────────────────────── */
void query_umb_all(char *out, int len)
{
    MYSQL_RES *res;
    MYSQL_ROW  row;
    char slots[3][4]; /* 2글자 코드 + null */
    int i;

    for (i = 0; i < 3; i++) strcpy(slots[i], "EM");

    pthread_mutex_lock(&g_db_mutex);

    if (mysql_ping(g_conn) != 0)
        mysql_real_connect(g_conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0);

    if (mysql_query(g_conn, "SELECT slot_id, status FROM `slot` ORDER BY slot_id")) {
        fprintf(stderr, "[DB] Error: %s\n", mysql_error(g_conn));
        pthread_mutex_unlock(&g_db_mutex);
        snprintf(out, len, "[UMB]1:EM@2:EM@3:EM\n");
        return;
    }

    res = mysql_store_result(g_conn);
    pthread_mutex_unlock(&g_db_mutex);

    if (!res) {
        snprintf(out, len, "[UMB]1:EM@2:EM@3:EM\n");
        return;
    }

    while ((row = mysql_fetch_row(res))) {
        int idx = atoi(row[0]) - 1;
        char *status = row[1] ? row[1] : "EMPTY";
        if (idx < 0 || idx > 2) continue;

        if      (!strcmp(status, "EMPTY"))    strcpy(slots[idx], "EM");
        else if (!strcmp(status, "USING"))    strcpy(slots[idx], "US");
        else if (!strcmp(status, "DRYING"))   strcpy(slots[idx], "DR");
        else if (!strcmp(status, "DRY_DONE")) strcpy(slots[idx], "DN");
        else if (!strcmp(status, "THEFT"))    strcpy(slots[idx], "TF");
        else                                  strcpy(slots[idx], "??");
    }
    mysql_free_result(res);

    snprintf(out, len, "[UMB]1:%s@2:%s@3:%s\n", slots[0], slots[1], slots[2]);
}

/* ────────────────────────────────────────────────
 * query_slot_one: 특정 슬롯 상세 조회
 * 결과: [SLOT]1@DRYING@45@06-01 11:38\n
 * ──────────────────────────────────────────────── */
void query_slot_one(int slot_id, char *out, int len)
{
    MYSQL_RES *res;
    MYSQL_ROW  row;
    char sql[256];

    pthread_mutex_lock(&g_db_mutex);

    if (mysql_ping(g_conn) != 0)
        mysql_real_connect(g_conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0);

    snprintf(sql, sizeof(sql),
        "SELECT slot_id, status, dry_level, "
        "DATE_FORMAT(updated_at, '%%m-%%d %%H:%%i') "
        "FROM `slot` WHERE slot_id = %d", slot_id);

    if (mysql_query(g_conn, sql)) {
        fprintf(stderr, "[DB] Error: %s\n", mysql_error(g_conn));
        pthread_mutex_unlock(&g_db_mutex);
        snprintf(out, len, "[SLOT]%d@ERR@0@--\n", slot_id);
        return;
    }

    res = mysql_store_result(g_conn);
    pthread_mutex_unlock(&g_db_mutex);

    if (!res) {
        snprintf(out, len, "[SLOT]%d@ERR@0@--\n", slot_id);
        return;
    }

    row = mysql_fetch_row(res);
    if (row) {
        snprintf(out, len, "[SLOT]%s@%s@%s@%s\n",
            row[0] ? row[0] : "?",
            row[1] ? row[1] : "?",
            row[2] ? row[2] : "0",
            row[3] ? row[3] : "--");
    } else {
        snprintf(out, len, "[SLOT]%d@EMPTY@0@--\n", slot_id);
    }
    mysql_free_result(res);
}

void error_handling(char *msg)
{
    fputs(msg, stderr);
    fputc('\n', stderr);
    exit(1);
}
