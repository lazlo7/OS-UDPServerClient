#include "../common.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

// 3 clients - Stealer, Loader, Observer
#define MAX_CLIENTS 3

int server_sock = -1;

// Performs a cleanup of all resources.
void cleanup(void)
{
    closeFd(&server_sock);
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

// Returns 0 on success, -1 on error.
int acceptConnection(char const* client_name, char role, struct sockaddr_in* client_addr, socklen_t* client_addr_len)
{
    printf("[Server] Waiting for %s to connect...\n", client_name);

    char buffer = '\0';    
    if (recvfrom(server_sock, &buffer, sizeof(buffer), 0, (struct sockaddr*)client_addr, client_addr_len) == -1) {
        printf("[Server Error] Failed to accept %s's connection: %s\n", client_name, strerror(errno));
        return -1;
    }

    // Assigning client a role.
    if (sendto(server_sock, &role, sizeof(role), 0, (struct sockaddr*)client_addr, *client_addr_len) == -1) {
        printf("[Server Error] Failed to assign client a role: %s\n", strerror(errno));
        return -1;
    }

    return 0;
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
    server_sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
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

    printf("[Server] Started on %s:%d\n", server_ip, server_port);

    struct sockaddr_in stealer_addr, loader_addr, observer_addr;
    socklen_t stealer_addr_len = sizeof(stealer_addr);
    socklen_t loader_addr_len = sizeof(loader_addr);
    socklen_t observer_addr_len = sizeof(observer_addr_len);

    // Accept Stealer, Loader, Observer (in this particular order).
    if (acceptConnection("Stealer", CLIENT_TYPE_STEALER, &stealer_addr, &stealer_addr_len) == -1
        || acceptConnection("Loader", CLIENT_TYPE_LOADER, &loader_addr, &loader_addr_len) == -1
        || acceptConnection("Observer", CLIENT_TYPE_OBSERVER, &observer_addr, &observer_addr_len) == -1) {
        printf("[Server Error] Failed to accept client's connection\n");
        cleanup();
        return 1;
    }

    // Send a byte to Stealer to start.
    char buffer_char = '\0';
    if (sendto(server_sock, &buffer_char, sizeof(buffer_char), 0, (struct sockaddr*)&stealer_addr, stealer_addr_len) == -1) {
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
        if (recvfrom(server_sock, &item_price, sizeof(item_price), 0, (struct sockaddr*)&stealer_addr, &stealer_addr_len) == -1) {
            printf("[Server Error] Failed to receive item price from Stealer: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        printf("[Server] Received data from Stealer, sending it Loader...\n");

        // Send the data to Loader.
        if (sendto(server_sock, &item_price, sizeof(item_price), 0, (struct sockaddr*)&loader_addr, loader_addr_len) == -1) {
            printf("[Server Error] Failed to send item price to Loader: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        // Receive acknowledgement from Loader.
        if (recvfrom(server_sock, &buffer_char, sizeof(buffer_char), 0, (struct sockaddr*)&loader_addr, &loader_addr_len) == -1) {
            printf("[Server Error] Failed to receive item acknowledgement from Loader: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        // Unblock Stealer after handing over the item only after
        // we received an item acknowledgement from Loader.
        if (sendto(server_sock, &buffer_char, sizeof(buffer_char), 0, (struct sockaddr*)&stealer_addr, stealer_addr_len) == -1) {
            printf("[Server Error] Failed to send unblocking notification to Stealer: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        printf("[Server] Sent data to Loader, receiving data back from Loader...\n");

        // Receive the data from Loader.
        if (recvfrom(server_sock, &item_price, sizeof(item_price), 0, (struct sockaddr*)&loader_addr, &loader_addr_len) == -1) {
            printf("[Server Error] Failed to receive item price from Loader: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        printf("[Server] Received data from Loader, sending data to Observer...\n");

        // Send the data to Observer.
        if (sendto(server_sock, &item_price, sizeof(item_price), 0, (struct sockaddr*)&observer_addr, observer_addr_len) == -1) {
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
