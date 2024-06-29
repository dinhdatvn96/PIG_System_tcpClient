#ifndef TCPCLIENT_H_INCLUDED
#define TCPCLIENT_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>

#define SERVER_IP "210.125.124.23" // Server IP address
#define PORT 5001

#define BUFFER_SIZE 2000     // Assuming each int is 4 bytes and we're receiving 1000 ints
#define MAX_BATCHES 10000    // Maximum number of batches in the circular buffer

#define NUMBER_FILES 100        // 100 files for storage
#define MAX_PATH 100            // Adjust this if your folder paths are longer

#define BYTES_ONE_FILES 4000000000

typedef struct
{
    char data[BUFFER_SIZE];
    int size;
} DataBatch;

void *receive_data(void *arg);
void *write_data(void *arg);

void Create_Files (void);

#endif // TCPCLIENT_H_INCLUDED
