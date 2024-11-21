#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>


void process1(int socket_pair[2]);


int main()
{
    int socket_pair[2];

    // Create a socket pair for inter-process communication
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair) < 0)
    {
        perror("Failed to create socket pair");
        return 1;
    }

    // Fork a child process
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("Fork failed");
        return 1;
    }
    else if (pid == 0)
    {
        // Child process (Process 1)
        process1(socket_pair);
    }
    else
    {
        // Parent process (Process 2 will be implemented later)
        close(socket_pair[1]); // Close unused writing socket in parent
        printf("Parent process running...\n");
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
    char *device = "/dev/myQueue"; // Path to the character device
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
