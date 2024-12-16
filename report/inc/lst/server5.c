while (1) {
    fd_set temp_set = readfds;
    if (pselect(max_fd + 1, &temp_set, NULL, NULL, NULL, NULL) > 0)
        if (FD_ISSET(server_fd, &temp_set)) {
            const int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd == -1) {
                perror("Accept failed");
                continue;
            }
            enqueue_task(client_fd);
        }
}