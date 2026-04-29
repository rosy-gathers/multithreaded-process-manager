#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_PROCESSES 64

typedef enum {
    STATE_EMPTY = 0,
    STATE_RUNNING,
    STATE_BLOCKED,
    STATE_ZOMBIE,
    STATE_TERMINATED
} proc_state_t;

typedef struct {
    int pid;
    int ppid;
    proc_state_t state;
    int exit_status;
    int children[MAX_PROCESSES];
    int num_children;
    pthread_cond_t wait_cond;
} PCB;

PCB process_table[MAX_PROCESSES];
pthread_mutex_t ptable_mutex;
int next_pid = 1;

__thread int current_thread_id = -1;

typedef struct {
    int pid;
    int ppid;
    proc_state_t state;
    int exit_status;
} PCB_Snapshot;

typedef struct SnapshotNode {
    char action[256];
    PCB_Snapshot table[MAX_PROCESSES];
    int count;
    struct SnapshotNode* next;
} SnapshotNode;

SnapshotNode* snap_head = NULL;
SnapshotNode* snap_tail = NULL;
pthread_mutex_t snap_mutex;
pthread_cond_t snap_cond;
int monitor_done = 0;

void enqueue_snapshot(const char* action) {
    SnapshotNode* node = malloc(sizeof(SnapshotNode));
    if (!node) return;
    strncpy(node->action, action, sizeof(node->action)-1);
    node->action[sizeof(node->action)-1] = '\0';
    
    int count = 0;
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(process_table[i].state != STATE_EMPTY && process_table[i].state != STATE_TERMINATED) {
            node->table[count].pid = process_table[i].pid;
            node->table[count].ppid = process_table[i].ppid;
            node->table[count].state = process_table[i].state;
            node->table[count].exit_status = process_table[i].exit_status;
            count++;
        }
    }
    node->count = count;
    node->next = NULL;
    
    pthread_mutex_lock(&snap_mutex);
    if(snap_tail) {
        snap_tail->next = node;
        snap_tail = node;
    } else {
        snap_head = snap_tail = node;
    }
    pthread_cond_signal(&snap_cond);
    pthread_mutex_unlock(&snap_mutex);
}

void init_process_manager() {
    pthread_mutex_init(&ptable_mutex, NULL);
    for(int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].state = STATE_EMPTY;
        process_table[i].num_children = 0;
        pthread_cond_init(&process_table[i].wait_cond, NULL);
    }
    
    // Init process
    process_table[0].pid = next_pid++;
    process_table[0].ppid = 0;
    process_table[0].state = STATE_RUNNING;
    process_table[0].exit_status = 0;
    process_table[0].num_children = 0;
    
    pthread_mutex_init(&snap_mutex, NULL);
    pthread_cond_init(&snap_cond, NULL);
    
    enqueue_snapshot("Initial Process Table");
}

void pm_fork(int parent_pid) {
    pthread_mutex_lock(&ptable_mutex);
    
    int p_idx = -1;
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(process_table[i].state != STATE_EMPTY && process_table[i].state != STATE_TERMINATED && process_table[i].pid == parent_pid) {
            p_idx = i;
            break;
        }
    }
    
    if (p_idx == -1) {
        pthread_mutex_unlock(&ptable_mutex);
        return; // Parent not found
    }
    
    int free_idx = -1;
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(process_table[i].state == STATE_EMPTY || process_table[i].state == STATE_TERMINATED) {
            free_idx = i;
            break;
        }
    }
    if(free_idx != -1) {
        int new_pid = next_pid++;
        process_table[free_idx].pid = new_pid;
        process_table[free_idx].ppid = parent_pid;
        process_table[free_idx].state = STATE_RUNNING;
        process_table[free_idx].exit_status = 0;
        process_table[free_idx].num_children = 0;
        
        process_table[p_idx].children[process_table[p_idx].num_children++] = new_pid;
        
        char action[256];
        snprintf(action, sizeof(action), "Thread %d calls pm_fork %d", current_thread_id, parent_pid);
        enqueue_snapshot(action);
    }
    pthread_mutex_unlock(&ptable_mutex);
}

void pm_exit(int pid, int status) {
    pthread_mutex_lock(&ptable_mutex);
    int p_idx = -1;
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(process_table[i].state != STATE_EMPTY && process_table[i].state != STATE_TERMINATED && process_table[i].pid == pid) {
            p_idx = i;
            break;
        }
    }
    if (p_idx != -1 && process_table[p_idx].state != STATE_ZOMBIE) {
        process_table[p_idx].state = STATE_ZOMBIE;
        process_table[p_idx].exit_status = status;
        
        char action[256];
        snprintf(action, sizeof(action), "Thread %d calls pm_exit %d %d", current_thread_id, pid, status);
        enqueue_snapshot(action);
        
        int parent_pid = process_table[p_idx].ppid;
        for(int i = 0; i < MAX_PROCESSES; i++) {
            if(process_table[i].state != STATE_EMPTY && process_table[i].state != STATE_TERMINATED && process_table[i].pid == parent_pid) {
                pthread_cond_broadcast(&process_table[i].wait_cond);
                break;
            }
        }
    }
    pthread_mutex_unlock(&ptable_mutex);
}

int pm_wait(int parent_pid, int child_pid) {
    pthread_mutex_lock(&ptable_mutex);
    
    int p_idx = -1;
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(process_table[i].state != STATE_EMPTY && process_table[i].state != STATE_TERMINATED && process_table[i].pid == parent_pid) {
            p_idx = i;
            break;
        }
    }
    if (p_idx == -1) {
        pthread_mutex_unlock(&ptable_mutex);
        return -1;
    }

    int initially_blocked = 0;
    int collected_status = -1;

    while(1) {
        int has_children = 0;
        int z_idx = -1;
        
        for(int i = 0; i < process_table[p_idx].num_children; i++) {
            int c_pid = process_table[p_idx].children[i];
            if (child_pid == -1 || c_pid == child_pid) {
                has_children = 1;
                for(int j = 0; j < MAX_PROCESSES; j++) {
                    if (process_table[j].state != STATE_EMPTY && process_table[j].state != STATE_TERMINATED && process_table[j].pid == c_pid) {
                        if (process_table[j].state == STATE_ZOMBIE) {
                            z_idx = j;
                        }
                        break;
                    }
                }
                if (z_idx != -1) break;
            }
        }
        
        if (!has_children) {
            pthread_mutex_unlock(&ptable_mutex);
            return -1;
        }
        
        if (z_idx != -1) {
            collected_status = process_table[z_idx].exit_status;
            int reaped_pid = process_table[z_idx].pid;
            
            process_table[z_idx].state = STATE_TERMINATED;
            process_table[p_idx].state = STATE_RUNNING;
            
            for(int i = 0; i < process_table[p_idx].num_children; i++) {
                if (process_table[p_idx].children[i] == reaped_pid) {
                    for(int j = i; j < process_table[p_idx].num_children - 1; j++) {
                        process_table[p_idx].children[j] = process_table[p_idx].children[j+1];
                    }
                    process_table[p_idx].num_children--;
                    break;
                }
            }
            
            char action[256];
            snprintf(action, sizeof(action), "Thread %d calls pm_wait %d %d", current_thread_id, parent_pid, child_pid);
            enqueue_snapshot(action);
            
            pthread_mutex_unlock(&ptable_mutex);
            return collected_status;
        }
        
        process_table[p_idx].state = STATE_BLOCKED;
        
        if (!initially_blocked) {
            char action[256];
            snprintf(action, sizeof(action), "Thread %d calls pm_wait %d %d", current_thread_id, parent_pid, child_pid);
            enqueue_snapshot(action);
            initially_blocked = 1;
        }
        
        pthread_cond_wait(&process_table[p_idx].wait_cond, &ptable_mutex);
    }
}

void pm_kill(int pid) {
    pthread_mutex_lock(&ptable_mutex);
    int p_idx = -1;
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(process_table[i].state != STATE_EMPTY && process_table[i].state != STATE_TERMINATED && process_table[i].pid == pid) {
            p_idx = i;
            break;
        }
    }
    if (p_idx != -1 && process_table[p_idx].state != STATE_ZOMBIE) {
        process_table[p_idx].state = STATE_ZOMBIE;
        process_table[p_idx].exit_status = 0; 
        
        char action[256];
        snprintf(action, sizeof(action), "Thread %d calls pm_kill %d", current_thread_id, pid);
        enqueue_snapshot(action);
        
        int parent_pid = process_table[p_idx].ppid;
        for(int i = 0; i < MAX_PROCESSES; i++) {
            if(process_table[i].state != STATE_EMPTY && process_table[i].state != STATE_TERMINATED && process_table[i].pid == parent_pid) {
                pthread_cond_broadcast(&process_table[i].wait_cond);
                break;
            }
        }
    }
    pthread_mutex_unlock(&ptable_mutex);
}

void pm_ps() {
    pthread_mutex_lock(&ptable_mutex);
    printf("%-10s%-11s%-16s%s\n", "PID", "PPID", "STATE", "EXIT_STATUS");
    printf("-------------------------------------------------\n");
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(process_table[i].state != STATE_EMPTY && process_table[i].state != STATE_TERMINATED) {
            const char* state_str = "";
            switch(process_table[i].state) {
                case STATE_RUNNING: state_str = "RUNNING"; break;
                case STATE_BLOCKED: state_str = "BLOCKED"; break;
                case STATE_ZOMBIE: state_str = "ZOMBIE"; break;
                default: break;
            }
            if(process_table[i].state == STATE_ZOMBIE) {
                printf("%-10d%-11d%-16s%d\n", process_table[i].pid, process_table[i].ppid, state_str, process_table[i].exit_status);
            } else {
                printf("%-10d%-11d%-16s-\n", process_table[i].pid, process_table[i].ppid, state_str);
            }
        }
    }
    pthread_mutex_unlock(&ptable_mutex);
}


void* monitor_thread_func(void* arg) {
    (void)arg;
    FILE* fp = fopen("snapshots.txt", "w");
    if (!fp) {
        fprintf(stderr, "Monitor: Cannot open snapshots.txt for writing.\n");
        return NULL;
    }
    
    while(1) {
        pthread_mutex_lock(&snap_mutex);
        while(snap_head == NULL && !monitor_done) {
            pthread_cond_wait(&snap_cond, &snap_mutex);
        }
        
        if (snap_head == NULL && monitor_done) {
            pthread_mutex_unlock(&snap_mutex);
            break;
        }
        
        SnapshotNode* node = snap_head;
        snap_head = node->next;
        if(snap_head == NULL) snap_tail = NULL;
        pthread_mutex_unlock(&snap_mutex);
        
        fprintf(fp, "%s\n", node->action);
        fprintf(fp, "%-10s%-11s%-16s%s\n", "PID", "PPID", "STATE", "EXIT_STATUS");
        fprintf(fp, "-------------------------------------------------\n");
        for(int i = 0; i < node->count; i++) {
            const char* state_str = "";
            switch(node->table[i].state) {
                case STATE_RUNNING: state_str = "RUNNING"; break;
                case STATE_BLOCKED: state_str = "BLOCKED"; break;
                case STATE_ZOMBIE: state_str = "ZOMBIE"; break;
                default: break;
            }
            if(node->table[i].state == STATE_ZOMBIE) {
                fprintf(fp, "%-10d%-11d%-16s%d\n", node->table[i].pid, node->table[i].ppid, state_str, node->table[i].exit_status);
            } else {
                fprintf(fp, "%-10d%-11d%-16s-\n", node->table[i].pid, node->table[i].ppid, state_str);
            }
        }
        fprintf(fp, "\n");
        fflush(fp);
        free(node);
    }
    fclose(fp);
    return NULL;
}


typedef struct {
    int thread_id;
    char script_file[256];
} ThreadArg;

void* worker_thread_func(void* arg) {
    ThreadArg* t_arg = (ThreadArg*)arg;
    current_thread_id = t_arg->thread_id;
    
    FILE* fp = fopen(t_arg->script_file, "r");
    if (!fp) {
        fprintf(stderr, "Thread %d: Cannot open %s\n", current_thread_id, t_arg->script_file);
        return NULL;
    }
    
    char line[256];
    while(fgets(line, sizeof(line), fp)) {
        char cmd[32];
        if (sscanf(line, "%s", cmd) == 1) {
            if (strcmp(cmd, "fork") == 0) {
                int parent;
                if (sscanf(line, "%*s %d", &parent) == 1) {
                    pm_fork(parent);
                }
            } else if (strcmp(cmd, "exit") == 0) {
                int pid, status;
                if (sscanf(line, "%*s %d %d", &pid, &status) == 2) {
                    pm_exit(pid, status);
                }
            } else if (strcmp(cmd, "wait") == 0) {
                int parent, child;
                if (sscanf(line, "%*s %d %d", &parent, &child) == 2) {
                    pm_wait(parent, child);
                }
            } else if (strcmp(cmd, "kill") == 0) {
                int pid;
                if (sscanf(line, "%*s %d", &pid) == 1) {
                    pm_kill(pid);
                }
            } else if (strcmp(cmd, "sleep") == 0) {
                int ms;
                if (sscanf(line, "%*s %d", &ms) == 1) {
                    usleep(ms * 1000);
                }
            }
        }
    }
    
    fclose(fp);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s thread0.txt thread1.txt ...\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    init_process_manager();
    
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, monitor_thread_func, NULL);
    
    int num_threads = argc - 1;
    pthread_t* threads = malloc(num_threads * sizeof(pthread_t));
    ThreadArg* args = malloc(num_threads * sizeof(ThreadArg));
    
    for(int i = 0; i < num_threads; i++) {
        args[i].thread_id = i;
        strncpy(args[i].script_file, argv[i+1], sizeof(args[i].script_file)-1);
        args[i].script_file[sizeof(args[i].script_file)-1] = '\0';
        pthread_create(&threads[i], NULL, worker_thread_func, &args[i]);
    }
    
    for(int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    pthread_mutex_lock(&snap_mutex);
    monitor_done = 1;
    pthread_cond_signal(&snap_cond);
    pthread_mutex_unlock(&snap_mutex);
    
    pthread_join(monitor_thread, NULL);
    
    free(threads);
    free(args);
    
    return EXIT_SUCCESS;
}

