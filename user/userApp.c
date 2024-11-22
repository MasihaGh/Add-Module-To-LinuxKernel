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

#define SHM_KEY 1234
#define SHM_SIZE 1024
#define QUEUE_DEVICE "/dev/myQueue"
#define MAX_PROCESSES 3

void process1(int socket_pair[2]);
void process2(int socket_pair[2]);
void process3();

typedef struct
{
    pid_t pid;
    time_t finished;
    time_t terminated;
} ProcessInfo;

int main()
{
    int socket_pair[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair) < 0)
    {
        perror("P_Failed to create socket pair");
        exit(EXIT_FAILURE);
    }

    int zshm_id = shmget(IPC_PRIVATE, MAX_PROCESSES * sizeof(ProcessInfo), IPC_CREAT | 0666);
    if (zshm_id < 0)
    {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }

    ProcessInfo *processes = (ProcessInfo *)shmat(zshm_id, NULL, 0);
    if (processes == (void *)-1)
    {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }

    int shm_id = shmget(SHM_KEY, SHM_SIZE, IPC_CREAT | 0666);
    if (shm_id == -1)
    {
        perror("P_Failed to create shared memory");
        exit(EXIT_FAILURE);
    }

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

    close(socket_pair[0]);
    close(socket_pair[1]);

    waitpid(pid1, NULL, 0);
    time(&processes[0].terminated);
    printf("Process 1 finished at: %ld, terminated at: %ld\n", processes[0].finished, processes[0].terminated);

    waitpid(pid2, NULL, 0);
    time(&processes[1].terminated);
    printf("Process 2 finished at: %ld, terminated at: %ld\n", processes[1].finished, processes[1].terminated);

    waitpid(pid3, NULL, 0);
    time(&processes[2].terminated);
    printf("Process 3 finished at: %ld, terminated at: %ld\n", processes[2].finished, processes[2].terminated);

    char *shm_ptr = (char *)shmat(shm_id, NULL, 0);
    if (shm_ptr == (void *)-1)
    {
        perror("P_Failed to attach shared memory");
        exit(EXIT_FAILURE);
    }

    printf("Parent Process: Data read from shared memory: %s\n", shm_ptr);

    time_t zombie_time1 = (processes[0].terminated - processes[0].finished);
    printf("Zombie time for Process 1 (PID: %d): %ld seconds\n", pid1, zombie_time1);

    time_t zombie_time2 = (processes[1].terminated - processes[1].finished);
    printf("Zombie time for Process 2 (PID: %d): %ld seconds\n", pid2, zombie_time2);

    time_t zombie_time3 = (processes[2].terminated - processes[2].finished);
    printf("Zombie time for Process 3 (PID: %d): %ld seconds\n", pid3, zombie_time3);

    shmdt(shm_ptr);
    shmdt(processes);
    shmctl(shm_id, IPC_RMID, NULL);

    return 0;
}

void process1(int socket_pair[2])
{
    char input[256];
    ssize_t bytes_written;

    close(socket_pair[0]);

    printf("Process 1: Enter a string: ");
    fgets(input, sizeof(input), stdin);

    size_t length = strlen(input);
    if (length > 0 && input[length - 1] == '\n')
    {
        input[length - 1] = '\0';
    }

    if (input[0] == '\0')
    {
        printf("Process 1 skiped!\n");
    }

    bytes_written = write(socket_pair[1], input, strlen(input) + 1);
    if (bytes_written < 0)
    {
        perror("Process 1: Failed to send message");
    }
    else
    {
        printf("Process 1: Sent '%s' to Process 2\n", input);
    }

    close(socket_pair[1]);
}

void process2(int sock_fd[2])
{
    char received_string[1024];
    char *device = QUEUE_DEVICE;
    int device_fd;
    ssize_t write_ret;

    close(sock_fd[1]);

    ssize_t received_len = read(sock_fd[0], received_string, sizeof(received_string) - 1);
    if (received_len < 0)
    {
        perror("Process 2: Failed to receive string from process1");
        exit(EXIT_FAILURE);
    }

    received_string[received_len] = '\0';

    printf("Process2 received string: %s\n", received_string);

    device_fd = open(device, O_WRONLY);
    if (device_fd < 0)
    {
        perror("Process 2: Failed to open character device");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < received_len; i++)
    {
        if (received_string[i] == '\n' || received_string[i] == '\0')
        {
            break;
        }

        write_ret = write(device_fd, &received_string[i], 1);
        if (write_ret < 0)
        {
            if (errno == ENOMEM)
            {
                printf("Process 2: Character device queue is full. Stopping write.\n");
                break;
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

    close(device_fd);

    close(sock_fd[0]);
}

void process3()
{
    int device_fd;
    int shm_id;
    char *shm_ptr;
    char buffer[2];
    ssize_t bytes_read;

    device_fd = open(QUEUE_DEVICE, O_RDONLY | O_NONBLOCK);
    if (device_fd < 0)
    {
        perror("Process 3: Failed to open character device");
        exit(EXIT_FAILURE);
    }

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

    shm_ptr[0] = '\0';

    printf("\nProcess 3: Reading from character device...\n");

    while (1)
    {
        bytes_read = read(device_fd, buffer, sizeof(buffer) - 1);

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

        buffer[bytes_read] = '\0';

        if (strlen(shm_ptr) + bytes_read >= SHM_SIZE)
        {
            fprintf(stderr, "Process 3: Shared memory is full. Cannot append more data.\n");
            break;
        }

        strcat(shm_ptr, buffer);
        printf("Process 3: Read '%s' from queue and appended to shared memory\n", buffer);
    }

    if (shmdt(shm_ptr) == -1)
    {
        perror("Process 3: Failed to detach shared memory");
    }

    close(device_fd);
}