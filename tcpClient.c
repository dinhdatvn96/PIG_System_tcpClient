#include "tcpClient.h"

static void Add_Leading_Zeros(char *number, int total_digits);


pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t data_ready;

DataBatch batches_rx[MAX_BATCHES];
DataBatch batches_tx[MAX_BATCHES];

unsigned int read_index = 0;
unsigned int write_index = 0;

uint64_t total_received = 0;
uint64_t total_written = 0;

volatile int data_available = 0;

volatile unsigned int rx_done = 0;
volatile unsigned int tx_done = 0;

// store the filenames
char **FileNames;
int file_number;

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
                printf("Read failed\n");
            }
            else
            {
                printf("Connection Closed by server.\n");
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

        //pthread_cond_signal(&buffer_cond);
        rx_done++;
        read_index = (read_index + 1) % MAX_BATCHES;
        sem_post(&data_ready);

        retry_count = 0; // Reset retry count after successful read
    }

    pthread_mutex_lock(&buffer_mutex);
    data_available = -1; // Indicate end of data
    pthread_mutex_unlock(&buffer_mutex);

    close(client_fd);
    pthread_exit(NULL);
}

void *write_data(void *arg)
{
    // Open the file for writing in binary mode ("wb")
    int fd = open(FileNames[file_number], O_WRONLY | O_CREAT| O_TRUNC, 0644);
    if (fd == -1)
    {
        perror("open failed");
        exit(1);
    }

    char *data = 0;
    unsigned int data_size = 0;
    unsigned int written_now, bytes_written, bytes_extra;

    while (1)
    {
        pthread_mutex_lock(&buffer_mutex);
        if (data_available == -1 && read_index == write_index)
        {
            break; // End of data
        }
        pthread_mutex_unlock(&buffer_mutex);
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
        bytes_extra = 0;

        if((total_written + data_size) >= BYTES_ONE_FILES)
        {
            bytes_extra = total_written + data_size - BYTES_ONE_FILES;
            data_size -= bytes_extra;
        }

write_to_newfiles:
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
                    break;
                }
            }
            else
            {
                bytes_written += written_now;
            }
        }

        total_written += bytes_written;

        if(bytes_extra > 0)
        {
            data_size = bytes_extra;
            bytes_extra = 0;
            total_written -= BYTES_ONE_FILES;
            bytes_written = 0;

            close(fd);
            file_number++;
            fd = open(FileNames[file_number], O_WRONLY | O_CREAT| O_TRUNC, 0644);
            if (fd == -1)
            {
                perror("open failed");
                break;
            }
            goto write_to_newfiles;
        }

        write_index = (write_index + 1) % MAX_BATCHES;
        tx_done++;
    }

    close(fd);
    pthread_exit(NULL);
}


void Create_Files()
{
    int desired_length = 3;
    int num_files = NUMBER_FILES;
    char *base_filename = "ascan";
    char *folder_path = "/home/pigsystem/Desktop/data";
    char number[4];

    // Check if folder path exists (basic check)
    if (access(folder_path, F_OK) == -1)
    {
        printf("Error: Folder '%s' does not exist!\n", folder_path);
    }

    FileNames = (char **)malloc(sizeof(char *) * MAX_PATH);

    // Loop to create files
    for (int i = 0; i < num_files; i++)
    {
        char filename[MAX_PATH];
        memset(filename, 0, MAX_PATH); // Clear the filename buffer

        // Construct the full path with folder and filename
        snprintf(number, sizeof(number), "%u", i+1);
        Add_Leading_Zeros(number, desired_length);

        snprintf(filename, MAX_PATH, "%s/%s_%s.bin", folder_path, base_filename, number);

        FILE *fp = fopen(filename, "w");

        if (fp == NULL)
        {
            printf("Error creating file %s\n", filename);
        }

        FileNames[i] = malloc(strlen(filename)+ 1);
        strcpy((FileNames[i]), filename);

        fclose(fp);
    }

    printf("Created %d files successfully in '%s' folder!\n", num_files, folder_path);
}


static void Add_Leading_Zeros(char *number, int total_digits)
{
    int num_digits = strlen(number);
    int num_zeros = total_digits - num_digits;

    // Handle cases where the number length is already equal or more than the desired total digits
    if (num_zeros <= 0)
    {
        return; // No need to modify the string
    }

    // Create a buffer large enough to hold the formatted string (including null terminator)
    char formatted_number[total_digits + 1];

    // Fill with zeros
    memset(formatted_number, '0', num_zeros);
    formatted_number[num_zeros] = '\0'; // Null terminate

    // Copy the original number after the leading zeros
    strncat(formatted_number, number, num_digits);

    // Replace the original string content with the formatted string
    strcpy(number, formatted_number);
}




