#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <time.h>

struct msg_buffer {
    long msg_type;
    int msg_data;
};

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <msgid>\n", argv[0]);
        exit(1);
    }

    int msgid = atoi(argv[1]);
    struct msg_buffer msg;
    srand(getpid()); 

    while (1) {
        // Receive the time quantum from oss
        if (msgrcv(msgid, &msg, sizeof(msg.msg_data), getpid(), 0) == -1) {
            perror("msgrcv from oss failed");
            exit(1);
        }
        int time_slice = abs(msg.msg_data);
        printf("Worker PID %d: Received time slice of %d nanoseconds\n", getpid(), time_slice);

        // Decide how the process should respond
        int action = rand() % 100;
        if (action < 30) {  
            msg.msg_data = -(rand() % time_slice + 1);  
            printf("Worker PID %d: Deciding to terminate after using %d nanoseconds\n", getpid(), -msg.msg_data);
        } else if (action < 60) {  
            msg.msg_data = rand() % time_slice + 1;
            printf("Worker PID %d: Using %d nanoseconds due to I/O\n", getpid(), msg.msg_data);
        } else {  
            msg.msg_data = time_slice;
            printf("Worker PID %d: Using full time slice of %d nanoseconds\n", getpid(), time_slice);
        }

        // Send the message back to oss
        msg.msg_type = 1; 
        if (msgsnd(msgid, &msg, sizeof(msg.msg_data), 0) == -1) {
            perror("msgsnd to oss failed");
            exit(1);
        }

        // Process terminate if negative
        if (msg.msg_data < 0) {
            printf("Worker PID %d: Terminating\n", getpid());
            break;
        }
    }
    return 0;
}

