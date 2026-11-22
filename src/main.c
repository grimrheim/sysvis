#include <stdio.h>
#include <pthread.h>
#include "tracer.h"
#include "syscalls.h"
#include "eventbuf.h"
#include "gui/gui.h"

static EventBuf g_events;

static void on_event(const SyscallEvent *ev) {
    eventbuf_push(&g_events, ev);
}

typedef struct { const char *path; char *const *argv; } TracerArgs;

static void *tracer_thread(void *arg) {
    TracerArgs *ta = arg;
    tracer_run(ta->path, ta->argv, on_event);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <program> [args...]\n", argv[0]);
        return 1;
    }

    eventbuf_init(&g_events);

    TracerArgs ta = { argv[1], argv + 1 };
    pthread_t tid;
    pthread_create(&tid, NULL, tracer_thread, &ta);

    gui_run(&g_events, argv[1]);

    pthread_join(tid, NULL);
    return 0;
}
