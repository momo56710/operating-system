#include "libc.h"

int main(void) {
    static const char *states[] = { "running", "ready", "sleeping", "blocked", "zombie", "defunct" };
    struct si_task t;
    printf("PID  PPID  STATE     KIND    NAME\n");
    for (unsigned long i = 0; sysinfo(SI_TASK, i, &t) > 0; i++)
        printf("%-4lu %-5lu %-9s %-7s %s\n", t.pid, t.parent_pid, states[t.state < 6 ? t.state : 0],
               t.is_user ? "user" : "kernel", t.name);
    return 0;
}
