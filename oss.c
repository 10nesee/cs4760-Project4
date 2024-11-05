#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#define MAX_PROCESSES 18  // Max number of processes allowed
#define REAL_TIME_LIMIT 3  // Real-world time limit in seconds

// Shared clock to track simulated system time
struct Clock {
    int seconds;
    int nanoseconds;
};

// Process Control Block (PCB) to store process-related data
struct PCB {
    int occupied;
    pid_t pid;
    int startSeconds, startNano; // Start time for each process
    int serviceTimeSeconds, serviceTimeNano; // Time spent being serviced
    int cpuTimeSeconds, cpuTimeNano; // Total CPU time used
    int waitTimeSeconds, waitTimeNano; // Total time spent waiting
};

// Message structure for communication between oss and workers
struct msg_buffer {
    long msg_type;
    int msg_data;
};

// Global Variables
struct PCB processTable[MAX_PROCESSES];
int readyQueue[MAX_PROCESSES], readyFront = 0, readyRear = 0;
int shmid, msgid;   
struct Clock *shm_clock; 
FILE *logfile;

// Function Prototypes
void enqueue(int queue[], int *rear, int pcbIndex);
int dequeue(int queue[], int *front);
double calculate_priority_ratio(struct PCB *pcb, struct Clock *clock);
void increment_clock(struct Clock *clock, int ns);
void add_time(int *seconds, int *nanoseconds, int add_ns);
void log_process_table();
void cleanup(int signo);
void setup_signal_handlers();
void create_process(int pcbIndex);
void terminate_on_timeout(int signo);

// Set up signal handlers to cleanup on exit
void setup_signal_handlers() {
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGALRM, terminate_on_timeout);
    alarm(REAL_TIME_LIMIT * 2);  
}

// Handler for when the program times out
void terminate_on_timeout(int signo) {
    fprintf(stderr, "Timeout reached. Terminating simulation.\n");
    cleanup(signo);
}

// Adds a process to the ready queue
void enqueue(int queue[], int *rear, int pcbIndex) {
    queue[*rear] = pcbIndex;
    *rear = (*rear + 1) % MAX_PROCESSES;
}

// Removes a process from the ready queue
int dequeue(int queue[], int *front) {
    int pcbIndex = queue[*front];
    *front = (*front + 1) % MAX_PROCESSES;
    return pcbIndex;
}

// Calculates priority for a process
double calculate_priority_ratio(struct PCB *pcb, struct Clock *clock) {
    double serviceTime = pcb->serviceTimeSeconds + pcb->serviceTimeNano / 1e9;
    double timeInSystem = (clock->seconds - pcb->startSeconds) + (clock->nanoseconds - pcb->startNano) / 1e9;
    return timeInSystem == 0 ? 0 : serviceTime / timeInSystem;
}

// Increment the clock
void increment_clock(struct Clock *clock, int ns) {
    clock->nanoseconds += ns;
    while (clock->nanoseconds >= 1000000000) {
        clock->seconds++;
        clock->nanoseconds -= 1000000000;
    }
}

// Adds time and manages overflow 
void add_time(int *seconds, int *nanoseconds, int add_ns) {
    *nanoseconds += add_ns;
    if (*nanoseconds >= 1000000000) {
        (*seconds)++;
        *nanoseconds -= 1000000000;
    }
}

// Cleans up shared memory, message queues, and child processes
void cleanup(int signo) {
    shmdt(shm_clock);  // Detach shared memory
    shmctl(shmid, IPC_RMID, NULL);  // Destroy shared memory
    msgctl(msgid, IPC_RMID, NULL);  // Destroy message queue

    // Kill active process
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processTable[i].occupied) {
            kill(processTable[i].pid, SIGTERM);
            waitpid(processTable[i].pid, NULL, 0);
        }
    }

    if (logfile) fclose(logfile);  // Close log file
    printf("Cleanup complete. Exiting.\n");
    exit(0);
}

// Logs to file
void log_process_table() {
    fprintf(logfile, "Current system time: %d:%d\n", shm_clock->seconds, shm_clock->nanoseconds);
    fprintf(logfile, "Process Table:\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processTable[i].occupied) {
            fprintf(logfile, "PID %d - CPU Time: %d s %d ns, Wait Time: %d s %d ns\n",
                    processTable[i].pid,
                    processTable[i].cpuTimeSeconds, processTable[i].cpuTimeNano,
                    processTable[i].waitTimeSeconds, processTable[i].waitTimeNano);
        }
    }
    fflush(logfile);
}

// Create worker process and adds it to the process table
void create_process(int pcbIndex) {
    pid_t pid = fork();
    if (pid == 0) {  // Child
        char msgid_str[10];
        sprintf(msgid_str, "%d", msgid);
        execlp("./worker", "worker", msgid_str, NULL);
        perror("execlp failed");
        exit(1);  
    } else {  // Parent
        processTable[pcbIndex].occupied = 1;
        processTable[pcbIndex].pid = pid;
        processTable[pcbIndex].startSeconds = shm_clock->seconds;
        processTable[pcbIndex].startNano = shm_clock->nanoseconds;
        enqueue(readyQueue, &readyRear, pcbIndex);
    }
}

// Main simulation loop
int main(int argc, char *argv[]) {
    setup_signal_handlers();
    logfile = fopen("test_log.txt", "w");

    // Setup shared memory for the system clock
    shmid = shmget(IPC_PRIVATE, sizeof(struct Clock), IPC_CREAT | 0666);
    shm_clock = (struct Clock *)shmat(shmid, NULL, 0);
    shm_clock->seconds = shm_clock->nanoseconds = 0;

    // Setup message queue for IPC
    msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);

    // Initialize process table
    for (int i = 0; i < MAX_PROCESSES; i++) processTable[i].occupied = 0;

    // Limits for testing
    int maxProcesses = 3;
    int maxSimultaneousProcesses = 2;
    int totalChildrenLaunched = 0, currentChildren = 0;
    int launchAllowed = 1;

    time_t start_time = time(NULL);

    // Main loop for managing process scheduling
    while ((totalChildrenLaunched < maxProcesses || currentChildren > 0)) {
        if ((time(NULL) - start_time) >= REAL_TIME_LIMIT) {
            launchAllowed = 0;
            printf("Real-time limit reached. No new processes will be launched.\n");
        }

        if (launchAllowed && totalChildrenLaunched < maxProcesses && currentChildren < maxSimultaneousProcesses) {
            create_process(totalChildrenLaunched++);
            currentChildren++;
        }

        int highestPriorityIndex = -1;
        double minRatio = __DBL_MAX__;
        for (int i = readyFront; i != readyRear; i = (i + 1) % MAX_PROCESSES) {
            int pcbIndex = readyQueue[i];
            struct PCB *pcb = &processTable[pcbIndex];

            pcb->serviceTimeNano += 10000;
            add_time(&pcb->serviceTimeSeconds, &pcb->serviceTimeNano, 0);

            double ratio = calculate_priority_ratio(pcb, shm_clock);
            if (ratio < minRatio) {
                minRatio = ratio;
                highestPriorityIndex = pcbIndex;
            }
        }

        if (highestPriorityIndex != -1) {
            int pcbIndex = highestPriorityIndex;
            dequeue(readyQueue, &readyFront);
            struct PCB *pcb = &processTable[pcbIndex];
            struct msg_buffer msg;
            msg.msg_type = pcb->pid;
            msg.msg_data = 50000000;
            if (msgsnd(msgid, &msg, sizeof(msg.msg_data), 0) == -1) {
                perror("msgsnd to worker failed");
                exit(1);
            }

            if (msgrcv(msgid, &msg, sizeof(msg.msg_data), 1, 0) == -1) {
                perror("msgrcv from worker failed");
                exit(1);
            }

            increment_clock(shm_clock, abs(msg.msg_data));
            add_time(&pcb->cpuTimeSeconds, &pcb->cpuTimeNano, abs(msg.msg_data));

            if (msg.msg_data > 0) {
                enqueue(readyQueue, &readyRear, pcbIndex);
            } else {
                waitpid(pcb->pid, NULL, 0);
                pcb->occupied = 0;
                currentChildren--;
            }
        }

        if (!launchAllowed && totalChildrenLaunched >= maxProcesses && currentChildren == 0) {
            printf("All processes completed. Terminating simulation.\n");
            break;
        }
    }

    fclose(logfile);
    cleanup(0);
    return 0;
}

