#include "../common.h"
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

// 3 clients - Stealer, Loader, Observer
#define MAX_CLIENTS 3

int server_sock = -1;
int stealer_sock = -1;
int loader_sock = -1;
int observer_sock = -1;

// Performs a cleanup of all resources.
void cleanup(void)
{
    closeFd(&server_sock);
    closeFd(&stealer_sock);
    closeFd(&loader_sock);
    closeFd(&observer_sock);
}

void onInterruptReceived(int signum)
{
    (void)signum;
    printf("[Server] Received SIGINT, cleaning up resources...\n");
    cleanup();
    printf("[Server] Exit.\n");
    exit(0);
}

void onSigPipeReceived(int signum)
{
    (void)signum;
    printf("[Server] Lost connection to a client, cleaning up resources...\n");
    cleanup();
    printf("[Server] Exit.\n");
    exit(0);
}

void printUsage(char const* cmd)
{
    printf("Usage: %s <server_port> [<server_ip>]\n", cmd);
    printf("By default, <server_ip> = %s\n", DEFAULT_IP);
}

// Returns socket fd of the accepted connection.
// On error, returns -1.
int acceptConnection(int sock, char const* client_name, char role)
{
    printf("[Server] Waiting for %s to connect...\n", client_name);

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    int const client_sock = accept(sock, (struct sockaddr*)&client_addr, &client_addr_len);
    if (client_sock == -1) {
        printf("[Server Error] Failed to accept %s's connection: %s\n", client_name, strerror(errno));
        return -1;
    }

    // Assigning client a role.
    if (send(client_sock, &role, sizeof(role), 0) == -1) {
        printf("[Server Error] Failed to assign client a role: %s\n", strerror(errno));
        close(client_sock);
        return -1;
    }

    return client_sock;
}

int main(int argc, char const** argv)
{
    // Register SIGINT handler.
    if (signal(SIGINT, onInterruptReceived) == SIG_ERR) {
        printf("[Server Error] Failed to register SIGINT handler: %s\n", strerror(errno));
        return 1;
    }

    if (signal(SIGPIPE, onSigPipeReceived) == SIG_ERR) {
        printf("[Server Error] Failed to register SIGPIPE handler: %s\n", strerror(errno));
        return 1;
    }

    // Parse args.
    if (argc < 2) {
        printUsage(argv[0]);
        printf("[Server Error] Missing required argument: <server_port>\n");
        return 1;
    }

    if (argc > 3) {
        printUsage(argv[0]);
        printf("[Server Error] Too many arguments: %d\n", argc);
        return 1;
    }

    int server_port;
    sscanf(argv[1], "%d", &server_port);

    char const* server_ip = argc == 3 ? argv[2] : DEFAULT_IP;

    // Create socket.
    int const server_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == -1) {
        printf("[Server Error] Failed to create socket: %s\n", strerror(errno));
        return 1;
    }

    // Construct address structure.
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(server_port);

    // Bind to the port.
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        printf("[Server Error] Failed to bind to port %d: %s\n", server_port, strerror(errno));
        cleanup();
        return 1;
    }

    // Listen for incoming connections.
    if (listen(server_sock, MAX_CLIENTS) == -1) {
        printf("[Server Error] Failed to listen on port %d: %s\n", server_port, strerror(errno));
        cleanup();
        return 1;
    }

    printf("[Server] Started on %s:%d\n", server_ip, server_port);

    // Accept Stealer, Loader, Observer (in this particular order).
    if ((stealer_sock = acceptConnection(server_sock, "Stealer", CLIENT_TYPE_STEALER)) == -1
        || (loader_sock = acceptConnection(server_sock, "Loader", CLIENT_TYPE_LOADER)) == -1
        || (observer_sock = acceptConnection(server_sock, "Observer", CLIENT_TYPE_OBSERVER)) == -1) {
        printf("[Server Error] Failed to accept client's connection\n");
        cleanup();
        return 1;
    }

    // Send a byte to Stealer to start.
    char buffer_char = '\0';
    if (send(stealer_sock, &buffer_char, sizeof(buffer_char), 0) == -1) {
        printf("[Server Error] Failed to send starting notification to Stealer: %s\n", strerror(errno));
        cleanup();
        return 1;
    }

    // Handle clients.
    int item_price = 0;
    int exit_code = 0;

    while (item_price >= 0) {
        printf("[Server] Waiting for data from Stealer...\n");

        // Receive data from Stealer.
        if (recv(stealer_sock, &item_price, sizeof(item_price), 0) == -1) {
            printf("[Server Error] Failed to receive item price from Stealer: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        printf("[Server] Received data from Stealer, sending it Loader...\n");

        // Send the data to Loader.
        if (send(loader_sock, &item_price, sizeof(item_price), 0) == -1) {
            printf("[Server Error] Failed to send item price to Loader: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        // Receive acknowledgement from Loader.
        if (recv(loader_sock, &buffer_char, sizeof(buffer_char), 0) == -1) {
            printf("[Server Error] Failed to receive item acknowledgement from Loader: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        // Unblock Stealer after handing over the item only after
        // we received an item acknowledgement from Loader.
        if (send(stealer_sock, &buffer_char, sizeof(buffer_char), 0) == -1) {
            printf("[Server Error] Failed to send unblocking notification to Stealer: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        printf("[Server] Sent data to Loader, receiving data back from Loader...\n");

        // Receive the data from Loader.
        if (recv(loader_sock, &item_price, sizeof(item_price), 0) == -1) {
            printf("[Server Error] Failed to receive item price from Loader: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        printf("[Server] Received data from Loader, sending data to Observer...\n");

        // Send the data to Observer.
        if (send(observer_sock, &item_price, sizeof(item_price), 0) == -1) {
            printf("[Server Error] Failed to send item price to Observer: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        printf("[Server] Sent data to Observer!\n");
    }

    // Clean up resources.
    printf("[Server] Cleaning up resources...\n");
    cleanup();

    if (exit_code == 0) {
        printf("[Server] Finished!\n");
    }

    return exit_code;
}
