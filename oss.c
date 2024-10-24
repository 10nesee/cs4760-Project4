#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <unistd.h>

struct Clock {
    int seconds;
    int nanoseconds;
};

struct msg_buffer {
    long msg_type; 
    int msg_data;  
};

int main(int argc, char *argv[]) {
    // Initialize shared memory for the clock
    int shmid;
    struct Clock *shm_clock;

    shmid = shmget(IPC_PRIVATE, sizeof(struct Clock), IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("Create shared memory error");
        exit(1);
    }

    shm_clock = (struct Clock *)shmat(shmid, NULL, 0);
    if (shm_clock == (struct Clock *)(-1)) {
        perror("Attach shared memory error");
        exit(1);
    }

    // Initialize the clock
    shm_clock->seconds = 0;
    shm_clock->nanoseconds = 0;

    // Create a message queue for communication with workers
    int msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
    if (msgid < 0) {
        perror("Message queue creation error");
        exit(1);
    }

    // Fork a worker process
    pid_t pid = fork();
    if (pid == 0) {
        // In the child (worker) process, exec worker.c
        char msgid_str[10];
        sprintf(msgid_str, "%d", msgid);  // Pass the message queue ID as a string
        execlp("./worker", "worker", msgid_str, (char *)NULL);
        perror("Exec failed");
        exit(1);
    } else if (pid > 0) {
        // In the parent (oss) process, send a message to the worker
        struct msg_buffer msg;
        msg.msg_type = 1;
        msg.msg_data = 1;  // Simulate a time slice for the worker
        msgsnd(msgid, &msg, sizeof(msg.msg_data), 0);
        printf("OSS: Sent message to worker PID %d\n", pid);

        // Wait for a response from the worker
        msgrcv(msgid, &msg, sizeof(msg.msg_data), 1, 0);
        printf("OSS: Received message from worker PID %d: %d\n", pid, msg.msg_data);

        // Send a message to terminate the worker
        msg.msg_data = 0;  // 0 indicates termination
        msgsnd(msgid, &msg, sizeof(msg.msg_data), 0);
        printf("OSS: Sent termination message to worker PID %d\n", pid);

        // Wait for the worker to terminate
        wait(NULL);
    }

    // Detach and clean up shared memory and message queue
    shmdt(shm_clock);
    shmctl(shmid, IPC_RMID, NULL);
    msgctl(msgid, IPC_RMID, NULL);

    return 0;
}

