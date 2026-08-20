/*
 * iot_client_sensor_device.c (UMB_DB) - DB 클라이언트
 *
 * 서버로부터 전달받은 아두이노 메시지를 분석해 MySQL에 INSERT/UPDATE/SELECT 수행.
 * 처리 액션: STORE(보관) / RETRIEVE(회수) / WET(건조상태) / THEFT(도난)
 *
 * 컴파일: gcc -o iot_client_sensor_device iot_client_sensor_device.c -lmysqlclient -lpthread
 * 실행:   ./iot_client_sensor_device <IP> <port> <name>
 * 예:     ./iot_client_sensor_device 10.10.16.63 5000 UMB_DB
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

#define BUF_SIZE 256
#define NAME_SIZE 32
#define ARR_CNT 8

void* send_msg(void* arg);
void* recv_msg(void* arg);
void error_handling(char* msg);

char name[NAME_SIZE] = "[Default]";
char msg[BUF_SIZE];

int main(int argc, char* argv[])
{
    int sock;
    struct sockaddr_in serv_addr;
    pthread_t snd_thread, rcv_thread;
    void* thread_return;

    if (argc != 4) {
        printf("Usage : %s <IP> <port> <name>\n", argv[0]);
        exit(1);
    }
    sprintf(name, "%s", argv[3]);

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    sprintf(msg, "[%s:PASSWD]", name);
    write(sock, msg, strlen(msg));

    pthread_create(&rcv_thread, NULL, recv_msg, (void*)&sock);
    pthread_create(&snd_thread, NULL, send_msg, (void*)&sock);

    pthread_join(snd_thread, &thread_return);
    pthread_join(rcv_thread, &thread_return);

    if (sock != -1)
        close(sock);

    return 0;
}

void* send_msg(void* arg)
{
    int* sock = (int*)arg;
    int ret;
    fd_set initset, newset;
    struct timeval tv;
    char name_msg[NAME_SIZE + BUF_SIZE + 2];

    FD_ZERO(&initset);
    FD_SET(STDIN_FILENO, &initset);

    fputs("Input a message! [ID]msg (Default ID:ALLMSG)\n", stdout);

    while (1) {
        memset(msg, 0, sizeof(msg));
        memset(name_msg, 0, sizeof(name_msg));

        tv.tv_sec = 1;
        tv.tv_usec = 0;
        newset = initset;

        ret = select(STDIN_FILENO + 1, &newset, NULL, NULL, &tv);

        if (FD_ISSET(STDIN_FILENO, &newset)) {
            fgets(msg, BUF_SIZE, stdin);

            if (!strncmp(msg, "quit\n", 5)) {
                *sock = -1;
                return NULL;
            }
            else if (msg[0] != '[') {
                strcat(name_msg, "[ALLMSG]");
                strcat(name_msg, msg);
            }
            else {
                strcpy(name_msg, msg);
            }

            if (write(*sock, name_msg, strlen(name_msg)) <= 0) {
                *sock = -1;
                return NULL;
            }
        }

        if (ret == 0) {
            if (*sock == -1)
                return NULL;
        }
    }
}

/* 메시지 예시 (strtok 기준: [ : @ ])
 *   [UMB_ARD:STORE@1@07FCEB05@0]
 *   [UMB_ARD:RETRIEVE@1@07FCEB05]
 *   [UMB_ARD:WET@1@45]
 *   [UMB_ARD:THEFT@1]
 */
void* recv_msg(void* arg)
{
    MYSQL* conn;
    int res;
    char sql_cmd[512] = {0};
    char send_buf[BUF_SIZE] = {0};

    char* host = "localhost";
    char* user = "iot";
    char* pass = "pwiot";
    char* dbname = "iotdb";

    int* sock = (int*)arg;
    int i;
    char* pToken;
    char* pArray[ARR_CNT] = {0};

    char name_msg[NAME_SIZE + BUF_SIZE + 1];
    int str_len;

    conn = mysql_init(NULL);
    puts("MYSQL startup");

    if (!(mysql_real_connect(conn, host, user, pass, dbname, 0, NULL, 0))) {
        fprintf(stderr, "ERROR : %s[%d]\n", mysql_error(conn), mysql_errno(conn));
        exit(1);
    }
    else {
        printf("Connection Successful!\n\n");
    }

    while (1) {
        memset(name_msg, 0x0, sizeof(name_msg));
        memset(pArray, 0x0, sizeof(pArray));
        memset(sql_cmd, 0x0, sizeof(sql_cmd));
        memset(send_buf, 0x0, sizeof(send_buf));

        str_len = read(*sock, name_msg, NAME_SIZE + BUF_SIZE);

        if (str_len <= 0) {
            *sock = -1;
            mysql_close(conn);
            return NULL;
        }

        name_msg[str_len] = '\0';
        name_msg[strcspn(name_msg, "\n")] = '\0';
        fputs(name_msg, stdout);
        fputc('\n', stdout);

        pToken = strtok(name_msg, "[:@]");
        i = 0;
        while (pToken != NULL) {
            pArray[i] = pToken;
            if (++i >= ARR_CNT)
                break;
            pToken = strtok(NULL, "[:@]");
        }

        if (i < 2 || pArray[1] == NULL) {
            continue;
        }

        /* 서버 로그인 메시지 무시: 예) [UMB_ARD:PASSWD] */
        if (!strcmp(pArray[1], "PASSWD")) {
            continue;
        }

        /* 1. 우산 보관: [UMB_ARD:STORE@1@07FCEB05@0]
         * pArray[0]=UMB_ARD pArray[1]=STORE pArray[2]=슬롯 pArray[3]=UID pArray[4]=건조도
         */
        if (!strcmp(pArray[1], "STORE") && i == 5) {
            int slot_id = atoi(pArray[2]);
            char uid_esc[100] = {0};
            int dry = atoi(pArray[4]);

            mysql_real_escape_string(conn, uid_esc, pArray[3], strlen(pArray[3]));

            sprintf(sql_cmd,
                "INSERT INTO `user`(uid, user_type) "
                "VALUES('%s', 'NORMAL') "
                "ON DUPLICATE KEY UPDATE uid=uid",
                uid_esc);
            res = mysql_query(conn, sql_cmd);
            if (res) {
                fprintf(stderr, "ERROR USER INSERT: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
                continue;
            }

            sprintf(sql_cmd,
                "UPDATE `slot` "
                "SET status='USING', assigned_uid='%s', dry_level=%d, locked=1, updated_at=NOW() "
                "WHERE slot_id=%d",
                uid_esc, dry, slot_id);
            res = mysql_query(conn, sql_cmd);
            if (res) {
                fprintf(stderr, "ERROR SLOT UPDATE: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
                continue;
            }

            sprintf(sql_cmd,
                "INSERT INTO `log`(slot_id, card_uid, action, detail) "
                "VALUES(%d, '%s', 'STORE', 'Umbrella stored')",
                slot_id, uid_esc);
            res = mysql_query(conn, sql_cmd);
            if (res) {
                fprintf(stderr, "ERROR LOG INSERT: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
                continue;
            }

            printf("[DB] STORE OK - slot=%d uid=%s dry=%d\n", slot_id, uid_esc, dry);
        }
        /* 2. 우산 회수: [UMB_ARD:RETRIEVE@1@07FCEB05] */
        else if (!strcmp(pArray[1], "RETRIEVE") && i == 4) {
            int slot_id = atoi(pArray[2]);
            char uid_esc[100] = {0};

            mysql_real_escape_string(conn, uid_esc, pArray[3], strlen(pArray[3]));

            sprintf(sql_cmd, "SELECT assigned_uid FROM `slot` WHERE slot_id=%d", slot_id);
            res = mysql_query(conn, sql_cmd);
            if (res) {
                fprintf(stderr, "ERROR SELECT SLOT: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
                continue;
            }

            MYSQL_RES* result = mysql_store_result(conn);
            if (result == NULL) {
                fprintf(stderr, "ERROR STORE RESULT: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
                continue;
            }

            MYSQL_ROW row = mysql_fetch_row(result);

            if (row && row[0] && strcmp(row[0], uid_esc) == 0) {
                sprintf(sql_cmd,
                    "UPDATE `slot` "
                    "SET status='EMPTY', assigned_uid=NULL, dry_level=0, locked=1, updated_at=NOW() "
                    "WHERE slot_id=%d",
                    slot_id);
                res = mysql_query(conn, sql_cmd);
                if (res) {
                    fprintf(stderr, "ERROR RETRIEVE UPDATE: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
                    mysql_free_result(result);
                    continue;
                }

                sprintf(sql_cmd,
                    "INSERT INTO `log`(slot_id, card_uid, action, detail) "
                    "VALUES(%d, '%s', 'PICKUP', 'Umbrella retrieved')",
                    slot_id, uid_esc);
                res = mysql_query(conn, sql_cmd);
                if (res) {
                    fprintf(stderr, "ERROR PICKUP LOG: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
                    mysql_free_result(result);
                    continue;
                }

                printf("[DB] RETRIEVE OK - slot=%d uid=%s\n", slot_id, uid_esc);

                sprintf(send_buf, "[%s]DB@RETRIEVE@OK\n", pArray[0]);
                write(*sock, send_buf, strlen(send_buf));
            }
            else {
                sprintf(sql_cmd,
                    "INSERT INTO `log`(slot_id, card_uid, action, detail) "
                    "VALUES(%d, '%s', 'AUTH_FAIL', 'UID mismatch')",
                    slot_id, uid_esc);
                res = mysql_query(conn, sql_cmd);
                if (res) {
                    fprintf(stderr, "ERROR AUTH_FAIL LOG: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
                    mysql_free_result(result);
                    continue;
                }
                printf("[DB] RETRIEVE FAIL - UID mismatch slot=%d uid=%s\n", slot_id, uid_esc);
            }
            mysql_free_result(result);
        }
        /* 3. 건조 상태 업데이트: [UMB_ARD:WET@1@45] */
        else if (!strcmp(pArray[1], "WET") && i == 4) {
            int slot_id = atoi(pArray[2]);
            int dry = atoi(pArray[3]);
            char status[20];
            char action[30];
            char detail[100];

            if (dry >= 95) {
                strcpy(status, "DRY_DONE");
                strcpy(action, "DRY_DONE");
                strcpy(detail, "Drying completed");
            }
            else {
                strcpy(status, "DRYING");
                strcpy(action, "DRY_UPDATE");
                strcpy(detail, "Dry level updated");
            }

            sprintf(sql_cmd,
                "UPDATE `slot` SET status='%s', dry_level=%d, updated_at=NOW() WHERE slot_id=%d",
                status, dry, slot_id);
            res = mysql_query(conn, sql_cmd);
            if (res) {
                fprintf(stderr, "ERROR WET UPDATE: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
                continue;
            }

            sprintf(sql_cmd,
                "INSERT INTO `log`(slot_id, card_uid, action, detail) VALUES(%d, NULL, '%s', '%s')",
                slot_id, action, detail);
            res = mysql_query(conn, sql_cmd);
            if (res) {
                fprintf(stderr, "ERROR WET LOG: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
                continue;
            }

            printf("[DB] WET OK - slot=%d dry=%d status=%s\n", slot_id, dry, status);
        }
        /* 4. 도난 감지: [UMB_ARD:THEFT@1] */
        else if (!strcmp(pArray[1], "THEFT") && i == 3) {
            int slot_id = atoi(pArray[2]);

            sprintf(sql_cmd,
                "UPDATE `slot` SET status='THEFT', locked=1, updated_at=NOW() WHERE slot_id=%d",
                slot_id);
            res = mysql_query(conn, sql_cmd);
            if (res) {
                fprintf(stderr, "ERROR THEFT UPDATE: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
                continue;
            }

            sprintf(sql_cmd,
                "INSERT INTO `log`(slot_id, card_uid, action, detail) "
                "VALUES(%d, NULL, 'THEFT', 'Forced removal detected')",
                slot_id);
            res = mysql_query(conn, sql_cmd);
            if (res) {
                fprintf(stderr, "ERROR THEFT LOG: %s[%d]\n", mysql_error(conn), mysql_errno(conn));
                continue;
            }

            printf("[DB] THEFT OK - slot=%d\n", slot_id);
        }
        else {
            printf("[DB] Unknown or invalid message format\n");
        }
    }

    mysql_close(conn);
    return NULL;
}

void error_handling(char* msg)
{
    fputs(msg, stderr);
    fputc('\n', stderr);
    exit(1);
}
