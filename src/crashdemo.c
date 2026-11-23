/*
 * crashdemo — demo app for QNX HealthWatch.
 *
 * A deliberately unreliable process: it prints a heartbeat once a
 * second and then dies after a short, roughly random lifetime
 * (default 5–15 s, override with CRASH_AFTER=<seconds> or -n).
 * healthd is supposed to notice the death and restart it.
 *
 * Death is a real crash, not a clean exit: SIGSEGV by default
 * (CRASH_MODE=exit uses exit(3) instead, for the "clean exit" case).
 *
 * Build on QNX:  qcc -Vgcc_ntox86_64 -O2 -o crashdemo crashdemo.c
 * Build on Linux: gcc -O2 -o crashdemo crashdemo.c
 */
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int parse_lifetime(int argc, char **argv)
{
    const char *env = getenv("CRASH_AFTER");
    if (env) {
        int n = atoi(env);
        if (n > 0) return n;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            int n = atoi(argv[++i]);
            if (n > 0) return n;
        }
    }
    /* 5–15 s, seeded from time+pid so restarts look different. */
    srand((unsigned)(time(NULL) ^ (getpid() << 8)));
    return 5 + rand() % 11;
}

int main(int argc, char **argv)
{
    int lifetime = parse_lifetime(argc, argv);
    const char *mode = getenv("CRASH_MODE");
    printf("crashdemo pid=%d: alive, will die in ~%ds (mode=%s)\n",
           (int)getpid(), lifetime, mode ? mode : "segv");
    fflush(stdout);
    for (int i = 1; i <= lifetime; i++) {
        sleep(1);
        printf("crashdemo pid=%d: heartbeat %d/%d\n", (int)getpid(), i, lifetime);
        fflush(stdout);
    }
    printf("crashdemo pid=%d: dying now\n", (int)getpid());
    fflush(stdout);
    if (mode && strcmp(mode, "exit") == 0) exit(3);
    raise(SIGSEGV); /* the "real crash" path */
    _exit(139);
    return 0;
}
