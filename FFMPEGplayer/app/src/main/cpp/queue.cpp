//
// Created by charles on 2024/8/17.
//
#include "ffmpegplayer/queue.h"

/**
 * @param queue
 */
void queue_init(Queue* queue) {
    queue->size = 0;
    queue->head = nullptr;
    queue->tail = nullptr;
    queue->is_block = true;
    queue->mutex_id = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(queue->mutex_id, nullptr);
    queue->not_empty_condition = (pthread_cond_t*) malloc(sizeof(pthread_cond_t));
    pthread_cond_init(queue->not_empty_condition, nullptr);
    queue->not_full_condition = (pthread_cond_t*) malloc(sizeof(pthread_cond_t));
    pthread_cond_init(queue->not_full_condition, nullptr);
}

/**
 * @param queue
 */
void queue_destroy(Queue* queue) {
    Node* node = queue->head;
    while (node != nullptr) {
        queue->head = queue->head->next;
        free(node);
        node = queue->head;
    }
    queue->head = nullptr;
    queue->tail = nullptr;
    queue->size = 0;
    queue->is_block = false;
    pthread_mutex_destroy(queue->mutex_id);
    pthread_cond_destroy(queue->not_empty_condition);
    pthread_cond_destroy(queue->not_full_condition);
    free(queue->mutex_id);
    free(queue->not_empty_condition);
    free(queue->not_full_condition);
}

/**
 * @param queue
 * @return
 */
bool queue_is_empty(Queue* queue) {
    return queue->size == 0;
}

/**
 * @param queue
 * @return
 */
bool queue_is_full(Queue* queue) {
    return queue->size == QUEUE_MAX_SIZE;
}

/**
 * @param queue
 * @param element
 */
void queue_in(Queue* queue, NodeElement element) {
    pthread_mutex_lock(queue->mutex_id);
    while (queue_is_full(queue) && queue->is_block) {
        pthread_cond_wait(queue->not_full_condition, queue->mutex_id);
    }
    if (queue->size >= QUEUE_MAX_SIZE) {
        pthread_mutex_unlock(queue->mutex_id);
        return;
    }
    Node* node = (Node*) malloc(sizeof(Node));
    node->data = element;
    node->next = nullptr;
    if (queue->tail == nullptr) {
        queue->head = node;
        queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
    queue->size += 1;
    pthread_cond_signal(queue->not_empty_condition);
    pthread_mutex_unlock(queue->mutex_id);
}

/**
 * @param queue
 * @return
 */
NodeElement queue_out(Queue* queue) {
    pthread_mutex_lock(queue->mutex_id);
    while (queue_is_empty(queue) && queue->is_block) {
        pthread_cond_wait(queue->not_empty_condition, queue->mutex_id);
    }
    if (queue->head == nullptr) {
        pthread_mutex_unlock(queue->mutex_id);
        return nullptr;
    }
    Node* node = queue->head;
    queue->head = queue->head->next;
    if (queue->head == nullptr) {
        queue->tail = nullptr;
    }
    NodeElement element = node->data;
    free(node);
    queue->size -= 1;
    pthread_cond_signal(queue->not_full_condition);
    pthread_mutex_unlock(queue->mutex_id);
    return element;
}

/**
 * @param queue
 */
void queue_clear(Queue* queue) {
    pthread_mutex_lock(queue->mutex_id);
    Node* node = queue->head;
    while (node != nullptr) {
        queue->head = queue->head->next;
        free(node);
        node = queue->head;
    }
    queue->head = nullptr;
    queue->tail = nullptr;
    queue->size = 0;
    queue->is_block = true;
    pthread_cond_signal(queue->not_full_condition);
    pthread_mutex_unlock(queue->mutex_id);
}

/**
 * @param queue
 */
void break_block(Queue* queue) {
    queue->is_block = false;
    pthread_cond_signal(queue->not_empty_condition);
    pthread_cond_signal(queue->not_full_condition);
}