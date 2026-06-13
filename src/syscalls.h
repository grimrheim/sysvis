#pragma once
#include <sys/types.h>
#include <stdint.h>

typedef enum {
    CAT_FILE    = 0,
    CAT_NETWORK = 1,
    CAT_PROCESS = 2,
    CAT_MEMORY  = 3,
    CAT_OTHER   = 4,
} SyscallCategory;

typedef struct {
    pid_t       pid;
    int         entering;   /* 1 = вход, 0 = выход из syscall*/
    long        sysno;
    const char *name;
    long        args[6];
    long        retval;
    char        str_arg[256];   /* предвычитанный строковой аргумент */
    uint64_t    ts_ms;
    SyscallCategory category;
} SyscallEvent;

const char      *syscall_name(long sysno);
void            syscall_print(const SyscallEvent *ev);
SyscallCategory syscall_category(long sysno);

