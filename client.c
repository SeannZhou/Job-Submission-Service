#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

// #include <fcntl.h> // for open
#include <unistd.h> // for close

#define SOCK_PATH "special_file"

int check_command(char *command);
int hex_int(void *source, int nbytes);

struct job_limits {
    int max_time;
    long max_memory;
    char priority_value;
};

int main(int argc, char *argv[], char *envp[]) {
    int serverfd; // Server info
    struct sockaddr_un server;

    // Obtaining server file descriptor
    if ((serverfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    printf("Trying to connect...\n");
    // Connecting to Server Unix Socket Pipe
    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, SOCK_PATH);

    // Connecting to the Server
    if (connect(serverfd, (struct sockaddr *)&server, sizeof(server)) == -1) {
        perror("connect");
        exit(1);
    }
    printf("Connected.\n");

    // Creating protocol
    int malloc_size = 0;
    // command type number + amount of bytes in message + argc + envp_size + job_limit struct
    malloc_size += 1 + 4 + 1 + 1 + 13;
    
    //new argv[i] sizes
     int amountofflags = 0;  // += 2 to account for excluding flag and value later on
    for(int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0) {
            amountofflags += 2;
            i++;
        } else if (strcmp(argv[i], "-t") == 0) {
            amountofflags += 2;
            i++;
        } else if (strcmp(argv[i], "-p") == 0) {
            amountofflags += 2;
            i++;
        } else {
            // printf("argv[i]: %s\n", argv[i]);
            // Only count for args that aren't job limit flags
            malloc_size += strlen(argv[i]) + 1;
        }
    }
 
    // envp[i] sizes
    for(int i = 0; envp[i] != NULL; i++) {
        malloc_size += strlen(envp[i]) + 1;
    }

    unsigned char *buf = malloc(malloc_size);      // Buffer
    if (buf == NULL) {
        perror("Malloc");
    }
    unsigned char *pointer = buf;

    // Send commands to server and print what is recv
    // byte 1: Command type number
    int command = check_command(argv[1]);
    if (command == -1) {                // If command passed is invalid
        printf("Incorrect command...\n");
        free(buf);
        exit(1);
    }
    char command_t = command + '0'; // Adding command type number
    memcpy(pointer++, &command_t, 1);

    // byte 2-5: Size of whole message
    unsigned char byte1, byte2, byte3, byte4;      //to store byte by byte value
    byte1 = (malloc_size & 0xFF);          //extract first byte
    byte2 = ((malloc_size >> 8) & 0xFF);   //extract second byte
    byte3 = ((malloc_size >> 16) & 0xFF);  //extract third byte
    byte4 = ((malloc_size >> 24) & 0xFF);  //extract fourth byte

    memcpy(pointer++, &byte1, 1);
    memcpy(pointer++, &byte2, 1);
    memcpy(pointer++, &byte3, 1);
    memcpy(pointer++, &byte4, 1);

    // byte 6: argc
    int newargc = argc;
    byte1 = (newargc & 0xFF);          //extract first byte
    memcpy(pointer++, &byte1, 1);

    for (int i = 0; i < argc; i++) {
        if(strcmp(argv[i], "-m") == 0) {
        i++;
    }
    else if(strcmp(argv[i], "-t") ==0) {
        i++;
    }
    else if(strcmp(argv[i], "-p") == 0) {
        i++;
    }
    else {
        memcpy(pointer, argv[i], strlen(argv[i]) +1);
        pointer += strlen(argv[i]) +1;  
        }
    }

    // Copying envp_size
    int envp_size = 0;
    for(int i = 0; envp[i] != NULL; i++) {
        envp_size++;
    }

    char actualsize = envp_size + '0';
    memcpy(pointer++, &actualsize, 1);

    // Copying envp array
    for (int i = 0; envp[i] != NULL; i++) {
        memcpy(pointer, envp[i], strlen(envp[i]) + 1);
        pointer += strlen(envp[i]) + 1;
    }

    // default job_limits
    int max_t = 10;
    long max_mem = 10;
    int prio_num = 0;
    char opt;
    int optind_holder = optind;
    while (optind_holder < argc) {
        if ((opt = getopt(argc, argv, "t:m:p:")) != -1) {
            // Option argument
            switch (opt) {
                case 't':
                        max_t = atoi(optarg);
                        break;
                    case 'm':
                        max_mem = atol(optarg);
                        break;
                    case 'p':
                        prio_num = atoi(optarg);
                        break;
                    default:
                        break;
            }

        } else {
                // Regular argument
                optind_holder++;  // Skip to the next argument
        }
    }

    // Copying job_limit struct
    struct job_limits job = {max_t, max_mem, prio_num};

    // max_time (4 bytes)
    byte1 = (job.max_time & 0xFF);
    byte2 = ((job.max_time >> 8) & 0xFF);
    byte3 = ((job.max_time >> 16) & 0xFF);
    byte4 = ((job.max_time >> 24) & 0xFF);
    memcpy(pointer++, &byte1, 1);
    memcpy(pointer++, &byte2, 1);
    memcpy(pointer++, &byte3, 1);
    memcpy(pointer++, &byte4, 1);

    // max_memory (8 bytes)
    unsigned char byte5, byte6, byte7, byte8;
    byte1 = (job.max_memory & 0xFF);
    byte2 = ((job.max_memory >> 8) & 0xFF);
    byte3 = ((job.max_memory >> 16) & 0xFF);
    byte4 = ((job.max_memory >> 24) & 0xFF);
    byte5 = ((job.max_memory >> 32) & 0xFF);
    byte6 = ((job.max_memory >> 40) & 0xFF);
    byte7 = ((job.max_memory >> 48) & 0xFF);
    byte8 = ((job.max_memory >> 56) & 0xFF);

    memcpy(pointer++, &byte1, 1);
    memcpy(pointer++, &byte2, 1);
    memcpy(pointer++, &byte3, 1);
    memcpy(pointer++, &byte4, 1);
    memcpy(pointer++, &byte5, 1);
    memcpy(pointer++, &byte6, 1);
    memcpy(pointer++, &byte7, 1);
    memcpy(pointer++, &byte8, 1);

    // priority_value (1 byte)
    byte1 = job.priority_value;
    memcpy(pointer++, &byte1, 1);

    // Sending buf
    if (send(serverfd, buf, malloc_size, 0) == -1) {
        perror("send");
        exit(1);
    }

    // Receiving buf
    int readsize;       // Size of read data
    unsigned char recv_buf[2048];             // Buffer string

    if ((readsize = recv(serverfd, recv_buf, 2048, 0)) > 0) {
        recv_buf[readsize] = '\0';
        int message_size = hex_int(&recv_buf[1], 4);

        message_size -= 5;
        for(int i = 0; i < message_size; i++) {
          printf("%c", recv_buf[5+i]);
        }

    } else {
        if (readsize < 0) {
            perror("recv");
        } else {
            printf("Server closed connection\n");
        }
        exit(1);
    }

    // Close out descriptors
    free(buf);
    close(serverfd);

    return 0;
}

int check_command(char *command) {
    if(strcmp(command, "add") == 0) {
        return 1;
    }
    if(strcmp(command, "list") == 0) {
        return 2;
    }
    if(strcmp(command, "kill") == 0) {
        return 3;
    }
    if(strcmp(command, "change") == 0) {
        return 4;
    }
    if(strcmp(command, "get") == 0) {
        return 5;
    }

  return -1;
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