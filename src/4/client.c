#include "../common.h"
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

int server_sock = -1;

void cleanup(void)
{
    closeFd(&server_sock);
}

void printUsage(char const* cmd)
{
    printf("Usage: %s <server_port> [<server_ip>]\n", cmd);
    printf("By default, <server_ip> = %s\n", DEFAULT_IP);
}

void onInterruptReceived(int signum)
{
    (void)signum;
    printf("[Client] Received SIGINT, cleaning up resources...\n");
    cleanup();
    printf("[Client] Exit.\n");
    exit(0);
}

void onSigPipeReceived(int signum)
{
    (void)signum;
    printf("[Client] Lost connection to the server, cleaning up resources...\n");
    cleanup();
    printf("[Client] Exit.\n");
    exit(0);
}

int getRandomNumber(int from, int to)
{
    return rand() % (to - from + 1) + from;
}

// item_prices should be at least of size MAX_ITEM_COUNT.
int generateItems(int* items)
{
    int const item_count = getRandomNumber(MIN_ITEM_RANDOM_COUNT, MAX_ITEM_RANDOM_COUNT);
    for (int i = 0; i < item_count; i++) {
        items[i] = getRandomNumber(MIN_ITEM_RANDOM_PRICE, MAX_ITEM_RANDOM_PRICE);
    }
    return item_count;
}

void emulateActivity(void)
{
    sleep(getRandomNumber(MIN_RANDOM_DELAY, MAX_RANDOM_DELAY));
}

void emulateStealer(void)
{
    printf("[Stealer] Started as Stealer!\n");

    // Entering item prices.
    printf("[Stealer] Enter the number of items in the warehouse (max: %d, non-positive to randomize items): ", MAX_ITEM_COUNT);
    int item_count = 0;
    scanf("%d", &item_count);

    if (item_count > MAX_ITEM_COUNT) {
        item_count = MAX_ITEM_COUNT;
    }

    int items[MAX_ITEM_COUNT];
    if (item_count <= 0) {
        item_count = generateItems(items);
        printf("Generated items:\n");
        for (int i = 0; i < item_count; ++i) {
            printf("%d", items[i]);
            if (i + 1 != item_count) {
                printf(", ");
            }
        }
        printf("\n");
    } else {
        printf("[Stealer] Enter %d item prices:\n", item_count);
        for (int i = 0; i < item_count; ++i) {
            printf("[Stealer] %d: ", i + 1);
            int item_price = 0;
            scanf("%d", &item_price);

            // Fool-proofing.
            if (item_price < 0) {
                --i;
                continue;
            }

            items[i] = item_price;
        }
    }

    int total_price_sum = 0;
    for (int i = 0; i < item_count; ++i) {
        total_price_sum += items[i];
    }

    printf("[Stealer] Military intel: %d items totalling %d rubles\n", item_count, total_price_sum);
    printf("[Stealer] Waiting for server to notify us to start...\n");

    // Wait for server to start.
    char buffer_char;
    if (recv(server_sock, &buffer_char, sizeof(buffer_char), 0) == -1) {
        printf("[Stealer Error] Failed to receive start notification from server: %s\n", strerror(errno));
        return;
    }

    for (int i = 0; i < item_count; ++i) {
        printf("[Stealer] Stealing a new item (%d item(s) left)...\n", item_count - i);
        // Emulate stealing process.
        emulateActivity();

        int const stolen_item = items[i];
        printf("[Stealer] Stolen a new item with price: %d\n", stolen_item);
        // Send server the price of a new item.
        // Additionally, this notifies server to unblock loader.
        if (send(server_sock, &stolen_item, sizeof(stolen_item), 0) == -1) {
            printf("[Stealer Error] Failed to send the price of a new item to server: %s\n", strerror(errno));
            return;
        }

        // Block until server notifies us that loader has received the item.
        if (recv(server_sock, &buffer_char, sizeof(buffer_char), 0) == -1) {
            printf("[Stealer Error] Failed to receive loader-received notification from server: %s\n", strerror(errno));
            return;
        }

        printf("[Stealer] Handed over the item to Loader!\n");
    }

    // Notify Loader that there are no more items to steal.
    // We do that by sending an item with a negative price.
    int negative_price = -1;
    if (send(server_sock, &negative_price, sizeof(negative_price), 0) == -1) {
        printf("[Stealer Error] Failed to send ending notification to server: %s\n", strerror(errno));
        return;
    }

    printf("[Stealer] Finished!\n");
}

void emulateLoader(void)
{
    printf("[Loader] Started as Loader!\n");

    int item_price = 0;
    char buffer_char = 0;

    for (;;) {
        printf("[Loader] Waiting for a new item from Stealer...\n");
        // Receive a new item price from server.
        // Additionally, recv() will block until the data is received,
        // acting as a synchronisation mechanism.
        if (recv(server_sock, &item_price, sizeof(item_price), 0) == -1) {
            printf("[Loader Error] Failed to receive a new item price from server: %s\n", strerror(errno));
            return;
        }

        // Notify server that we got the item.
        if (send(server_sock, &buffer_char, sizeof(buffer_char), 0) == -1) {
            printf("[Loader Error] Failed to send new item acknowledgement to server: %s\n", strerror(errno));
            return;
        }

        printf("[Loader] Received a new item from Stealer, loading it...\n");
        // Emulate loading process.
        emulateActivity();

        // Send the item price to server.
        if (send(server_sock, &item_price, sizeof(item_price), 0) == -1) {
            printf("[Loader Error] Failed to send item price to server: %s\n", strerror(errno));
            return;
        }

        // Break the loop on negative item price.
        if (item_price < 0) {
            printf("[Loader] No more items to load, exiting\n");
            break;
        }

        printf("[Loader] Loaded a new item!\n");
    }

    printf("[Loader] Finished!\n");
}

void emulateObserver(void)
{
    printf("[Observer] Started as Observer!\n");

    int item_price = 0;
    int total_items_price = 0;

    for (int item_count = 1;; ++item_count) {
        printf("[Observer] Standing by, looking out for officers nearby...\n");

        // Receive a new item price from server.
        if (recv(server_sock, &item_price, sizeof(item_price), 0) == -1) {
            printf("[Observer] Failed to receive a new item price from server: %s\n", strerror(errno));
            return;
        }

        // Break the loop if the new item price is negative
        if (item_price < 0) {
            printf("[Observer] No more items to observe, exiting\n");
            break;
        }

        // Emulate price calculation process.
        emulateActivity();

        total_items_price += item_price;

        printf("[Observer] A new item! +%d rubles, total: %d rubles for %d items stolen\n",
            item_price, total_items_price, item_count);
    }

    printf("[Observer] Finished!\n");
}

int main(int argc, char const** argv)
{
    srand(time(NULL));

    // Register SIGINT handler.
    if (signal(SIGINT, onInterruptReceived) == SIG_ERR) {
        printf("[Client Error] Failed to register SIGINT handler: %s\n", strerror(errno));
        return 1;
    }

    // Register SIGPIPE handler.
    // SIGPIPE is sent when a socket's connection is lost.
    if (signal(SIGPIPE, onSigPipeReceived) == SIG_ERR) {
        printf("[Client Error] Failed to register SIGPIPE handler: %s\n", strerror(errno));
        return 1;
    }

    // Parse args.
    if (argc < 2) {
        printUsage(argv[0]);
        printf("[Client Error] Missing required argument: <server_port>\n");
        return 1;
    }

    if (argc > 3) {
        printUsage(argv[0]);
        printf("[Client Error] Too many arguments: %d\n", argc);
        return 1;
    }

    int server_port;
    sscanf(argv[1], "%d", &server_port);
    char const* server_ip = argc == 3 ? argv[2] : DEFAULT_IP;

    // Create socket.
    server_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == -1) {
        printf("[Client Error] Failed to create socket: %s\n", strerror(errno));
        return 1;
    }

    // Construct address structure.
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(server_port);

    // Connect to the server.
    if (connect(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        printf("[Client Error] Failed to connect to %s:%d: %s\n", server_ip, server_port, strerror(errno));
        cleanup();
        return 1;
    }

    printf("[Client] Started on %s:%d\n", server_ip, server_port);
    printf("[Client] Waiting for server to assign us a role...\n");

    // Receive role from server.
    char role;
    if (recv(server_sock, &role, sizeof(role), 0) == -1) {
        printf("[Client Error] Failed to receive role from server: %s\n", strerror(errno));
        cleanup();
        return 1;
    }

    switch (role) {
    case CLIENT_TYPE_STEALER:
        emulateStealer();
        break;
    case CLIENT_TYPE_LOADER:
        emulateLoader();
        break;
    case CLIENT_TYPE_OBSERVER:
        emulateObserver();
        break;
    default:
        printf("[Client Error] Server sent us an unknown role: '%c' (available: '%c', '%c' and '%c')\n",
            role, CLIENT_TYPE_STEALER, CLIENT_TYPE_LOADER, CLIENT_TYPE_OBSERVER);
        cleanup();
        return 1;
    }

    printf("[Client] Cleaning up resources...\n");
    cleanup();
    printf("[Client] Exit.\n");

    return 0;
}
