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


//#define SERVER_IP "210.125.111.23" // Server IP address
#define SERVER_IP "210.125.124.23" // Server IP address
#define PORT 5001
#define BUFFER_SIZE 2000   // Assuming each int is 4 bytes and we're receiving 1000 ints
#define MAX_BATCHES 10000    // Maximum number of batches in the circular buffer

typedef struct
{
    char data[BUFFER_SIZE];
    int size;
} DataBatch;

pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
//pthread_cond_t buffer_cond = PTHREAD_COND_INITIALIZER;

sem_t data_ready;  // Semaphore for empty slots in the buffer

DataBatch batches_rx[MAX_BATCHES];
DataBatch batches_tx[MAX_BATCHES];

unsigned int read_index = 0;
unsigned int write_index = 0;

unsigned int total_received = 0;
unsigned int total_written = 0;

volatile int data_available = 0;

volatile unsigned int rx_done = 0;
volatile unsigned int tx_done = 0;

void *receive_data(void *arg);
void *write_data(void *arg);

int main()
{
    int client_fd;
    struct sockaddr_in server_addr;
    pthread_t reader_thread, writer_thread;

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
    sem_init(&data_ready, 0, 0);

    // Wait for threads to finish
    pthread_join(reader_thread, NULL);
    pthread_join(writer_thread, NULL);

    close(client_fd);
    printf("Total arrays received: %u\n", total_received);
    printf("Total arrays written: %u\n", total_written);
    printf("--------------------------------------\n");

    if(total_received== total_written)
        printf("--- Succeed!!! Congratulations =)) ---\n");
    else
        printf("---      Fail!!!  STUPID!!!        ---\n");

    printf("---------------------------------------\n");

    getchar();
    return 0;
}

void *receive_data(void *arg)
{
    int client_fd = *((int *)arg);
    ssize_t bytes_read;
    int retry_count = 0;
    const int max_retries = 5;
    const int timeout_sec = 5;

    // Set the socket to non-blocking mode
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    int select_result = 0;
    struct timeval timeout;

    while (1)
    {
//        pthread_mutex_lock(&buffer_mutex);
//        while (data_available == MAX_BATCHES)
//        {
//            pthread_cond_wait(&buffer_cond, &buffer_mutex);
//        }
//        pthread_mutex_unlock(&buffer_mutex);

        fd_set read_fds;

        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);
        timeout.tv_sec = timeout_sec;
        timeout.tv_usec = 0;

        select_result = select(client_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (select_result == -1)
        {
            perror("select failed");
            break;
        }
        else if (select_result == 0)
        {
            // Timeout occurred
            if (++retry_count > max_retries)
            {
                printf("Max retries reached, terminating connection.\n");
                break;
            }
            continue;
        }

        //bytes_read = recv(client_fd, batches_rx[read_index].data, BUFFER_SIZE,0);
        bytes_read = read(client_fd, batches_rx[read_index].data, BUFFER_SIZE);

        if (bytes_read <= 0)
        {
            if (bytes_read == -1)
            {
                perror("read failed");
            }
            break;
        }

        //pthread_mutex_lock(&buffer_mutex);
        if (read_index >= MAX_BATCHES)
        {
            printf("Error: write_index out of bounds: %d\n", read_index);
            pthread_mutex_unlock(&buffer_mutex);
            break;
        }

        batches_rx[read_index].size = bytes_read;
        memcpy(batches_tx[read_index].data, batches_rx[read_index].data, batches_rx[read_index].size);
        batches_tx[read_index].size = batches_rx[read_index].size;

        //data_available++;
        //pthread_mutex_unlock(&buffer_mutex);
        total_received += batches_rx[read_index].size;
        if(total_received > 4000000000)
            total_received -= 4000000000;

        //pthread_cond_signal(&buffer_cond);
        rx_done++;
        read_index = (read_index + 1) % MAX_BATCHES;
        sem_post(&data_ready);

//        if(total_received >= TOTAL_BYTES)
//            break;

        retry_count = 0; // Reset retry count after successful read
    }

    pthread_mutex_lock(&buffer_mutex);
    data_available = -1; // Indicate end of data
    pthread_mutex_unlock(&buffer_mutex);
    //pthread_cond_signal(&buffer_cond);
    //sem_post(&data_ready);

    close(client_fd);
    pthread_exit(NULL);
}

void *write_data(void *arg)
{
    // Open the file for writing in binary mode ("wb")
    int fd = open("/home/pigsystem/Desktop/storage/all_data.bin", O_WRONLY | O_CREAT, 0644);
    if (fd == -1)
    {
        perror("open failed");
        exit(1);
    }

    char *data = 0;
    unsigned int data_size = 0;
    unsigned int written_now, bytes_written;

    while (1)
    {
        if (data_available <= -1 && read_index == write_index)
        {
            break; // End of data
        }
        // wait for the data from server
        sem_wait(&data_ready);

        if (write_index >= MAX_BATCHES)
        {
            printf("Error: read_index out of bounds: %d\n", write_index);
            pthread_mutex_unlock(&buffer_mutex);
            break;
        }

        pthread_mutex_lock(&buffer_mutex);
        data = batches_tx[write_index].data;
        data_size = batches_tx[write_index].size;
        pthread_mutex_unlock(&buffer_mutex);

        bytes_written = 0;
        while (bytes_written < data_size)
        {
            written_now = write(fd, &data[bytes_written], data_size - bytes_written);
            if (written_now == -1)
            {
                // Check for EAGAIN error (buffer full in non-blocking mode)
                if (errno == EAGAIN)
                {
                    printf("Write buffer full, retrying later...\n");
                }
                else
                {
                    perror("write failed");
                    close(fd);
                    exit(1);
                }
            }
            else
            {
                bytes_written += written_now;
            }
        }

        total_written += bytes_written;

        if(total_written > 4000000000)
            total_written -= 4000000000;

        write_index = (write_index + 1) % MAX_BATCHES;
        tx_done++;
    }

    close(fd);
    pthread_exit(NULL);
}
