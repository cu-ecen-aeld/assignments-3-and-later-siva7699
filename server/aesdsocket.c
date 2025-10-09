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

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int server_fd = -1;
int client_fd = -1;
FILE *data_fp = NULL;

void cleanup() {
    if (client_fd != -1) close(client_fd);
    if (server_fd != -1) close(server_fd);
    if (data_fp) fclose(data_fp);
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

int main(int argc, char *argv[]) {
    // Truncate the data file at startup
    int fd_trunc = open(DATA_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_trunc != -1) close(fd_trunc);
    int daemon_mode = 0;
    // Parse arguments for -d
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
    char buffer[BUFFER_SIZE];
    char client_ip[INET_ADDRSTRLEN];


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

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }

        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        data_fp = fopen(DATA_FILE, "a+");
        if (!data_fp) {
            syslog(LOG_ERR, "File open failed: %s", strerror(errno));
            close(client_fd);
            continue;
        }

        size_t packet_size = 0;
        size_t packet_capacity = 0;
        char *packet = NULL;
        int found_newline = 0;
        while (!found_newline) {
            ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) break;
            buffer[bytes_received] = '\0';
            // Find if this buffer contains a newline
            char *newline_ptr = strchr(buffer, '\n');
            size_t to_copy = bytes_received;
            if (newline_ptr) {
                found_newline = 1;
                to_copy = (newline_ptr - buffer) + 1;
            }
            // Expand packet buffer as needed
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
            fseek(data_fp, 0, SEEK_END);
            fwrite(packet, 1, packet_size, data_fp);
            fflush(data_fp);
            free(packet);
            packet = NULL;
            packet_size = 0;
            packet_capacity = 0;
        }
        // Now send back the full file contents
        fseek(data_fp, 0, SEEK_SET);
        while (1) {
            size_t n = fread(buffer, 1, BUFFER_SIZE, data_fp);
            if (n == 0) break;
            send(client_fd, buffer, n, 0);
        }

        fclose(data_fp);
        data_fp = NULL;

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
        client_fd = -1;
    }

    cleanup();
    return 0;
}
