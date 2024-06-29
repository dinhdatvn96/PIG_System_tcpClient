#include <stdio.h>
#include <stdlib.h>
#include "tcpClient.h"



// prototype
static void System_Config(void);

// extern variables
extern sem_t data_ready;

extern unsigned int read_index ;
extern unsigned int write_index ;

extern uint64_t total_received ;
extern uint64_t total_written ;
extern int file_number;

//global variables
pthread_t reader_thread, writer_thread;
int client_fd;


int main()
{
    //Create the files first
    Create_Files();

    // config the thread, sem, ethernet...
    System_Config();

    // Wait for threads to finish
    pthread_join(reader_thread, NULL);
    pthread_join(writer_thread, NULL);

    printf("Total arrays received: %lu\n", total_received);
    printf("Total arrays written: %lu\n", total_written);
    printf("--------------------------------------\n");

    uint64_t total_written_infiles = (file_number + 1) * BYTES_ONE_FILES;
    if(total_received == total_written_infiles)
        printf("--- Succeed!!! Congratulations =)) ---\n");
    else
        printf("---      Fail!!!  STUPID!!!        ---\n");

    printf("---------------------------------------\n");

    getchar();
    return 0;
}

static void System_Config()
{
    struct sockaddr_in server_addr;

    // Create socket
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Initialize server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
    {
        perror("invalid address or address not supported");
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("connection failed");
        close(client_fd);
        exit(EXIT_FAILURE);
    }
    printf("Connected to server %s on port %d...\n", SERVER_IP, PORT);

    // Config the semaphore
    sem_init(&data_ready, 0, 0);

    // Initialize thread attributes
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    // Set scheduling policy (example: SCHED_RR)
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

    // Set specific priority within the scheduling policy (optional)
    struct sched_param param;

    param.sched_priority = 4;
    pthread_attr_setschedparam(&attr, &param);
    // Create threads
    if (pthread_create(&reader_thread, &attr, receive_data, (void *)&client_fd) != 0)
    {
        perror("pthread_create failed for reader_thread");
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    param.sched_priority = 2;
    pthread_attr_setschedparam(&attr, &param);

    if (pthread_create(&writer_thread, &attr, write_data, NULL) != 0)
    {
        perror("pthread_create failed for writer_thread");
        close(client_fd);
        exit(EXIT_FAILURE);
    }
}



