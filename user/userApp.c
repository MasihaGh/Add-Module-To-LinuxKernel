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

#define SHM_KEY 1234       // Key for shared memory
#define SHM_SIZE 1024      // Size of shared memory
#define QUEUE_DEVICE "/dev/myQueue"


void process1(int socket_pair[2]);
void process2(int socket_pair[2]);


// Parent process that creates shared memory and waits for Process 3
int main() {
    // Create shared memory
    int shm_id = shmget(SHM_KEY, SHM_SIZE, IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Failed to create shared memory");
        exit(1);
    }

    // Fork processes
    pid_t pid3 = fork();

    if (pid3 == 0) {
        // In child process, execute process3()
        process3();
        exit(0);  // Exit after process3 execution
    } else if (pid3 > 0) {
        // Parent Process
        // Wait for Process 3 to finish
        waitpid(pid3, NULL, 0);

        // Attach shared memory
        char *shm_ptr = (char *)shmat(shm_id, NULL, 0);
        if (shm_ptr == (void *)-1) {
            perror("Failed to attach shared memory");
            exit(1);
        }

        // Read and print shared memory values
        printf("Data read from shared memory: %s\n", shm_ptr);

        // Detach and destroy shared memory
        shmdt(shm_ptr);
        shmctl(shm_id, IPC_RMID, NULL);
    } else {
        perror("Failed to fork");
        exit(1);
    }

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

void process2(int sock_fd[2]) {
    char received_string[1024]; // Buffer to store received string
    char *device = QUEUE_DEVICE; // Path to the character device
    int device_fd; // File descriptor for the character device
    ssize_t write_ret; // Return value of the write operation

    // Close the unused write end of the socket pair
    close(sock_fd[1]);

    // Receive the string from process1 via the socket
    ssize_t received_len = read(sock_fd[0], received_string, sizeof(received_string) - 1);
    if (received_len < 0) {
        perror("Failed to receive string from process1");
        exit(EXIT_FAILURE);
    }

    // Null-terminate the received string
    received_string[received_len] = '\0';

    printf("Process2 received string: %s\n", received_string);

    // Open the character device for writing
    device_fd = open(device, O_WRONLY);
    if (device_fd < 0) {
        perror("Failed to open character device");
        exit(EXIT_FAILURE);
    }

    // Write the string to the character device, character by character
    for (int i = 0; i < received_len; i++) {
        write_ret = write(device_fd, &received_string[i], 1); // Write one character
        if (write_ret < 0) {
            if (errno == ENOMEM) {
                printf("Character device queue is full. Stopping write.\n");
                break; // Stop writing if the queue is full
            } else {
                perror("Error writing to character device");
                break;
            }
        } else {
            printf("Wrote '%c' to the character device successfully.\n", received_string[i]);
        }
    }

    // Close the character device
    close(device_fd);

    // Close the socket read end
    close(sock_fd[0]);

    exit(EXIT_SUCCESS);
}

void process3() {
    // Attach to shared memory
    int shm_id = shmget(SHM_KEY, SHM_SIZE, 0666);
    if (shm_id == -1) {
        perror("Failed to access shared memory");
        exit(1);
    }
    char *shm_ptr = (char *)shmat(shm_id, NULL, 0);
    if (shm_ptr == (void *)-1) {
        perror("Failed to attach to shared memory");
        exit(1);
    }

    // Open the character device
    int fd = open(QUEUE_DEVICE, O_RDONLY);
    if (fd == -1) {
        perror("Failed to open character device");
        exit(1);
    }

    char ch;
    int read_ret;
    size_t shm_index = 0;

    // Read from the queue character by character
    while ((read_ret = read(fd, &ch, 1)) > 0) {
        // Add to shared memory
        shm_ptr[shm_index++] = ch;

        // Stop if shared memory is full
        if (shm_index >= SHM_SIZE - 1) {
            fprintf(stderr, "Shared memory full, cannot write more data.\n");
            break;
        }
    }

    if (read_ret == 0) {
        printf("Queue is empty.\n");
    } else if (read_ret < 0) {
        perror("Error reading from queue");
    }

    // Null-terminate the shared memory
    shm_ptr[shm_index] = '\0';

    // Cleanup
    shmdt(shm_ptr);
    close(fd);
}
