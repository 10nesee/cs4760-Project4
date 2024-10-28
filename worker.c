#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <time.h>

// Message structure for communication
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
    srand(getpid());  // Seed random with unique PID for different processes

    while (1) {
        // Receive the time quantum from oss
        msgrcv(msgid, &msg, sizeof(msg.msg_data), 1, 0);
        int time_slice = abs(msg.msg_data);  // Ensure time slice is positive

        printf("Worker PID %d: Received time slice of %d nanoseconds\n", getpid(), time_slice);

        // Use entire slice if it's very small (less than 1 ms), otherwise, random usage
        int use_time = (time_slice < 1000000) ? time_slice : (rand() % 100 < 70) ? time_slice : (time_slice * (rand() % 50 + 1) / 100);
        int blocked = (use_time < time_slice) ? 1 : 0;

        msg.msg_type = 1;
        msg.msg_data = blocked ? -use_time : use_time;
        msgsnd(msgid, &msg, sizeof(msg.msg_data), 0);

        if (blocked) {
            printf("Worker PID %d: Blocked with %d ns used. Waiting for OSS to unblock.\n", getpid(), use_time);
            msgrcv(msgid, &msg, sizeof(msg.msg_data), getpid(), 0);
            if (msg.msg_data == 0) {
                printf("Worker PID %d: Terminating.\n", getpid());
                break;
            }
        } else {
            printf("Worker PID %d: Used entire time slice or finished and terminating.\n", getpid());
            break;
        }
    }
    return 0;
}

