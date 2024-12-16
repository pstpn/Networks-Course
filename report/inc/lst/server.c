#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>

#define PORT 8080
#define MAX_THREADS 11
#define QUEUE_SIZE 64
#define ROOT_DIR "./static"
#define MAX_BUFFER 4096
#define LOG_BUFFER 512

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

int task_queue[QUEUE_SIZE];
int queue_start = 0,
    queue_end = 0,
    queue_count = 0;

FILE *log_file = NULL;
int server_fd;

int endswith(const char *str, const char *suffix) {
    if (!str || !suffix)
        return 0;

    const size_t str_len = strlen(str);
    const size_t suffix_len = strlen(suffix);
    if (suffix_len >  str_len)
        return 0;

    return strncmp(
               str + str_len - suffix_len,
               suffix,
               suffix_len) == 0;
}

void log_event(const char *message) {
    pthread_mutex_lock(&queue_mutex);
    if (log_file) {
        const time_t now = time(NULL);
        fprintf(log_file, "[%s] %s\n",
                strtok(ctime(&now), "\n"),
                message);
        fflush(log_file);
    }
    pthread_mutex_unlock(&queue_mutex);
}

void cleanup() {
    if (log_file)
        fclose(log_file);
    if (server_fd > 0)
        close(server_fd);

    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&queue_cond);
}

void handle_signal(const int sig) {
    if (sig == SIGINT || sig == SIGKILL) {
        log_event("Server shutting down...");
        cleanup();
        exit(0);
    }
}

void enqueue_task(const int fd) {
    pthread_mutex_lock(&queue_mutex);
    if (queue_count == QUEUE_SIZE) {
        pthread_mutex_unlock(&queue_mutex);
        close(fd);
        return;
    }

    task_queue[queue_end] = fd;
    queue_end = (queue_end + 1) % QUEUE_SIZE;
    queue_count++;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
}

int dequeue_task() {
    pthread_mutex_lock(&queue_mutex);
    while (queue_count == 0)
        pthread_cond_wait(
          &queue_cond,
          &queue_mutex);

    const int fd = task_queue[queue_start];
    queue_start = (queue_start + 1) % QUEUE_SIZE;
    --queue_count;
    pthread_mutex_unlock(&queue_mutex);

    return fd;
}

void send_response(const int fd,
                   const int status,
                   const char *status_text,
                   const char *content_type,
                   const char *body,
                   const size_t body_length) {
    char header[MAX_BUFFER];
    const int header_length = snprintf(header, MAX_BUFFER,
        "HTTP/1.1 %d %s\r\n"
        "Content-Length: %zu\r\n"
        "Content-Type: %s\r\n"
        "Connection: close\r\n\r\n",
        status, status_text, body_length, content_type);

    write(fd, header, header_length);
    if (body && body_length > 0)
        write(fd, body, body_length);
    close(fd);

    char log_msg[LOG_BUFFER];
    snprintf(log_msg, sizeof(log_msg),
             "Response: %d %s", status, status_text);
    log_event(log_msg);
}

void serve_static_file(const int fd, const char *file_path) {
    struct stat file_stat;
    if (stat(file_path, &file_stat) == -1 ||
        S_ISDIR(file_stat.st_mode)) {
        send_response(fd, 404, "Not Found",
                    "text/html",
                    "<h1>404 Not Found</h1>", 22);
        return;
    }

    if (access(file_path, R_OK) == -1) {
        send_response(fd, 403, "Forbidden",
                    "text/html",
                    "<h1>403 Forbidden</h1>", 22);
        return;
    }

    const int file_fd = open(file_path, O_RDONLY);
    if (file_fd == -1) {
        send_response(fd, 500, "Internal Server Error",
                    "text/html",
                    "<h1>500 Internal Error</h1>", 27);
        return;
    }

    char header[MAX_BUFFER];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\nContent-Length: %ld"
             "\r\nContent-Type: %s\r\n"
             "Connection: close\r\n\r\n",
        file_stat.st_size,
        (endswith(file_path, ".html") ? "text/html" :
                                           "text/plain"));
    write(fd, header, strlen(header));

    ssize_t bytes_read;
    char buffer[MAX_BUFFER];
    while ((bytes_read = read(
                file_fd,
                buffer,
                sizeof(buffer))) > 0)
        write(fd, buffer, bytes_read);

    close(file_fd);
    close(fd);

    log_event("Response: 200 OK");
}

void *worker_thread(void *arg) {
    while (1) {
        const int client_fd = dequeue_task();

        char buffer[MAX_BUFFER];
        const ssize_t bytes_read = read(
            client_fd,
            buffer,
            sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            close(client_fd);
            continue;
        }
        buffer[bytes_read] = '\0';

        char method[16], path[256];
        sscanf(buffer, "%15s %255s", method, path);

        char log_msg[LOG_BUFFER];
        snprintf(log_msg, sizeof(log_msg),
                 "Request: %s on %s", method, path);
        log_event(log_msg);

        if (strcmp(method, "GET") != 0 &&
            strcmp(method, "HEAD") != 0) {
            send_response(client_fd, 405,
                        "Method Not Allowed",
                        "text/html",
                        "<h1>405 Method Not Allowed</h1>",
                        31);
            continue;
        }

        if (strlen(path) == 1 && path[0] == '/')
            strncpy(path, "/index.html", 11);

        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s%s",
                 ROOT_DIR, path);
        serve_static_file(client_fd, file_path);
    }
}

int main() {
    signal(SIGINT, handle_signal);
    log_file = fopen("server.log", "a");
    if (!log_file) {
        perror("Failed to open log file");
        return 1;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("Bind failed");
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("Listen failed");
        return 1;
    }

    char log_msg[LOG_BUFFER];
    snprintf(log_msg, sizeof(log_msg),
             "Server starting on :%d", PORT);
    log_event(log_msg);

    pthread_t threads[MAX_THREADS];
    for (int i = 0; i < MAX_THREADS; ++i)
        pthread_create(
          &threads[i],
          NULL,
          worker_thread,
          NULL);

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(server_fd, &readfds);
    const int max_fd = server_fd;

    while (1) {
        fd_set temp_set = readfds;
        if (pselect(max_fd + 1, &temp_set,
                    NULL, NULL, NULL, NULL) > 0) {
            if (FD_ISSET(server_fd, &temp_set)) {
                const int client_fd = accept(
                  server_fd,
                  NULL,
                  NULL);
                if (client_fd == -1) {
                    perror("Accept failed");
                    continue;
                }
                enqueue_task(client_fd);
            }
        }
    }
}
