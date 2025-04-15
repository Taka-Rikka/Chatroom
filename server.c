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

#define MAX_CONNECTIONS 100
#define BUFFER_SIZE 2048

static _Atomic unsigned int active_clients = 0;
static int next_uid = 1;

typedef struct {
    struct sockaddr_in addr_info;
    int conn_fd;
    int client_id;
    char nickname[32];
} Client;

Client *client_list[MAX_CONNECTIONS];
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

void clear_stdout() {
    printf("\r> ");
    fflush(stdout);
}

void trim_newline(char* str, int len) {
    for (int i = 0; i < len; ++i) {
        if (str[i] == '\n') {
            str[i] = '\0';
            return;
        }
    }
}

void show_ip(struct sockaddr_in addr) {
    printf("%d.%d.%d.%d",
           addr.sin_addr.s_addr & 0xFF,
           (addr.sin_addr.s_addr >> 8) & 0xFF,
           (addr.sin_addr.s_addr >> 16) & 0xFF,
           (addr.sin_addr.s_addr >> 24) & 0xFF);
}

void add_to_clients(Client *c) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CONNECTIONS; ++i) {
        if (!client_list[i]) {
            client_list[i] = c;
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

void remove_from_clients(int id) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CONNECTIONS; ++i) {
        if (client_list[i] && client_list[i]->client_id == id) {
            client_list[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

void broadcast_msg(char *msg, int sender_id) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CONNECTIONS; ++i) {
        if (client_list[i] && client_list[i]->client_id != sender_id) {
            if (send(client_list[i]->conn_fd, msg, strlen(msg), 0) < 0) {
                perror("Send error");
                break;
            }
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

void* client_handler(void *arg) {
    char msg_buffer[BUFFER_SIZE];
    Client *current = (Client*)arg;
    int disconnect = 0;

    active_clients++;
    
    if (recv(current->conn_fd, current->nickname, 32, 0) <= 0 || 
        strlen(current->nickname) < 2) {
        disconnect = 1;
    } else {
        char join_msg[BUFFER_SIZE];
        snprintf(join_msg, BUFFER_SIZE, "%s joined\n", current->nickname);
        broadcast_msg(join_msg, current->client_id);
        printf("%s", join_msg);
    }

    while (!disconnect) {
        int bytes = recv(current->conn_fd, msg_buffer, BUFFER_SIZE, 0);
        
        if (bytes > 0) {
            if (strlen(msg_buffer) > 0) {
                broadcast_msg(msg_buffer, current->client_id);
                trim_newline(msg_buffer, strlen(msg_buffer));
                printf("%s - %s\n", msg_buffer, current->nickname);
            }
        } else if (bytes == 0 || strcmp(msg_buffer, "exit") == 0) {
            char leave_msg[BUFFER_SIZE];
            snprintf(leave_msg, BUFFER_SIZE, "%s left\n", current->nickname);
            printf("%s", leave_msg);
            broadcast_msg(leave_msg, current->client_id);
            disconnect = 1;
        } else {
            perror("Receive error");
            disconnect = 1;
        }
        
        memset(msg_buffer, 0, BUFFER_SIZE);
    }

    close(current->conn_fd);
    remove_from_clients(current->client_id);
    free(current);
    active_clients--;
    pthread_detach(pthread_self());
    return NULL;
}

int main(int argc, char ​**​argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len;
    pthread_t tid;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    signal(SIGPIPE, SIG_IGN);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("=== CHAT SERVER STARTED ===\n");

    while (1) {
        addr_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);

        if (active_clients >= MAX_CONNECTIONS) {
            printf("Connection limit reached: ");
            show_ip(client_addr);
            printf(":%d\n", ntohs(client_addr.sin_port));
            close(client_fd);
            continue;
        }

        Client *new_client = malloc(sizeof(Client));
        new_client->addr_info = client_addr;
        new_client->conn_fd = client_fd;
        new_client->client_id = next_uid++;

        add_to_clients(new_client);
        pthread_create(&tid, NULL, client_handler, (void*)new_client);
    }

    return EXIT_SUCCESS;
}