int dequeue_task() {
    pthread_mutex_lock(&queue_mutex);
    while (queue_count == 0)
        pthread_cond_wait(&queue_cond, &queue_mutex);

    const int fd = task_queue[queue_start];
    queue_start = (queue_start + 1) % QUEUE_SIZE;
    --queue_count;
    pthread_mutex_unlock(&queue_mutex);

    return fd;
}