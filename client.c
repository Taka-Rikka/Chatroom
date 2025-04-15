#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_BUFFER_SIZE 2048

// 全局变量
volatile sig_atomic_t exitFlag = 0;
int socket_fd = 0;
char userName[32];

// 输出提示符
void printPrompt() {
    printf("> ");
    fflush(stdout);
}

// 去除字符串中的换行符
void trimNewline(char *str, int len) {
    for (int i = 0; i < len; i++) {
        if (str[i] == '\n') {
            str[i] = '\0';
            break;
        }
    }
}

// 信号处理函数
void handleSigint(int sig) {
    exitFlag = 1;
}

// 发送消息的线程处理函数
void *sendMessage(void *arg) {
    char msgBuffer[MAX_BUFFER_SIZE] = {0};
    char outBuffer[MAX_BUFFER_SIZE + 32] = {0};

    while (1) {
        printPrompt();
        if (fgets(msgBuffer, MAX_BUFFER_SIZE, stdin) == NULL)
            continue;

        trimNewline(msgBuffer, MAX_BUFFER_SIZE);

        if (strcmp(msgBuffer, "exit") == 0) {
            break;
        } else {
            sprintf(outBuffer, "%s: %s\n", userName, msgBuffer);
            send(socket_fd, outBuffer, strlen(outBuffer), 0);
        }

        memset(msgBuffer, 0, MAX_BUFFER_SIZE);
        memset(outBuffer, 0, MAX_BUFFER_SIZE + 32);
    }
    handleSigint(2);
    return NULL;
}

// 接收消息的线程处理函数
void *receiveMessage(void *arg) {
    char inBuffer[MAX_BUFFER_SIZE] = {0};

    while (1) {
        int recvLen = recv(socket_fd, inBuffer, MAX_BUFFER_SIZE, 0);
        if (recvLen > 0) {
            printf("%s", inBuffer);
            printPrompt();
        } else if (recvLen == 0) {
            break;
        }
        memset(inBuffer, 0, sizeof(inBuffer));
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *serverIP = "127.0.0.1";
    int serverPort = atoi(argv[1]);

    signal(SIGINT, handleSigint);

    printf("请输入你的名字: ");
    if (fgets(userName, sizeof(userName), stdin) == NULL) {
        fprintf(stderr, "读取名字失败！\n");
        return EXIT_FAILURE;
    }
    trimNewline(userName, strlen(userName));

    if (strlen(userName) < 2 || strlen(userName) > 30) {
        fprintf(stderr, "名字的长度必须在2到30字符之间。\n");
        return EXIT_FAILURE;
    }

    struct sockaddr_in serverAddress;
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("创建套接字失败");
        return EXIT_FAILURE;
    }
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr(serverIP);
    serverAddress.sin_port = htons(serverPort);

    if (connect(socket_fd, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
        perror("连接服务器失败");
        return EXIT_FAILURE;
    }

    // 发送用户名给服务器
    send(socket_fd, userName, sizeof(userName), 0);
    printf("=== 欢迎进入聊天室 ===\n");

    pthread_t sendThread, recvThread;
    if (pthread_create(&sendThread, NULL, sendMessage, NULL) != 0) {
        fprintf(stderr, "创建发送消息线程失败\n");
        return EXIT_FAILURE;
    }
    if (pthread_create(&recvThread, NULL, receiveMessage, NULL) != 0) {
        fprintf(stderr, "创建接收消息线程失败\n");
        return EXIT_FAILURE;
    }

    while (1) {
        if (exitFlag) {
            printf("\nBye\n");
            break;
        }
    }

    close(socket_fd);
    return EXIT_SUCCESS;
}
