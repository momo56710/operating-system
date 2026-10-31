#pragma once
typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
#define SIGINT 2
#define SIGABRT 6
#define SIGFPE 8
#define SIGSEGV 11
#define SIGTERM 15
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
/* LeOS has no signals yet: signal() remembers nothing useful and raise() does nothing. */
sighandler_t signal(int sig, sighandler_t handler);
int raise(int sig);
