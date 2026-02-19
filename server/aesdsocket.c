
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int server_fd = -1;
int client_fd = -1;
FILE *data_fp = NULL;

typedef struct thread_data{
    pthread_t thread;
    struct sockaddr_in client_addr;
    int client_fd;
    pthread_mutex_t *mutex;
    bool thread_complete_success;
    struct thread_data *thread_data_next;
} thread_data_t;

int start_thread_obtaining_mutex(thread_data_t **thread_data, pthread_mutex_t *mutex, int client_fd, struct sockaddr_in client_addr);

void cleanup() {
    if (client_fd != -1) {
        close(client_fd);
    }
    if (server_fd != -1) {
        close(server_fd);
    }
    if (data_fp) {
        fclose(data_fp);
    }
    closelog();
}

void cleanup_and_remove_file() {
    cleanup();
    remove(DATA_FILE);
}

void signal_handler(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    cleanup_and_remove_file();
    exit(0);
}

// Handle a single client connection: receive until newline, append to
// data file, then send back full file contents and close connection.
static void handle_client(thread_data_t *thread_data)
{   
    pthread_mutex_lock(thread_data->mutex);
    int client_fd = thread_data->client_fd;
    struct sockaddr_in *client_addr = &thread_data->client_addr;
    char buffer[BUFFER_SIZE];
    char client_ip[INET_ADDRSTRLEN];
    FILE *fp = NULL;

    inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, sizeof(client_ip));
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    fp = fopen(DATA_FILE, "a+");
    if (!fp) {
        syslog(LOG_ERR, "File open failed: %s", strerror(errno));
        close(client_fd);
        return;
    }

    size_t packet_size = 0;
    size_t packet_capacity = 0;
    char *packet = NULL;
    int found_newline = 0;

    while (!found_newline) {
        ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0)
            break;
        buffer[bytes_received] = '\0';
        char *newline_ptr = strchr(buffer, '\n');
        size_t to_copy = bytes_received;
        if (newline_ptr) {
            found_newline = 1;
            to_copy = (newline_ptr - buffer) + 1;
        }

        if (packet_size + to_copy + 1 > packet_capacity) {
            packet_capacity = packet_size + to_copy + 1;
            char *new_packet = realloc(packet, packet_capacity);
            if (!new_packet) {
                syslog(LOG_ERR, "Memory allocation failed");
                free(packet);
                packet = NULL;
                break;
            }
            packet = new_packet;
        }
        memcpy(packet + packet_size, buffer, to_copy);
        packet_size += to_copy;
        packet[packet_size] = '\0';
    }

    if (packet && packet_size > 0) {
        fseek(fp, 0, SEEK_END);
        fwrite(packet, 1, packet_size, fp);
        fflush(fp);
        free(packet);
        packet = NULL;
    }

    fseek(fp, 0, SEEK_SET);
    while (1) {
        size_t n = fread(buffer, 1, BUFFER_SIZE, fp);
        if (n == 0)
            break;
        send(client_fd, buffer, n, 0);
    }

    fclose(fp);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    close(client_fd);
    pthread_mutex_unlock(thread_data->mutex);
    thread_data->thread_complete_success = true;
}

void timestamp_thread_function(pthread_mutex_t *mutex) {
    while (1) {
        sleep(10);
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%a %b %d %H:%M:%S %Y", tm_info);

        pthread_mutex_lock(mutex);
        FILE *fp = fopen(DATA_FILE, "a");
        if (fp) {
            fprintf(fp, "timestamp: %s\n", timestamp);
            fclose(fp);
        } else {
            syslog(LOG_ERR, "Timestamp file open failed: %s", strerror(errno));
        }
        pthread_mutex_unlock(mutex);
    }
}

int start_thread_obtaining_mutex(thread_data_t **thread_data, pthread_mutex_t *mutex, int client_fd, struct sockaddr_in client_addr)
{
    thread_data_t *new_thread_data = malloc(sizeof(thread_data_t));
    if (!new_thread_data) {
        syslog(LOG_ERR, "Memory allocation failed");
        return -1;
    }
    new_thread_data->client_fd = client_fd;
    new_thread_data->client_addr = client_addr;
    new_thread_data->mutex = mutex;
    new_thread_data->thread_complete_success = false;
    new_thread_data->thread_data_next = NULL;

    if (pthread_create(&new_thread_data->thread, NULL, (void *(*)(void *))handle_client, new_thread_data) != 0) {
        syslog(LOG_ERR, "Thread creation failed: %s", strerror(errno));
        free(new_thread_data);
        return -1;
    }

    // Add to linked list
    new_thread_data->thread_data_next = *thread_data;
    *thread_data = new_thread_data;

    return 0;
}

int main(int argc, char *argv[]) {
    // Truncate the data file at startup
    int fd_trunc = open(DATA_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_trunc != -1) close(fd_trunc);

    int daemon_mode = 0;
    int opt;
    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
            case 'd':
                daemon_mode = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    openlog("aesdsocket", LOG_PID, LOG_USER);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        cleanup();
        return -1;
    }

    // Daemonize if requested
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
            cleanup();
            return -1;
        }
        if (pid > 0) {
            // Parent exits
            exit(0);
        }
        // Child continues
        // Create new session and process group
        if (setsid() < 0) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            cleanup();
            return -1;
        }
        // Change working directory to root
        if (chdir("/") < 0) {
            syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
            cleanup();
            return -1;
        }
        // Redirect standard file descriptors to /dev/null
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);
        open("/dev/null", O_RDONLY); // stdin
        open("/dev/null", O_WRONLY); // stdout
        open("/dev/null", O_RDWR);   // stderr
    }

    if (listen(server_fd, 10) == -1) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        cleanup();
        return -1;
    }
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    thread_data_t* head=NULL;
    pthread_t timestamp_thread;

    pthread_create(&timestamp_thread, NULL, (void *(*)(void *))timestamp_thread_function, &mutex);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }
        start_thread_obtaining_mutex(&head, &mutex, client_fd, client_addr);

        for (struct thread_data *current = head, *prev = NULL; current != NULL; ) {
            if (current->thread_complete_success) {
                pthread_join(current->thread, NULL);
                if (prev) {
                    prev->thread_data_next = current->thread_data_next;
                } else {
                    head = current->thread_data_next;
                }
                free(current);
                current = (prev) ? prev->thread_data_next : head;
            } else {
                prev = current;
                current = current->thread_data_next;

            }
        }        

        client_fd = -1;
    }

    cleanup();
    return 0;
}
