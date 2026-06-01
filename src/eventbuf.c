#include "eventbuf.h"

void eventbuf_init(EventBuf *eb) {
    eb->head = 0;
    eb->tail = 0;
    eb->count = 0;
    pthread_mutex_init(&eb->lock, NULL);
}

int eventbuf_push(EventBuf *eb, const SyscallEvent *ev) {
    pthread_mutex_lock(&eb->lock);
    eb->buf[eb->tail] = *ev;
    if (eb->count == EVENTBUF_SIZE) 
        eb->head = (eb->head + 1) % EVENTBUF_SIZE;
    else 
        eb->count++;
    eb->tail = (eb->tail + 1) % EVENTBUF_SIZE;
    pthread_mutex_unlock(&eb->lock);
    return 1;
}

int eventbuf_pop(EventBuf *eb, SyscallEvent *out) {
    pthread_mutex_lock(&eb->lock);
    if (eb->count == 0) {
        pthread_mutex_unlock(&eb->lock);
        return 0;
    }
    *out = eb->buf[eb->head];
    eb->head = (eb->head + 1) % EVENTBUF_SIZE;
    eb->count--;
    pthread_mutex_unlock(&eb->lock);
    return 1;
}