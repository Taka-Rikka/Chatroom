#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_BUFFER 2048
#define USERNAME_LEN 32

volatile sig_atomic_t app_active = 1;
int connection_socket = -1;
char username[USERNAME_LEN];

/* 界面显示相关函数 */
void display_prompt() {
    printf("\033[1;36m>> \033[0m");
    fflush(stdout);
}

void sanitize_input(char* str, size_t max_len) {
    for(size_t i = 0; i < max_len; ++i) {
        if(str[i] == '\n' || str[i] == '\r') {
            str[i] = '\0';
            return;
        }
    }
    str[max_len-1] = '\0';  // 确保终止
}

/* 信号处理 */
void handle_signal(int sig_num) {
    app_active = 0;
    close(connection_socket);
    printf("\nTerminating connection...\n");
    exit(EXIT_SUCCESS);
}

/* 消息发送线程 */
void* send_messages(void* arg) {
    char msg_buffer[MAX_BUFFER];
    char formatted_msg[MAX_BUFFER + 64];

    while(app_active) {
        display_prompt();
        if(fgets(msg_buffer, MAX_BUFFER, stdin) == NULL) break;

        sanitize_input(msg_buffer, MAX_BUFFER);

        if(strcasecmp(msg_buffer, "quit") == 0 || 
           strcasecmp(msg_buffer, "exit") == 0) {
            handle_signal(0);
        }

        snprintf(formatted_msg, sizeof(formatted_msg), 
               "[%s]: %s\n", username, msg_buffer);
        
        if(send(connection_socket, formatted_msg, strlen(formatted_msg), 0) < 0) {
            perror("Message send failed");
        }

        memset(msg_buffer, 0, MAX_BUFFER);
        memset(formatted_msg, 0, sizeof(formatted_msg));
    }
    return NULL;
}

/* 消息接收线程 */
void* receive_messages(void* arg) {
    char incoming_msg[MAX_BUFFER];
    
    while(app_active) {
        ssize_t bytes_recv = recv(connection_socket, incoming_msg, MAX_BUFFER-1, 0);
        
        if(bytes_recv > 0) {
            incoming_msg[bytes_recv] = '\0';
            printf("\r%s\n", incoming_msg);
            display_prompt();
        } else if(bytes_recv == 0) {
            printf("\nServer connection lost\n");
            app_active = 0;
            break;
        } else {
            perror("Receive error");
        }
    }
    return NULL;
}

/* 网络连接初始化 */
int establish_connection(const char* server_ip, uint16_t port) {
    struct sockaddr_in server_config;
    
    if((connection_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return -1;
    }

    server_config.sin_family = AF_INET;
    server_config.sin_port = htons(port);
    
    if(inet_pton(AF_INET, server_ip, &server_config.sin_addr) <= 0) {
        perror("Invalid server address");
        return -1;
    }

    if(connect(connection_socket, (struct sockaddr*)&server_config, 
              sizeof(server_config)) < 0) {
        perror("Connection failed");
        return -1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <IP> <PORT>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, handle_signal);

    printf("Enter your nickname: ");
    if(!fgets(username, USERNAME_LEN, stdin)) {
        fprintf(stderr, "Username input error\n");
        exit(EXIT_FAILURE);
    }
    sanitize_input(username, USERNAME_LEN);

    if(strlen(username) < 2 || strlen(username) >= USERNAME_LEN-1) {
        fprintf(stderr, "Invalid username length (2-30 characters)\n");
        exit(EXIT_FAILURE);
    }

    uint16_t port = (uint16_t)atoi(argv[2]);
    if(port < 1024 || port > 65535) {
        fprintf(stderr, "Invalid port number\n");
        exit(EXIT_FAILURE);
    }

    if(establish_connection(argv[1], port) != 0) {
        exit(EXIT_FAILURE);
    }

    send(connection_socket, username, USERNAME_LEN, 0);
    printf("\n=== Connected to chat server ===\n");

    pthread_t send_thread, recv_thread;
    
    if(pthread_create(&send_thread, NULL, send_messages, NULL) ||
       pthread_create(&recv_thread, NULL, receive_messages, NULL)) {
        fprintf(stderr, "Thread creation error\n");
        close(connection_socket);
        exit(EXIT_FAILURE);
    }

    while(app_active) {
        sleep(1);  // 主线程保持活动
    }

    pthread_join(send_thread, NULL);
    pthread_join(recv_thread, NULL);
    close(connection_socket);
    
    return EXIT_SUCCESS;
}