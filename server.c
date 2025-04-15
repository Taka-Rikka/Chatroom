#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS 100
#define BUF_SIZE 2048

static _Atomic unsigned int connected_clients = 0;
static int client_uid_counter = 1;

typedef struct {
    struct sockaddr_in address;
    int socket_fd;
    int client_uid;
    char alias[32];
} ChatClient;

ChatClient *clients[MAX_CLIENTS];
pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;

// 显示控制台提示符
void display_prompt() {
    printf("\r> ");
    fflush(stdout);
}

// 去除字符串中的换行符
void remove_trailing_newline(char *s, int len) {
    for (int i = 0; i < len; ++i) {
        if (s[i] == '\n') {
            s[i] = '\0';
            return;
        }
    }
}

// 显示IP地址（4段形式）
void display_ip(struct sockaddr_in addr) {
    printf("%d.%d.%d.%d", 
           addr.sin_addr.s_addr & 0xFF,
           (addr.sin_addr.s_addr >> 8) & 0xFF,
           (addr.sin_addr.s_addr >> 16) & 0xFF,
           (addr.sin_addr.s_addr >> 24) & 0xFF);
}

// 添加客户端到全局数组
void register_client(ChatClient *client) {
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i]) {
            clients[i] = client;
            break;
        }
    }
    pthread_mutex_unlock(&clients_lock);
}

// 从全局数组中移除指定UID的客户端
void unregister_client(int uid) {
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && (clients[i]->client_uid == uid)) {
            clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&clients_lock);
}

// 将消息广播给所有非发送者的客户端
void broadcastMessage(char *msg, int sender_uid) {
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && (clients[i]->client_uid != sender_uid)) {
            if (send(clients[i]->socket_fd, msg, strlen(msg), 0) < 0) {
                perror("发送消息失败");
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_lock);
}

// 客户端处理线程
void* handle_client(void *arg) {
    char buffer[BUF_SIZE];
    ChatClient *current_client = (ChatClient *)arg;
    int should_disconnect = 0;

    connected_clients++;
    
    // 接收客户端昵称
    if (recv(current_client->socket_fd, current_client->alias, 32, 0) <= 0 ||
        strlen(current_client->alias) < 2) {
        should_disconnect = 1;
    } else {
        char join_notification[BUF_SIZE];
        snprintf(join_notification, BUF_SIZE, "%s joined\n", current_client->alias);
        broadcastMessage(join_notification, current_client->client_uid);
        printf("%s", join_notification);
    }

    while (!should_disconnect) {
        int recvd = recv(current_client->socket_fd, buffer, BUF_SIZE, 0);
        if (recvd > 0) {
            if (strlen(buffer) > 0) {
                broadcastMessage(buffer, current_client->client_uid);
                remove_trailing_newline(buffer, strlen(buffer));
                printf("%s - %s\n", buffer, current_client->alias);
            }
        } else if (recvd == 0 || strcmp(buffer, "exit") == 0) {
            char leave_notification[BUF_SIZE];
            snprintf(leave_notification, BUF_SIZE, "%s left\n", current_client->alias);
            printf("%s", leave_notification);
            broadcastMessage(leave_notification, current_client->client_uid);
            should_disconnect = 1;
        } else {
            perror("接收消息失败");
            should_disconnect = 1;
        }
        memset(buffer, 0, BUF_SIZE);
    }

    close(current_client->socket_fd);
    unregister_client(current_client->client_uid);
    free(current_client);
    connected_clients--;
    pthread_detach(pthread_self());
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    int srv_socket, cli_socket;
    struct sockaddr_in srv_addr, cli_addr;
    socklen_t cli_addr_len;
    pthread_t thread_id;

    srv_socket = socket(AF_INET, SOCK_STREAM, 0);
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons(port);

    // 忽略 SIGPIPE 信号，防止服务器因发送给已关闭连接而崩溃
    signal(SIGPIPE, SIG_IGN);

    int opt = 1;
    setsockopt(srv_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(srv_socket, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("绑定失败");
        exit(EXIT_FAILURE);
    }

    if (listen(srv_socket, 10) < 0) {
        perror("监听失败");
        exit(EXIT_FAILURE);
    }

    printf("=== 聊天服务器已启动 ===\n");

    while (1) {
        cli_addr_len = sizeof(cli_addr);
        cli_socket = accept(srv_socket, (struct sockaddr *)&cli_addr, &cli_addr_len);

        // 超过最大连接数时，拒绝连接
        if (connected_clients >= MAX_CLIENTS) {
            printf("连接已达上限: ");
            display_ip(cli_addr);
            printf(":%d\n", ntohs(cli_addr.sin_port));
            close(cli_socket);
            continue;
        }

        ChatClient *new_client = malloc(sizeof(ChatClient));
        new_client->address = cli_addr;
        new_client->socket_fd = cli_socket;
        new_client->client_uid = client_uid_counter++;

        register_client(new_client);
        pthread_create(&thread_id, NULL, handle_client, (void *)new_client);
    }

    return EXIT_SUCCESS;
}
