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
#include <netinet/in.h>


#define LENGTH_NAME 30
#define LENGTH_SEND 600
#define LENGTH_MSG  500  // Reduced to accommodate additional data in LENGTH_SEND

volatile sig_atomic_t flag = 0;
int sockfd = 0;
char nickname[LENGTH_NAME] = {};

void str_overwrite_stdout() {
    printf("\033[0K");
    fflush(stdout);
}

void str_trim_lf(char* arr, int length) {
    for (int i = 0; i < length; i++) {
        if (arr[i] == '\n') {
            arr[i] = '\0';
            break;
        }
    }
}

void catch_ctrl_c_and_exit(int sig) {
    flag = 1;
}

void send_msg_handler() {
    char message[LENGTH_MSG] = {};
    while (1) {
        printf("\nChoose an option:\n1: Send a private message\n2: Send a broadcast message\nType 'exit' to quit\nYour choice: ");
        fgets(message, LENGTH_MSG, stdin);  // Read the user's choice
        str_trim_lf(message, LENGTH_MSG);

        if (strcmp(message, "1") == 0) {
            int recipient_port;
            char private_message[LENGTH_MSG];

            printf("Enter the recipient's port: ");
            scanf("%d", &recipient_port);  // Read the recipient's port
            getchar();  // Consume the newline character
            printf("Enter your private message: ");
            fgets(private_message, LENGTH_MSG, stdin);  // Read the private message
            str_trim_lf(private_message, LENGTH_MSG);

            // Calculate the required length for the formatted message
            int required_length = snprintf(NULL, 0, "/priv %d %s", recipient_port, private_message) + 1;  // +1 for null terminator

            if (required_length <= LENGTH_SEND) {
                // Check if the formatted message fits into the buffer
                char formatted_msg[LENGTH_SEND];
                snprintf(formatted_msg, LENGTH_SEND, "/priv %d %s", recipient_port, private_message);
                send(sockfd, formatted_msg, LENGTH_SEND, 0);
            } else {
                // Handle the case where the message is too long
                printf("Message is too long and will be truncated. Please shorten it.\n");
            }
        } else if (strcmp(message, "2") == 0) {
            printf("%s :> ", nickname);
            fgets(message, LENGTH_MSG, stdin);  // Read the broadcast message
            str_trim_lf(message, LENGTH_MSG);
            send(sockfd, message, LENGTH_MSG, 0);  // Send the broadcast message to the server
        } else if (strcmp(message, "exit") == 0) {
            catch_ctrl_c_and_exit(2);  // Handle exit
            break;
        } else {
            printf("Invalid option, please try again.\n");
        }
    }
}

void recv_msg_handler() {
    char receiveMessage[LENGTH_SEND] = {};
    while (1) {
        int receive = recv(sockfd, receiveMessage, LENGTH_SEND, 0);
        if (receive > 0) {
            // Received a new message
            printf("\nReceived: %s\n", receiveMessage);  // Print the received message
            printf("%s :> ", nickname);  // Re-print the prompt for a smoother user experience
            fflush(stdout);  // Ensure the prompt is displayed immediately
        } else if (receive == 0) {
            // The server has closed the connection
            printf("\nServer has disconnected.\n");
            exit(EXIT_SUCCESS);  // Exit the program as the server has closed the connection
        } else {
            // An error occurred
            perror("Failed to receive message");
            exit(EXIT_FAILURE);  // Exit the program due to the error
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, catch_ctrl_c_and_exit);

    printf("Welcome to the Chat Client!\n");
    printf("Please enter your name: ");
    if (fgets(nickname, LENGTH_NAME, stdin) != NULL) {
        str_trim_lf(nickname, LENGTH_NAME);
    }
    if (strlen(nickname) < 2 || strlen(nickname) >= LENGTH_NAME-1) {
        printf("\nName must be more than one and less than thirty characters.\n");
        exit(EXIT_FAILURE);
    }

    char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Fail to create a socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_info;
    memset(&server_info, 0, sizeof(server_info));
    server_info.sin_family = PF_INET;
    server_info.sin_addr.s_addr = inet_addr(server_ip);
    server_info.sin_port = htons(server_port);

    int err = connect(sockfd, (struct sockaddr *)&server_info, sizeof(server_info));
    if (err == -1) {
        perror("Connection to Server error");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server_info_actual;
socklen_t addrlen = sizeof(server_info_actual);

if (getpeername(sockfd, (struct sockaddr*)&server_info_actual, &addrlen) == 0) {
    printf("Connected to Server: %s:%d\n", inet_ntoa(server_info_actual.sin_addr), ntohs(server_info_actual.sin_port));
} else {
    perror("getpeername failed");
    exit(EXIT_FAILURE);
}
 struct sockaddr_in client_info;
    socklen_t client_addrlen = sizeof(client_info);

    if (getsockname(sockfd, (struct sockaddr *)&client_info, &client_addrlen) == 0) {
        printf("Client is bound to local port: %d\n", ntohs(client_info.sin_port));
    } else {
        perror("getsockname failed");
        exit(EXIT_FAILURE);
    }

    // printf("Connected to Server: %s:%d\n", inet_ntoa(server_info.sin_addr), ntohs(server_info.sin_port));
    // send(sockfd, nickname, LENGTH_NAME, 0);

    pthread_t send_msg_thread;
    if (pthread_create(&send_msg_thread, NULL, (void *)send_msg_handler, NULL) != 0) {
        perror("Fail to create pthread");
        exit(EXIT_FAILURE);
    }

    char receiveMessage[LENGTH_SEND] = {};
    while (1) {
        if (flag) {
            printf("\nBye\n");
            break;
        }

        int receive = recv(sockfd, receiveMessage, LENGTH_SEND, 0);
        if (receive > 0) {
            printf("\nReceived message: %s\n", receiveMessage);
            printf("enter the option 1 or 2\n");
            printf("%s :> ", nickname);  // Prompt for next message
            fflush(stdout);
        } else if (receive == 0) {
            printf("\nServer has disconnected.\n");
            break;
        } else {
            perror("Receive failed");
            break;
        }
    }

    close(sockfd);
    return 0;
}
