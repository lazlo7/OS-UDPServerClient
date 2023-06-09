#pragma once

#include <unistd.h>

#define DEFAULT_IP "127.0.0.1"

#define CLIENT_TYPE_STEALER 'S'
#define CLIENT_TYPE_LOADER 'L'
#define CLIENT_TYPE_OBSERVER 'O'

#define MAX_ITEM_COUNT 1024
#define MIN_ITEM_RANDOM_COUNT 1
#define MAX_ITEM_RANDOM_COUNT 20

#define MIN_ITEM_RANDOM_PRICE 1
#define MAX_ITEM_RANDOM_PRICE 10000

#define MIN_RANDOM_DELAY 1
#define MAX_RANDOM_DELAY 5

// Helper function: closes the file descriptor only if it's non-negative
// and then sets the fd to -1.
void closeFd(int* fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}
