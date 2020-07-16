#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/un.h>
#include <sys/wait.h>

// #include <sys/unistd.h>
#include <fcntl.h> // for open
#include <unistd.h> // for close

#define SERV_PATH "special_file"

struct job {                    // Struct for each job
    //int job_num;                // Unique job ID
    long max_memory;
    pid_t pid;                  // Process ID
    int command;                // 1: Add   2: list 3: kill
    int process_state;          // 0: nothing   1: running   2: stopped   3: Completed (ready to be reaped)
    char task[30];              // Job command request (job being added)
    int retval;
    // job_limit
    int max_time;
    int job_num;
    //long max_memory;
    char priority_value;
};
struct job *jobs;
static int amount_of_jobs = 0;      // Current amount of jobs
static int max_jobs = 5;          //Default maximum jobs -- changed wih input

// List modifying functions
int sendSignal(unsigned char *buf, int job_num, int killsignal);
int extracting_parameters(char **arg_arr, int argc, char **parameters);

// List accessor functions
int listJobs(unsigned char *buf);
int addJob(unsigned char *buf, struct job current_job);
int getJob(unsigned char *buf, int job_num, int type);

// List helper functions
void initjobs();
void clearjob(struct job *job);
int deletejob(int job_num);
int addjob(struct job jb);
void list_jobs();
struct job *getjobjobnum(int job_num);
struct job *getjobpid(pid_t pid);

// Signal functions
typedef void sigfunc(int);
sigfunc *Signal(int signum, sigfunc *handler);
void sigchld_handler(int sig);

// Helper functions
pid_t Fork(void);
void freeArray(char **ptr, int size);
int hex_int(void *source, int nbytes);

int main(int argc, char *argv[]) {
    // Default amount of jobs set to 5
    if (argv[1] != NULL) {
        max_jobs = atoi(argv[1]);
    }

    Signal(SIGCHLD, sigchld_handler);   // terminated or stopped child

    int serverfd; // Server info
    struct sockaddr_un server;       // Server sockets

    // Create socket
    if ((serverfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    // Setting server socket struct
    server.sun_family = AF_UNIX;                                    // Unix socket
    strcpy(server.sun_path, SERV_PATH);                             // Copy server path
    
    unlink(server.sun_path);

    // Bind server file descriptor to socket
    if (bind(serverfd, (struct sockaddr *)&server, sizeof(server)) == -1) {
        perror("bind");
        exit(1);
    }

    // Create job list
    jobs = (struct job*)malloc(sizeof(struct job) * max_jobs);
    if (jobs == NULL) {
        perror("malloc");
    }

    initjobs(jobs); // Initialize job list

    // Set server fd to listen
    if (listen(serverfd, max_jobs) == -1) {
        perror("listen");
        exit(1);
    }

    struct sockaddr_un client;       // Client sockets
    int clientfd, csocklen;    // Client info
    unsigned char buf[4096];             // Buffer string
    // Wait and accept client request
    while(1) {
        int done, readsize;
        printf("Waiting for a connection...\n");
        csocklen = sizeof(client);
        if ((clientfd = accept(serverfd, (struct sockaddr *)&client, (socklen_t *)&csocklen)) == -1) {
            perror("accept");
            exit(1);
        }
        printf("Connected.\n");

        done = 0;
        do {
            readsize = recv(clientfd, buf, 4096, 0);
            if (readsize <= 0) {
                if (readsize < 0) perror("recv");
                done = 1;
                continue;
            }

            // Command byte
            unsigned char *ptr_buf = buf;
            int command = *ptr_buf - '0';
            ptr_buf++;

            // Message size
            // int message_size = hex_int(ptr_buf, 4);
            ptr_buf += 4;

            // argc value
            int argv_size = hex_int(ptr_buf, 1);
            ptr_buf++;

            // argv
            char **arg_arr = malloc(argv_size * sizeof(char *));
            if (arg_arr == NULL) {
                    perror("malloc");
            }
            for (int i = 0; i < argv_size; i++) {
                arg_arr[i] = strdup((char *)ptr_buf);
                if (arg_arr[i] == NULL) {
                    perror("strdup");
                }
                ptr_buf += strlen((char*)ptr_buf) + 1;
            }

            // envp_size
            int envp_size = *ptr_buf - '0';
            ptr_buf++;

            // envp
            char **envp_arr = malloc(envp_size * sizeof(char *));
            if (envp_arr == NULL) {
                    perror("malloc");
            }
            for (int i = 0; i < envp_size; i++) {
                envp_arr[i] = strdup((char *)ptr_buf);
                if (envp_arr[i] == NULL) {
                    perror("strdup");
                }
                ptr_buf += strlen((char*)ptr_buf) + 1;
            }

            // job_limit values
            int maxjobtime = hex_int(ptr_buf, 4);
            ptr_buf += 4;

            long maxjobmem = (long)hex_int(ptr_buf, 8);
            ptr_buf += 8;

            // unsigned char jobpriority = hex_int(ptr_buf, 1);
            char jobpriority = (char)*ptr_buf;
            ptr_buf += 1;

            // Reset buffer to send back to client
            memset(buf, 0, readsize);
            int send_size = 0;  // Buffer size

                // Add command
            if(command == 1) {
              char *parameters[100];
              int parameter_size = extracting_parameters(arg_arr, argv_size, parameters);

              struct job current_job;
              strcpy(current_job.task, arg_arr[2]);
              current_job.process_state = 1;
              current_job.command = command;
              current_job.job_num = atoi(arg_arr[3]);
              current_job.max_time = maxjobtime;
              current_job.max_memory = maxjobmem;
              current_job.priority_value = jobpriority;

                char bincommand[100];       // Command with to hold /bin/ before concat
                strcpy(bincommand, "/bin/");
                strcat(bincommand, parameters[0]);

                int pipefd[2];
                if (pipe(pipefd) == -1) {
                    perror("pipe");
                }

              pid_t forkpid = Fork();
              if (forkpid == 0) {   // Child process

                close(pipefd[0]);   // Close read pipe
                dup2(pipefd[1], 1); // Set stdout to write into pipe
                dup2(pipefd[1], 2); // Set stderr to write into pipe
                close(pipefd[1]);   // No longer need pipe write descriptor

                // Only run file if it exists
                struct stat stats;
                stat(parameters[0], &stats);

                if (S_ISREG(stats.st_mode) == 1) {
                    execv(parameters[0], parameters);
                } else {    // If file does not exist, run bin commmand
                    execv(bincommand, parameters);
                }
                freeArray(arg_arr, argv_size);
                freeArray(envp_arr, envp_size);
                free(arg_arr);
                free(envp_arr);
                freeArray(parameters, parameter_size);
                exit(1);

              } else {  // Parent process
                    current_job.pid = forkpid;

                    char pipe_buf[1024];
                    close(pipefd[1]);
                    memset(pipe_buf, 0, 1024);
                    int amtread = 0;

                    if(strcmp(parameters[0], "sleep") != 0) {
                      amtread = read(pipefd[0], pipe_buf, sizeof(pipe_buf));
                    }

                    send_size = addJob(buf, current_job);
                    memcpy(&buf[send_size], pipe_buf, amtread);
                    send_size += amtread;

                    unsigned char byte1, byte2, byte3, byte4;      //to store byte by byte value
                    byte1 = (send_size & 0xFF);          //extract first byte
                    byte2 = ((send_size >> 8) & 0xFF);   //extract second byte
                    byte3 = ((send_size >> 16) & 0xFF);  //extract third byte
                    byte4 = ((send_size >> 24) & 0xFF);  //extract fourth byte

                    memcpy(&buf[1], &byte1, 1);
                    memcpy(&buf[2], &byte2, 1);
                    memcpy(&buf[3], &byte3, 1);
                    memcpy(&buf[4], &byte4, 1);
              }

                freeArray(parameters, parameter_size);

                // list command
            } else if (command == 2) {
                send_size = listJobs(buf);

                // kill command
            } else if(command == 3) {
                // send sig by job id

                int jobnum = atoi(arg_arr[2]);
                int killsignal = atoi(arg_arr[3]);
                send_size = sendSignal(buf, jobnum, killsignal);
                // change command
            } else if(command == 5) {
                send_size = getJob(buf, atoi(arg_arr[2]), atoi(arg_arr[3]));
            }

            // Free argv and envp variables
            freeArray(arg_arr, argv_size);
            freeArray(envp_arr, envp_size);
            free(arg_arr);
            free(envp_arr);

            if (!done) {
                if (send(clientfd, buf, send_size, 0) < 0) {
                    perror("send");
                    done = 1;
                }
            }
        } while(!done);
        
        close(clientfd);
    }
    free(jobs);
    return 0;
}

int sendSignal(unsigned char *buf, int job_num, int killsignal) {
    struct job *jobtosignal = getjobjobnum(job_num);
    char str_msg[512];
    int msg_len = 0;
    unsigned char *ptr = buf + 5;


    // Job doesn't exist
    if (jobtosignal == NULL) {
        buf[0] = 1 + '0';
        msg_len = sprintf(str_msg, "Job [%d] not found.\n", job_num);
        memcpy(&ptr[0], str_msg, msg_len);
        msg_len += 5;
        buf[1] = msg_len;
        return msg_len;
    }

        // SIGSTOP
    if (killsignal == 0) {
        if(kill(jobtosignal->pid, SIGSTOP) == 0) {
            jobtosignal->process_state = 2;

            buf[0] = 0 + '0';
            msg_len = sprintf(str_msg, "Job [%d] has been stopped.\n", job_num);
            memcpy(&ptr[0], str_msg, msg_len);
            msg_len += 5;
            buf[1] = msg_len;
            return msg_len;
        }

        // SIGCONT
    } else if (killsignal == 1) {
        if(kill(jobtosignal->pid, SIGCONT) == 0) {
            jobtosignal->process_state = 1;

            buf[0] = 0 + '0';
            msg_len = sprintf(str_msg, "Job [%d] has been continued.\n", job_num);
            memcpy(&ptr[0], str_msg, msg_len);
            msg_len += 5;
            buf[1] = msg_len;
            return msg_len;
        }

        // SIGINT
    } else if (killsignal == 2) {
        if(kill(jobtosignal->pid, SIGINT) == 0) {
            buf[0] = 0 + '0';
            if (deletejob(job_num) == -1) {
                msg_len = sprintf(str_msg, "ERROR: cannot delete job...\n");
            } else {
                msg_len = sprintf(str_msg, "Job [%d] has been terminated from the list.\n", job_num);  
            } 
            memcpy(&ptr[0], str_msg, msg_len);
            msg_len += 5;
            buf[1] = msg_len;
            return msg_len;
        }
        else {
          buf[0] = 0 + '0';
          if (deletejob(job_num) == -1) {
            msg_len = sprintf(str_msg, "ERROR: cannot delete job...\n");
          } else {
            msg_len = sprintf(str_msg, "Job [%d] has been deleted from the list.\n", job_num);  
          }
          memcpy(&ptr[0], str_msg, msg_len);
          msg_len += 5;
          buf[1] = msg_len;
          return msg_len;
        }

    }
    buf[0] = 1 + '0';
    msg_len = sprintf(str_msg, "Signal to job [%d] did not send. The errno is %d.\n", job_num, errno);
    memcpy(&ptr[0], str_msg, msg_len);
    msg_len += 5;
    buf[1] = msg_len;
    return msg_len;
}

// 1) add <job> <job ID> -t <maxjobtime>  -m <maxjobmem> -p <jobpriority>
// For extracting job arguments excluding server command flags
int extracting_parameters(char **arg_arr, int argc, char **parameters) {
  parameters[0] = strdup(arg_arr[2]); //takes the job
  int parameter_counter = 1;
  if(parameters[0] == NULL) {
    perror("strdup");
  }
  // printf();

  for(int i = 4; i < argc; i++) {
    if(strcmp(arg_arr[i], "-t") == 0) {
      i++;
      continue;
    } else if(strcmp(arg_arr[i], "-m") == 0) {
      i++;
      continue;
    } else if(strcmp(arg_arr[i], "-p") == 0) {
      i++;
      continue;
    }

    parameters[parameter_counter] = strdup(arg_arr[i]);
    if(parameters[parameter_counter] == NULL) {
      perror("strdup");
    }
    parameter_counter++;
  }

  parameters[parameter_counter] = NULL;
  return parameter_counter;
}

// Job functions
int listJobs(unsigned char *buf) {
    buf[0] = 0 + '0';
    // build a string
    int size_msg = 0;       // Hold total message size
    char job_data[512];

    unsigned char *ptr = buf + 5;
    int size_str = 0;

    int isCompletedJob = 0;     // If completed jobs exist in list
    for (int i = 0; i < max_jobs; i++) {
        if (jobs[i].command != 0) {
            printf("still a job: %s\n", jobs[i].task);
            isCompletedJob = 1;
            break;
        }
    }

    if (amount_of_jobs == 0 && isCompletedJob == 0) {
        size_str += sprintf(job_data, "There are currently no jobs...\n");
        // printf("size_str: %d\n", size_str);
        memcpy(&ptr[size_msg], job_data, size_str);
        size_msg += size_str;
        size_msg += 5;
        buf[1] = size_msg;
        return size_msg;
    }

    // Traverse through job list to append active jobs
    for (int i = 0; i < max_jobs; i++) {
        // If active job
        if (jobs[i].command != 0) {
            // printf("task: %s\n", jobs[i].task);
            switch (jobs[i].process_state) {
                case 1:
                    size_str = sprintf(job_data, "[%d]\tRunning\t\t%s\n", jobs[i].job_num, jobs[i].task);
                    break;
                case 2:
                    size_str = sprintf(job_data, "[%d]\tStopped\t\t%s\n", jobs[i].job_num, jobs[i].task);
                    break;
                case 3:
                    size_str = sprintf(job_data, "[%d]\tCompleted\t%s\n", jobs[i].job_num, jobs[i].task);
                    break;
                default:
                    size_str = 0;
                    break;
            }
            memcpy(&ptr[size_msg], job_data, size_str);
            size_msg += size_str;
        }
    }
    size_msg += 5;
    buf[1] = size_msg;

    return size_msg;
}

// Add new job to list
int addJob(unsigned char *buf, struct job current_job) {
  addjob(current_job);
  int size_msg = 25;
                                                    // bytes added
  buf[0] = 0 + '0';                                 // 1
  // buf[1] = size_msg;                                // 4
  strcpy((char*)&buf[5], "Job has been added.\n");  // 20

  return size_msg;
}

// Get job status/retval
int getJob(unsigned char *buf, int job_num, int type) {
  int size_msg = 0;
  buf[0] = 0 + '0';

  int size_str = 0;
  char job_data[512];

  unsigned char *ptr = buf + 5;

  for(int i = 0; i < max_jobs; i++) {
    if(jobs[i].job_num == job_num) {
      //the status - running or stopped
      if(type == 1) {
        switch(jobs[i].process_state) {
          case 1:
            size_str = sprintf(job_data, "[%d] status is RUNNING.\n", jobs[i].job_num);
            break;
          case 2:
            size_str = sprintf(job_data, "[%d] status is STOPPED.\n", jobs[i].job_num);
            break;
        case 3:
            size_str = sprintf(job_data, "[%d] status is COMPLETED.\n", jobs[i].job_num);
            break;
          default:
            break;
        }
      }
      if(type == 2) {
        size_str = sprintf(job_data, "[%d] return value/error value is %d.\n", jobs[i].job_num, jobs[i].retval);
      }
    }
  }
  memcpy(&ptr[0], job_data, size_str);

  size_msg = 5 + size_str;
  buf[1] = size_msg;

  return size_msg;
}

// List functions
// Clear all jobs
void initjobs() {
  for(int i = 0; i < max_jobs; i++) {
    clearjob(&jobs[i]);
  }
}

void clearjob(struct job *job) {
  job->job_num = 0;
  job->process_state = 0;
  job->command = 0;
  job->pid = 0;
  job->retval = 0;
  strcpy(job->task, "");
  job->max_time = 10;
  job->max_memory = 10;
  job->priority_value = 0;
}

// Deleting jobs
int deletejob(int job_num) {
    for(int i = 0; i < max_jobs; i++) {
      if(jobs[i].job_num == job_num) {
        clearjob(&jobs[i]);
        return 0;
      }
    }
    return -1;
}

// Add a job to the job list
int addjob(struct job jb) {
  if(amount_of_jobs == max_jobs) {
    printf("The limit for the amount of jobs has been met.\n");
    return -1;
  }

  // Prioritize empty job slots first
  for(int i = 0; i < max_jobs; i++) {
    if(jobs[i].command == 0) {
      jobs[i].job_num = jb.job_num;
      jobs[i].pid = jb.pid;
      jobs[i].process_state = jb.process_state;
      jobs[i].command = jb.command;
      strcpy(jobs[i].task, jb.task);

      jobs[i].max_time = jb.max_time;
      jobs[i].max_memory = jb.max_memory;
      jobs[i].priority_value = jb.priority_value;
      amount_of_jobs++;
      return 0;
    }
  }

  // Else check for finished jobs
  for(int i = 0; i < max_jobs; i++) {
    if(jobs[i].process_state == 3) {
      jobs[i].job_num = jb.job_num;
      jobs[i].pid = jb.pid;
      jobs[i].process_state = jb.process_state;
      jobs[i].command = jb.command;
      strcpy(jobs[i].task, jb.task);
      jobs[i].max_time = jb.max_time;
      jobs[i].max_memory = jb.max_memory;
      jobs[i].priority_value = jb.priority_value;
      amount_of_jobs++;
      return 0;
    }
  }

  return -1;
}

void list_jobs() {
  if(amount_of_jobs == 0) {
    printf("There are currently no jobs.\n");
    return;
  }

  for(int i = 0; i < max_jobs; i++) {
    if(jobs[i].command != 0) {
      switch(jobs[i].command) {
        case 1:
          printf("[JOB %d with command ADD]:  \n", jobs[i].job_num);
          break;
        case 2:
          printf("[JOB %d with command LIST]:  \n", jobs[i].job_num);
          break;
        case 3:
          printf("[JOB %d with command KILL]:  \n", jobs[i].job_num);
          break;
        default:
          break;
      }
    }
  }
}

// Get job by job id
struct job *getjobjobnum(int job_num) {
    // Bad PIDs
    if (job_num < 1) {
        return NULL;
    }
    for (int i = 0; i < max_jobs; i++) {
        if (jobs[i].job_num == job_num) {
            return &jobs[i];
        }
    }
    return NULL;
}

// Gets a job by pid
struct job *getjobpid(pid_t pid) {
    // Bad PIDs
    if (pid < 1) {
        return NULL;
    }
    for (int i = 0; i < max_jobs; i++) {
        if (jobs[i].pid == pid) {
            return &jobs[i];
        }
    }
    return NULL;
}

// Wrapper function for signal
sigfunc *Signal(int signum, sigfunc *handler)  {
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0) {
        fprintf(stderr, "Error: signal error %d\n", errno);
    }

    return (old_action.sa_handler);
}

// Sigchild handler to catch retval
void sigchld_handler (int sig) {
    int pid, status, serrno, returnval;
    serrno = errno;
    while (1) {
        pid = waitpid (WAIT_ANY, &status, WNOHANG);
        returnval = WEXITSTATUS(status);
       
        if (pid <= 0) {
            break;
        } else {
            struct job *jobreceived = getjobpid(pid);
            jobreceived->retval = returnval;        // Add retval to job
            jobreceived->process_state = 3;         // Change job process to Completed
            printf("ENDED: %s (ret=%d)\n", jobreceived->task, returnval);
            amount_of_jobs--;
        }
    }
    errno = serrno;
}

// Helper functions
pid_t Fork(void) {
    pid_t pid;
    if((pid = fork()) < 0) {
        fprintf(stderr, "Error: fork error %d\n", errno);
    }
  return pid;
}

void freeArray(char **ptr, int size) {
    for (int i = 0; i < size; i++) {
        free(ptr[i]);
    }
}

int hex_int(void *source, int nbytes) {
    int num = 0, multi = 1;

    for(int i = 0; i != nbytes; i++)
    {
        num += multi * ((unsigned char*) source)[i];
        multi <<= 8;
    }
    return num;
}
