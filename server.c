#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define LENGTH_NAME 31
#define LENGTH_MSG 101
#define LENGTH_SEND 201
#define MAX_CLIENTS 10

typedef struct ClientNode {
    int sockfd;
    struct sockaddr_in client_addr;  // Client's address information
    struct ClientNode *next;
    char name[LENGTH_NAME];
} ClientNode;

ClientNode *root = NULL;
int server_sockfd = 0, connected_clients = 0;

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Forward declarations
void broadcast_message(char *s, int sockfd);
void private_message(char *s, int sender_sockfd, int recipient_port);
void catch_ctrl_c_and_exit(int sig);
void *handle_client(void *client_node);

int main() {
    signal(SIGINT, catch_ctrl_c_and_exit);

    int port;
    printf("Enter the port number you want to listen on: ");
    scanf("%d", &port);

    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        perror("Failed to create a socket. Exiting...");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind error. Exiting...");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_sockfd, 5) < 0) {
        perror("Listen error. Exiting...");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    printf("=== WELCOME TO THE CHATROOM SERVER ===\nServer is listening on port %d\n", port);

    while (1) {
        if (connected_clients >= MAX_CLIENTS) {
            printf("Maximum number of clients reached. Waiting for a client to disconnect...\n");
            sleep(1);
            continue;
        }

        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, &client_addr_len);

        if (client_sockfd < 0) {
            perror("Accept error. Continuing...");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Client connected from %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        ClientNode *new_node = (ClientNode *)malloc(sizeof(ClientNode));
        if (!new_node) {
            perror("Failed to allocate memory for new client");
            continue;
        }
        new_node->sockfd = client_sockfd;
        new_node->client_addr = client_addr;  // Save client's address information

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, (void *)new_node) != 0) {
            perror("Thread creation error. Continuing...");
            close(client_sockfd);
            free(new_node);
            continue;
        }

        connected_clients++;
    }

    close(server_sockfd);
    return 0;
}

void broadcast_message(char *s, int sockfd) {
    pthread_mutex_lock(&clients_mutex);

    ClientNode *tmp = root;
    while (tmp != NULL) {
        if (tmp->sockfd != sockfd) { // Send to all clients except the sender
            if (send(tmp->sockfd, s, LENGTH_SEND, 0) == -1) {
                perror("send error");
            }
        }
        tmp = tmp->next;
    }

    pthread_mutex_unlock(&clients_mutex);
}

void private_message(char *s, int sender_sockfd, int recipient_port) {
    pthread_mutex_lock(&clients_mutex);

    ClientNode *tmp = root;
    while (tmp != NULL) {
        if (tmp->sockfd != sender_sockfd && ntohs(tmp->client_addr.sin_port) == recipient_port) {
            char formatted_message[LENGTH_SEND];
            snprintf(formatted_message, LENGTH_SEND, "Private message from %s: %s", s, tmp->name);
            send(tmp->sockfd, formatted_message, strlen(formatted_message), 0);
            break;
        }
        tmp = tmp->next;
    }

    pthread_mutex_unlock(&clients_mutex);
}


void catch_ctrl_c_and_exit(int sig) {
    pthread_mutex_lock(&clients_mutex);

    printf("Server shutting down...\n");

    ClientNode *tmp = root;
    while (tmp != NULL) {
        close(tmp->sockfd);
        tmp = tmp->next;
    }

    ClientNode *temp;
    while (root != NULL) {
        temp = root;
        root = root->next;
        free(temp);
    }

    pthread_mutex_unlock(&clients_mutex);

    close(server_sockfd);
    exit(EXIT_SUCCESS);
}

void *handle_client(void *client_node) {
    ClientNode *current = (ClientNode *)client_node;
    int client_sockfd = current->sockfd;
    struct sockaddr_in client_addr = current->client_addr;

    pthread_mutex_lock(&clients_mutex);

    // Add new client to the list
    current->next = root;
    root = current;

    pthread_mutex_unlock(&clients_mutex);

    char name[LENGTH_NAME];
    recv(client_sockfd, name, LENGTH_NAME, 0);
    snprintf(current->name, LENGTH_NAME, "%s", name);
    printf("Client \"%s\" joined\n", current->name);

    char join_message[LENGTH_SEND];
    snprintf(join_message, LENGTH_SEND, "%s has joined the chat\n", current->name);
    broadcast_message(join_message, current->sockfd);

    char message[LENGTH_MSG];
    while (1) {
        if (recv(client_sockfd, message, LENGTH_MSG, 0) <= 0) {
            printf("Client \"%s\" disconnected\n", current->name);
            char leave_message[LENGTH_SEND];
            snprintf(leave_message, LENGTH_SEND, "%s has left the chat\n", current->name);
            broadcast_message(leave_message, current->sockfd);

            // Remove the client from the linked list
            pthread_mutex_lock(&clients_mutex);
            ClientNode *temp = root;
            while (temp != NULL && temp->next != current) {
                temp = temp->next;
            }
            if (temp != NULL) {
                temp->next = current->next;
            }
            pthread_mutex_unlock(&clients_mutex);

            connected_clients--;
            close(client_sockfd);
            free(current);
            pthread_exit(NULL);
        }

       if (message[0] == '/') {
            // Private message format: "/priv PORT MESSAGE"
            int recipient_port;
            if (sscanf(message + 6, "%d", &recipient_port) == 1) {
                private_message(message + 6, current->sockfd, recipient_port);
            } else {
                printf("Invalid private message format: %s\n", message);
            }
        } else {
            char formatted_message[LENGTH_SEND];
            snprintf(formatted_message, LENGTH_SEND, "%s: %s\n", current->name, message);
            broadcast_message(formatted_message, current->sockfd);
        }
    }
}
