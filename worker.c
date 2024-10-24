#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <unistd.h>

// Structure for the Clock (Shared Memory)
struct Clock {
    int seconds;
    int nanoseconds;
};

// Structure for Message Buffer (Message Queue)
struct msg_buffer {
    long msg_type; 
    int msg_data;  
};

int main(int argc, char *argv[]) {
    // Message queue ID and message buffer
    int msgid;
    struct msg_buffer msg;

    // Get message queue ID (this will be passed in as a parameter)
    msgid = atoi(argv[1]);

    // Loop simulating worker process behavior
    while (1) {
        // Receive a message from oss (blocking until received)
        msgrcv(msgid, &msg, sizeof(msg.msg_data), 1, 0);
        printf("Worker PID %d: Received message with data %d\n", getpid(), msg.msg_data);

        // If the message data is 0, terminate the worker
        if (msg.msg_data == 0) {
            printf("Worker PID %d: Terminating.\n", getpid());
            break;
        }

        // Simulate doing work (for now, just sleep for a short time)
        usleep(100000);  // Sleep for 100ms (simulated work)

        // Send a message back to oss with data indicating work done
        msg.msg_type = 1;
        msg.msg_data = 1;  // Simulate completion of work
        msgsnd(msgid, &msg, sizeof(msg.msg_data), 0);
        printf("Worker PID %d: Sent message back to OSS.\n", getpid());
    }

    return 0;
}

