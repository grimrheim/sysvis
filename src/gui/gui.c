#include "gui.h"
#include <ncurses.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <sys/syscall.h>
#include <locale.h>

#define STATUS_LINES  1
#define DETAIL_LINES  6
#define MAX_EVENTS    8192
#define FILTER_ALL    0x1F // 32, биты 0-4 по олному на каждую категорию

/* индексы цветовых пар */
#define CLR_FILE        1
#define CLR_NETWORK     2
#define CLR_PROCESS     3
#define CLR_MEMORY      4
#define CLR_OTHER       5
#define CLR_ERROR       6
#define CLR_STATUS      7
#define CLR_SELECTED    8

static SyscallEvent evlist[MAX_EVENTS];
static int          ev_count   = 0;
static int          selected   = 0;     // выбранная строка среди видимых
static int          scroll_top = 0;     // первая видимая строка
static uint8_t      filt       = FILTER_ALL;
static pid_t        traced_pid = 0;
static int          proc_alive = 1;
static struct timespec gui_start;

static volatile sig_atomic_t resized = 0;
static void handle_sigwinch(int sig) { (void)sig; resized = 1; }

static void init_colors(void) {
    start_color();
    use_default_colors();
    init_pair(CLR_FILE,     COLOR_GREEN,    -1);
    init_pair(CLR_NETWORK,  COLOR_BLUE,     -1);
    init_pair(CLR_PROCESS,  COLOR_YELLOW,   -1);
    init_pair(CLR_MEMORY,   COLOR_MAGENTA,  -1);
    init_pair(CLR_OTHER,    COLOR_WHITE,    -1);
    init_pair(CLR_ERROR,    COLOR_RED,      -1);
    init_pair(CLR_STATUS,   COLOR_BLACK,    COLOR_WHITE);
    init_pair(CLR_SELECTED, COLOR_BLACK,    COLOR_CYAN);
}

static int category_color(SyscallCategory cat) {
    switch (cat) {
        case CAT_FILE:      return CLR_FILE;
        case CAT_NETWORK:   return CLR_NETWORK;
        case CAT_PROCESS:   return CLR_PROCESS;
        case CAT_MEMORY:    return CLR_MEMORY;
        default:            return CLR_OTHER;
    }
}

static const char *category_name(SyscallCategory cat) {
    switch (cat) {
        case CAT_FILE:      return "FILE";
        case CAT_NETWORK:   return "NET ";
        case CAT_PROCESS:   return "PROC";
        case CAT_MEMORY:    return "MEM ";
        default:            return "    ";
    }
}

static void format_args(const SyscallEvent *ev, char *buf, int size) {
    switch (ev->sysno) {
        case SYS_read: case SYS_write: case SYS_pread64: case SYS_pwrite64:
            snprintf(buf, size, "fd=%ld len=%ld", ev->args[0], ev->args[2]);
            break;
        case SYS_open:
            snprintf(buf, size, "\"%s\"", ev->str_arg);
            break;
        case SYS_openat:
            snprintf(buf, size, "\"%s\"", ev->str_arg);
            break;
        case SYS_close:
            snprintf(buf, size, "fd=%ld", ev->args[0]);
            break;
        case SYS_socket:
            snprintf(buf, size, "dom=%ld type=%ld", ev->args[0], ev->args[1]);
            break;
        case SYS_connect: case SYS_bind: case SYS_accept: case SYS_accept4:
            snprintf(buf, size, "fd=%ld", ev->args[0]);
            break;
        case SYS_execve:
            snprintf(buf, size, "\"%s\"", ev->str_arg);
            break;
        case SYS_mmap:
            snprintf(buf, size, "len=%ld, prot=%ld", ev->args[1], ev->args[2]);
            break;
        default:
            snprintf(buf, size, "%ld %ld %ld", ev->args[0], ev->args[1], ev->args[2]);
            break;
    }
}

static void draw_status(WINDOW *win, const char *prog_name) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed = (now.tv_sec - gui_start.tv_sec);

    int cols = getmaxx(win);
    werase(win);
    wattron(win, COLOR_PAIR(CLR_STATUS) | A_BOLD);

    char buf[256];
    snprintf(buf, sizeof(buf),
             " sysvis | %s  PID: %d  [%s]  вызов: %d  время:  %lds",
             prog_name,
             traced_pid,
             proc_alive ? "running" : "exited",
             ev_count,
             elapsed);
    mvwaddnstr(win, 0, 0, buf, cols);

    /* заполняем остаток строки пробелами, чтобы фон был ровным */
    int len = (int)strlen(buf);
    for (int i = len; i < cols; i++) waddch(win, ' ');

    wattroff(win, COLOR_PAIR(CLR_STATUS) | A_BOLD);
    wrefresh(win);
}

static void draw_detail(WINDOW *win, int rows, int cols) {
    werase(win);
    wattron(win, A_REVERSE);
    mvwhline(win, 0, 0, ' ', cols);
    mvwaddstr(win, 0, 1, " Детали ");
    wattroff(win, A_REVERSE);

    if (ev_count == 0) { wrefresh(win); return; }

    /* находим событие по индексу selected среди видимых */
    int vis = 0;
    const SyscallEvent *ev = NULL;
    for (int i = 0; i < ev_count; i++) {
        if (!((filt >> evlist[i].category) & 1)) continue;
        if (vis == selected) { ev = &evlist[i]; break; }
        vis++;
    }
    if (!ev) { wrefresh(win); return; }

    /* строка 1: имя, категория, время, retval */
    char buf[512];
    int clr = (ev->retval == -1) ? CLR_ERROR : category_color(ev->category);
    wattron(win, COLOR_PAIR(clr) | A_BOLD);
    snprintf(buf, sizeof(buf), " [%s] %s  +%llums  ret=%ld",
             category_name(ev->category), ev->name,
             (unsigned long long)ev->ts_ms, ev->retval);
    mvwaddnstr(win, 1, 0, buf, cols);
    wattroff(win, COLOR_PAIR(clr) | A_BOLD);

    /* строка 2: аргументы */
    snprintf(buf, sizeof(buf), " args: %ld  %ld  %ld  %ld  %ld  %ld",
             ev->args[0], ev->args[1], ev->args[2],
             ev->args[3], ev->args[4], ev->args[5]);
    mvwaddnstr(win, 2, 0, buf, cols);

    /* строка 3: строковый аргумент, если он есть */
    if (ev->str_arg[0]) {
        snprintf(buf, sizeof(buf), " path: \"%s\"", ev->str_arg);
        mvwaddnstr(win, 3, 0, buf, cols);
    }

    /* строка 4: PID */
    snprintf(buf, sizeof(buf), " pid:  %d", ev->pid);
    mvwaddnstr(win, 4, 0, buf, cols);

    (void)rows;
    wrefresh(win);
}

static void draw_table(WINDOW *win, int rows, int cols) {
    werase(win);

    /* заголовок колонок */
    wattron(win, A_REVERSE);
    int args_w = cols - 46;
    if (args_w < 4) args_w = 4;
    char hdr[256];
    snprintf(hdr, sizeof(hdr), " %-5s %-7s %-4s %-16s %-*s %8s",
             "#", "ms", "cat", "syscall", args_w, "args", "ret");
    mvwaddnstr(win, 0, 0, hdr, cols);
    wattroff(win, A_REVERSE);

    int vis_rows = rows - 1;
    int vis = 0;
    int row = 0;
    
    for (int i = 0; i < ev_count && row < vis_rows; i++) {
        const SyscallEvent *ev = &evlist[i];

        /* пропуск скрытых категорий */
        if (!((filt >> ev->category) & 1)) continue;

        /* пропуск строк выше scroll_top */
        if (vis < scroll_top) { vis++; continue; }

        int is_sel = (vis == selected);
        int clr = (ev->retval == -1) ? CLR_ERROR : category_color(ev->category);

        if (is_sel)
            wattron(win, COLOR_PAIR(CLR_SELECTED) | A_BOLD);
        else
            wattron(win, COLOR_PAIR(clr));

        char args[64];
        format_args(ev, args, sizeof(args));

        char line[512];
        snprintf(line, sizeof(line)," %-5d %-7llu %-4s %-16s %-*.*s %8ld",
                 i + 1,
                 (unsigned long long)ev->ts_ms,
                 category_name(ev->category),
                 ev->name,
                 args_w, args_w, args,
                 ev->retval);
        mvwaddnstr(win, row + 1, 0, line, cols);

        if (is_sel)
            wattroff(win, COLOR_PAIR(CLR_SELECTED) | A_BOLD);
            
        else
            wattroff(win, COLOR_PAIR(clr));
        vis++;
        row++;
    }
}

void gui_run(EventBuf *events, const char *prog_name) {
    signal(SIGWINCH, handle_sigwinch);
    clock_gettime(CLOCK_MONOTONIC, &gui_start);
    setlocale(LC_ALL, "");

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    timeout(100);       // getch() ждет не больше 100 мс
    curs_set(0);
    init_colors();

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int table_h = rows - STATUS_LINES - DETAIL_LINES;

    WINDOW *win_status  = newwin(STATUS_LINES, cols, 0,                      0);
    WINDOW *win_table   = newwin(table_h,      cols, STATUS_LINES,           0);
    WINDOW * win_detail = newwin(DETAIL_LINES, cols, STATUS_LINES + table_h, 0);

    for (;;) {
        if (resized) {
            resized = 0;
            endwin(); refresh();
            getmaxyx(stdscr, rows, cols);
            table_h = rows - STATUS_LINES - DETAIL_LINES;
            wresize(win_status, STATUS_LINES, cols);
            wresize(win_table, table_h,       cols);
            wresize(win_detail, DETAIL_LINES, cols);
            mvwin(win_status, 0,                        0);
            mvwin(win_table, STATUS_LINES,              0);
            mvwin(win_detail, STATUS_LINES + table_h,   0);
        }

        /* читаем все события из буфера */
        SyscallEvent ev;
        while (ev_count < MAX_EVENTS && eventbuf_pop(events, &ev)) {
            if (traced_pid == 0) traced_pid = ev.pid;
            evlist[ev_count++] = ev;
        }

        /* проверяем, жив ли процесс */
        if (traced_pid && kill(traced_pid, 0) == -1)
            proc_alive = 0;

        draw_status(win_status, prog_name);
        draw_table(win_table,  table_h,      cols);
        draw_detail(win_detail, DETAIL_LINES, cols);

        /* подсчет видимых строк для навигации */
        int vis_total = 0;
        for (int i = 0; i < ev_count; i++) 
            if ((filt >> evlist[i].category) & 1) vis_total++;
        int table_vis = table_h - 1;

        int ch = getch();
        switch (ch) {
        case 'q': case 'Q':
            goto done;

        case KEY_UP:
            if (selected > 0) selected--;
            if (selected < scroll_top) scroll_top = selected;
            break;

        case KEY_DOWN:
            if (selected < vis_total - 1) selected++;
            if (selected >= scroll_top + table_vis)
                scroll_top = selected - table_vis + 1;
            break;

        case KEY_END:
            selected = (vis_total > 0) ? vis_total - 1 : 0;
            if (selected >= scroll_top + table_vis)
                scroll_top = selected - table_vis + 1;
            break;

        case 'f': filt ^= (1 << CAT_FILE);      selected = 0; scroll_top = 0; break;
        case 'n': filt ^= (1 << CAT_NETWORK);   selected = 0; scroll_top = 0; break;
        case 'p': filt ^= (1 << CAT_PROCESS);   selected = 0; scroll_top = 0; break;
        case 'm': filt ^= (1 << CAT_MEMORY);    selected = 0; scroll_top = 0; break;
        case 'a': filt = FILTER_ALL;            selected = 0; scroll_top = 0; break;
        }
    }
    done:
        delwin(win_detail);
        delwin(win_status);
        delwin(win_table);
        gui_shutdown();
}

void gui_shutdown(void) {
    endwin();
}
