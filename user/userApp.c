#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>

#define SHM_KEY 1234  // Key for shared memory
#define SHM_SIZE 1024 // Size of shared memory
#define QUEUE_DEVICE "/dev/myQueue"
#define MAX_PROCESSES 3

// Function declarations for the processes
void process1(int socket_pair[2]);
void process2(int socket_pair[2]);
void process3();

// Structure to store the start time and end time of each process
typedef struct
{
    pid_t pid;
    time_t finished;
    time_t terminated;
} ProcessInfo;

int main()
{
    int socket_pair[2]; // Socket pair for inter-process communication

    // Create a socket pair
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair) < 0)
    {
        perror("P_Failed to create socket pair");
        exit(EXIT_FAILURE);
    }

    // Create shared memory segment
    int zshm_id = shmget(IPC_PRIVATE, MAX_PROCESSES * sizeof(ProcessInfo), IPC_CREAT | 0666);
    if (zshm_id < 0)
    {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }

    // Attach shared memory
    ProcessInfo *processes = (ProcessInfo *)shmat(zshm_id, NULL, 0);
    if (processes == (void *)-1)
    {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }

    // Create shared memory for process3 and parent
    int shm_id = shmget(SHM_KEY, SHM_SIZE, IPC_CREAT | 0666);
    if (shm_id == -1)
    {
        perror("P_Failed to create shared memory");
        exit(EXIT_FAILURE);
    }

    // Fork Process 1
    pid_t pid1 = fork();
    if (pid1 == 0)
    {
        process1(socket_pair);
        ProcessInfo *processes = (ProcessInfo *)shmat(zshm_id, NULL, 0);
        if (processes == (void *)-1)
        {
            perror("shmat failed");
            exit(EXIT_FAILURE);
        }
        time(&processes[0].finished);
        shmdt(processes);
        exit(EXIT_SUCCESS);
    }

    // Fork Process 2
    pid_t pid2 = fork();
    if (pid2 == 0)
    {
        process2(socket_pair);
        ProcessInfo *processes = (ProcessInfo *)shmat(zshm_id, NULL, 0);
        if (processes == (void *)-1)
        {
            perror("shmat failed");
            exit(EXIT_FAILURE);
        }
        time(&processes[1].finished);
        shmdt(processes);
        exit(EXIT_SUCCESS);
    }

    // Fork Process 3
    pid_t pid3 = fork();
    if (pid3 == 0)
    {
        process3();
        ProcessInfo *processes = (ProcessInfo *)shmat(zshm_id, NULL, 0);
        if (processes == (void *)-1)
        {
            perror("shmat failed");
            exit(EXIT_FAILURE);
        }
        time(&processes[2].finished);
        shmdt(processes);
        exit(EXIT_SUCCESS);
    }

    // Parent Process
    close(socket_pair[0]); // Close the unused ends of the socket
    close(socket_pair[1]);

    // Wait for all child processes to finish
    waitpid(pid1, NULL, 0);
    time(&processes[0].terminated);
    printf("Process 1 finished at: %ld, terminated at: %ld\n", processes[0].finished, processes[0].terminated);

    waitpid(pid2, NULL, 0);
    time(&processes[1].terminated);
    printf("Process 2 finished at: %ld, terminated at: %ld\n", processes[1].finished, processes[1].terminated);

    waitpid(pid3, NULL, 0);
    time(&processes[2].terminated);
    printf("Process 3 finished at: %ld, terminated at: %ld\n", processes[2].finished, processes[2].terminated);

    // Attach to the shared memory
    char *shm_ptr = (char *)shmat(shm_id, NULL, 0);
    if (shm_ptr == (void *)-1)
    {
        perror("P_Failed to attach shared memory");
        exit(EXIT_FAILURE);
    }

    // Print the content of the shared memory
    printf("Parent Process: Data read from shared memory: %s\n", shm_ptr);

    // Calculate zombie time for Process 1
    time_t zombie_time1 = (processes[0].terminated - processes[0].finished);
    printf("Zombie time for Process 1 (PID: %d): %ld seconds\n", pid1, zombie_time1);

    // Calculate zombie time for Process 2
    time_t zombie_time2 = (processes[1].terminated - processes[1].finished);
    printf("Zombie time for Process 2 (PID: %d): %ld seconds\n", pid2, zombie_time2);

    // Calculate zombie time for Process 3
    time_t zombie_time3 = (processes[2].terminated - processes[2].finished);
    printf("Zombie time for Process 3 (PID: %d): %ld seconds\n", pid3, zombie_time3);

    // Detach and clean up the shared memory
    shmdt(shm_ptr);
    shmdt(processes);
    shmctl(shm_id, IPC_RMID, NULL);

    return 0;
}

// Process 1: Get input from the user and send it to Process 2
void process1(int socket_pair[2])
{
    char input[256];       // Buffer to hold user input
    ssize_t bytes_written; // Number of bytes written to the socket

    // Close the unused socket for receiving (socket_pair[0])
    close(socket_pair[0]);

    // Prompt the user for input
    printf("Process 1: Enter a string: ");
    fgets(input, sizeof(input), stdin);

    // Remove the newline character from the input (if present)
    size_t length = strlen(input);
    if (length > 0 && input[length - 1] == '\n')
    {
        input[length - 1] = '\0';
    }

    if (input[0] == '\0')
    {
        printf("Process 1 skiped!\n");
    }

    // Write the user input to the socket
    bytes_written = write(socket_pair[1], input, strlen(input) + 1); // +1 to include the null terminator
    if (bytes_written < 0)
    {
        perror("Process 1: Failed to send message");
    }
    else
    {
        printf("Process 1: Sent '%s' to Process 2\n", input);
    }

    // Close the writing socket after sending the data
    close(socket_pair[1]);
}

void process2(int sock_fd[2])
{
    char received_string[1024];  // Buffer to store received string
    char *device = QUEUE_DEVICE; // Path to the character device
    int device_fd;               // File descriptor for the character device
    ssize_t write_ret;           // Return value of the write operation

    // Close the unused write end of the socket pair
    close(sock_fd[1]);

    // Receive the string from process1 via the socket
    ssize_t received_len = read(sock_fd[0], received_string, sizeof(received_string) - 1);
    if (received_len < 0)
    {
        perror("Process 2: Failed to receive string from process1");
        exit(EXIT_FAILURE);
    }

    // Null-terminate the received string
    received_string[received_len] = '\0';

    printf("Process2 received string: %s\n", received_string);

    // Open the character device for writing
    device_fd = open(device, O_WRONLY);
    if (device_fd < 0)
    {
        perror("Process 2: Failed to open character device");
        exit(EXIT_FAILURE);
    }

    // Write the string to the character device, character by character
    for (int i = 0; i < received_len; i++)
    {
        if (received_string[i] == '\n' || received_string[i] == '\0')
        {
            break;
        }

        write_ret = write(device_fd, &received_string[i], 1); // Write one character
        if (write_ret < 0)
        {
            if (errno == ENOMEM)
            {
                printf("Process 2: Character device queue is full. Stopping write.\n");
                break; // Stop writing if the queue is full
            }
            else
            {
                perror("Process 2: Error writing to character device");
                break;
            }
        }
        else
        {
            printf("Process 2: Wrote '%c' to the character device successfully.\n", received_string[i]);
        }
    }

    // Close the character device
    close(device_fd);

    // Close the socket read end
    close(sock_fd[0]);
}

void process3()
{
    int device_fd;      // File descriptor for the character device
    int shm_id;         // Shared memory ID
    char *shm_ptr;      // Pointer to shared memory
    char buffer[2];     // Buffer to read data from the character device
    ssize_t bytes_read; // Number of bytes read

    // Open the character device for reading in non-blocking mode
    device_fd = open(QUEUE_DEVICE, O_RDONLY | O_NONBLOCK);
    if (device_fd < 0)
    {
        perror("Process 3: Failed to open character device");
        exit(EXIT_FAILURE);
    }

    // Attach to the shared memory segment
    shm_id = shmget(SHM_KEY, SHM_SIZE, IPC_CREAT | 0666);
    if (shm_id == -1)
    {
        perror("Process 3: Failed to create shared memory");
        close(device_fd);
        exit(EXIT_FAILURE);
    }
    shm_ptr = (char *)shmat(shm_id, NULL, 0);
    if (shm_ptr == (void *)-1)
    {
        perror("Process 3: Failed to attach shared memory");
        close(device_fd);
        exit(EXIT_FAILURE);
    }

    // Initialize shared memory with an empty string
    shm_ptr[0] = '\0';

    printf("\nProcess 3: Reading from character device...\n");

    // Read from the character device until the queue is empty or an error occurs
    while (1)
    {
        bytes_read = read(device_fd, buffer, sizeof(buffer) - 1); // Leave space for null terminator

        if (bytes_read < 0)
        {
            if (errno == EAGAIN)
            {
                printf("Process 3: Queue is empty (blocking mode).\n");
                break;
            }
            else
            {
                perror("Process 3: Error reading from queue");
                break;
            }
        }
        else if (bytes_read == 0)
        {
            printf("Process 3: Queue is empty (non-blocking mode).\n");
            break;
        }

        // Ensure the buffer is null-terminated
        buffer[bytes_read] = '\0';

        // Check for shared memory overflow
        if (strlen(shm_ptr) + bytes_read >= SHM_SIZE)
        {
            fprintf(stderr, "Process 3: Shared memory is full. Cannot append more data.\n");
            break;
        }

        // Append the read character to shared memory
        strcat(shm_ptr, buffer);
        printf("Process 3: Read '%s' from queue and appended to shared memory\n", buffer);
    }

    // Detach from shared memory
    if (shmdt(shm_ptr) == -1)
    {
        perror("Process 3: Failed to detach shared memory");
    }

    // Close the character device
    close(device_fd);
}