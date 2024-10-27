#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#define MAX_PROCESSES 18
#define REAL_TIME_LIMIT 3  

// Simulated system clock
struct Clock {
    int seconds;
    int nanoseconds;
};

// Process Control Block (PCB) structure
struct PCB {
    int occupied;
    pid_t pid;
    int startSeconds;
    int startNano;
    int serviceTimeSeconds;
    int serviceTimeNano;
    int blocked;
};

// PCB Table and Queues
struct PCB processTable[MAX_PROCESSES];
int readyQueue[MAX_PROCESSES], readyFront = 0, readyRear = 0;

struct msg_buffer {
    long msg_type;
    int msg_data;
};

void enqueue(int queue[], int *rear, int pcbIndex) {
    queue[*rear] = pcbIndex;
    *rear = (*rear + 1) % MAX_PROCESSES;
}

int dequeue(int queue[], int *front) {
    int pcbIndex = queue[*front];
    *front = (*front + 1) % MAX_PROCESSES;
    return pcbIndex;
}

// Calculate priority 
double calculate_priority_ratio(struct PCB *pcb, struct Clock *clock) {
    double serviceTime = pcb->serviceTimeSeconds + pcb->serviceTimeNano / 1e9;
    double timeInSystem = (clock->seconds - pcb->startSeconds) + (clock->nanoseconds - pcb->startNano) / 1e9;
    return timeInSystem == 0 ? 0 : serviceTime / timeInSystem;
}

// Increment the clock
void increment_clock(struct Clock *clock, int ns) {
    clock->nanoseconds += ns;
    if (clock->nanoseconds >= 1000000000) {
        clock->seconds++;
        clock->nanoseconds -= 1000000000;
    }
}

int main() {
    int shmid = shmget(IPC_PRIVATE, sizeof(struct Clock), IPC_CREAT | 0666);
    struct Clock *shm_clock = (struct Clock *)shmat(shmid, NULL, 0);
    shm_clock->seconds = shm_clock->nanoseconds = 0;

    int msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);

    // Record start time
    time_t start_time = time(NULL);

    // Initialize PCB table and fork workers
    for (int i = 0; i < MAX_PROCESSES; i++) processTable[i].occupied = 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char msgid_str[10];
            sprintf(msgid_str, "%d", msgid);
            execlp("./worker", "worker", msgid_str, (char *)NULL);
            exit(1);
        } else {
            for (int j = 0; j < MAX_PROCESSES; j++) {
                if (!processTable[j].occupied) {
                    processTable[j].occupied = 1;
                    processTable[j].pid = pid;
                    enqueue(readyQueue, &readyRear, j);
                    break;
                }
            }
        }
    }

    // Scheduling loop 
    while (readyFront != readyRear && (time(NULL) - start_time) < REAL_TIME_LIMIT) {
        int highestPriorityIndex = -1;
        double minRatio = __DBL_MAX__;

        // Determine the highest priority process
        for (int i = readyFront; i != readyRear; i = (i + 1) % MAX_PROCESSES) {
            int pcbIndex = readyQueue[i];
            struct PCB *pcb = &processTable[pcbIndex];
            double ratio = calculate_priority_ratio(pcb, shm_clock);

            if (ratio < minRatio) {
                minRatio = ratio;
                highestPriorityIndex = i;
            }
        }

        // Schedule the highest priority process
        if (highestPriorityIndex != -1) {
            int pcbIndex = dequeue(readyQueue, &readyFront);
            struct PCB *pcb = &processTable[pcbIndex];

            struct msg_buffer msg = {1, 50000000};  
            msgsnd(msgid, &msg, sizeof(msg.msg_data), 0);

            // Wait for a response and update clock and service time
            msgrcv(msgid, &msg, sizeof(msg.msg_data), 1, 0);

            int time_used = abs(msg.msg_data);
            increment_clock(shm_clock, time_used);
            pcb->serviceTimeNano += time_used;
            if (pcb->serviceTimeNano >= 1000000000) {
                pcb->serviceTimeSeconds++;
                pcb->serviceTimeNano -= 1000000000;
            }

            // Enqueue again or remove process 
            if (msg.msg_data > 0) {
                enqueue(readyQueue, &readyRear, pcbIndex);
            } else {
                waitpid(pcb->pid, NULL, 0);
                pcb->occupied = 0;
            }
        }
    }

    // Cleanup code
    shmdt(shm_clock);
    shmctl(shmid, IPC_RMID, NULL);
    msgctl(msgid, IPC_RMID, NULL);

    printf("OSS: Simulation completed.\n");

    return 0;
}

