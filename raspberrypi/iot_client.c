/*
 * iot_client.c - 범용 콘솔 TCP 클라이언트 (관리자/디버그용)
 *
 * 컴파일: gcc -o iot_client iot_client.c -lpthread
 * 실행:   ./iot_client <IP> <port> <name> <passwd>
 * 예:     ./iot_client 10.10.16.63 5000 YGY_ADM ADMIN1
 *
 * 입력 형식: [ID]msg  (ID 생략 시 기본값 ALLMSG)
 * 관리자 명령 예: [UMB_ARD]SLOT@1@OPEN, [UMB_ARD]SLOT@ALL@CLOSE, [UMB_ARD]BUZ@OFF
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

#define BUF_SIZE 100
#define NAME_SIZE 20
#define PASS_SIZE 20
#define ARR_CNT 5

void * send_msg(void * arg);
void * recv_msg(void * arg);
void error_handling(char * msg);

char name[NAME_SIZE] = "[Default]";
char passwd[PASS_SIZE] = "PASSWD";
char msg[BUF_SIZE];

int main(int argc, char *argv[])
{
    int sock;
    struct sockaddr_in serv_addr;
    pthread_t snd_thread, rcv_thread;
    void * thread_return;

    if (argc != 5) {
        printf("Usage : %s <IP> <port> <name> <passwd>\n", argv[0]);
        printf("Example : %s 10.10.16.63 5000 YGY_ADM ADMIN1\n", argv[0]);
        exit(1);
    }
    snprintf(name, sizeof(name), "%s", argv[3]);
    snprintf(passwd, sizeof(passwd), "%s", argv[4]);

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    snprintf(msg, sizeof(msg), "[%s:%s]", name, passwd);
    write(sock, msg, strlen(msg));

    printf("[TCP] Connected to server %s:%s as %s\n", argv[1], argv[2], name);
    printf("[TCP] Login message: %s\n", msg);

    pthread_create(&rcv_thread, NULL, recv_msg, (void *)&sock);
    pthread_create(&snd_thread, NULL, send_msg, (void *)&sock);

    pthread_join(snd_thread, &thread_return);
    // pthread_join(rcv_thread, &thread_return);

    close(sock);
    return 0;
}

void * send_msg(void * arg)
{
    int *sock = (int *)arg;
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

        if (ret < 0) {
            perror("select()");
            *sock = -1;
            return NULL;
        }

        if (FD_ISSET(STDIN_FILENO, &newset)) {
            if (fgets(msg, BUF_SIZE, stdin) == NULL) {
                *sock = -1;
                return NULL;
            }

            if (!strncmp(msg, "quit\n", 5)) {
                *sock = -1;
                return NULL;
            }
            else if (msg[0] != '[') {
                snprintf(name_msg, sizeof(name_msg), "[ALLMSG]%s", msg);
            }
            else {
                snprintf(name_msg, sizeof(name_msg), "%s", msg);
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

void * recv_msg(void * arg)
{
    int * sock = (int *)arg;
    char name_msg[NAME_SIZE + BUF_SIZE + 1];
    int str_len;

    while (1) {
        memset(name_msg, 0x0, sizeof(name_msg));
        str_len = read(*sock, name_msg, NAME_SIZE + BUF_SIZE);
        if (str_len <= 0) {
            *sock = -1;
            return NULL;
        }
        name_msg[str_len] = 0;
        fputs(name_msg, stdout);
    }
}

void error_handling(char * msg)
{
    fputs(msg, stderr);
    fputc('\n', stderr);
    exit(1);
}
