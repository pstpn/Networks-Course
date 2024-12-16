void send_response(const int fd, const int status, const char *status_text, const char *content_type, const char *body, const size_t body_length) {
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
    snprintf(log_msg, sizeof(log_msg), "Response: %d %s", status, status_text);
    log_event(log_msg);
}