void log_event(const char *message) {
    pthread_mutex_lock(&queue_mutex);
    if (log_file) {
        const time_t now = time(NULL);
        fprintf(log_file, "[%s] %s\n", strtok(ctime(&now), "\n"), message);
        fflush(log_file);
    }
    pthread_mutex_unlock(&queue_mutex);
}