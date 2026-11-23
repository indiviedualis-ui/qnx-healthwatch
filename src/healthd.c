/*
 * healthd — QNX HealthWatch supervisor daemon (v1)
 *
 * Watches the processes listed in a small YAML config, restarts them
 * when they die (exponential backoff, capped, with a restart limit),
 * logs every event to slog2 on QNX (stderr/syslog elsewhere) and to a
 * JSONL events file, and publishes a live status snapshot as JSON
 * (default /tmp/health.json) so the host tool can read it over SSH.
 *
 * v1 deliberately has NO resource manager: /dev/healthwatch is a
 * week-2 addition (see docs/ROADMAP.md). The status file is the
 * contract between the daemon and the host tool in v1.
 *
 * Build on QNX (SDP 8.0):
 *   qcc -Vgcc_ntox86_64 -O2 -std=c11 -Wall -Wextra -o healthd healthd.c -lslog2
 * Build on Linux (logic test only — same supervisor loop, same parser):
 *   gcc -O2 -std=c11 -Wall -Wextra -DHEALTHWATCH_LINUX_TEST -o healthd healthd.c
 *
 * Usage: healthd -c healthwatch.yaml [-f]   (-f = stay in foreground)
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE /* daemon() is a BSD extension */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef __QNX__
#include <sys/slog2.h>
#endif

#define HEALTHD_VERSION "1.0.0"
#define MAX_PROCESSES 32
#define MAX_NAME 64
#define MAX_CMD 512
#define MAX_PATH 256
#define MAX_BACKOFF_SEC 30

/* ------------------------------------------------------------------ */
/* Logging: slog2 on QNX, timestamped stderr elsewhere                */
/* ------------------------------------------------------------------ */

#ifdef __QNX__
static slog2_buffer_t g_slog_bufs[1];
static bool g_slog_ok = false;

static void hw_slog_init(void)
{
    slog2_buffer_set_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.buffer_set_name = "healthd";
    cfg.num_buffers = 1;
    cfg.verbosity_level = SLOG2_INFO;
    cfg.buffer_config[0].buffer_name = "healthd";
    cfg.buffer_config[0].num_pages = 7;
    cfg.max_retries = 3;
    g_slog_ok = (slog2_register(&cfg, g_slog_bufs, 0) == 0);
}
#endif

static void hw_log(const char *level, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

#ifdef __QNX__
    if (g_slog_ok) {
        uint8_t sev = SLOG2_INFO;
        if (strcmp(level, "ERROR") == 0) sev = SLOG2_ERROR;
        else if (strcmp(level, "WARN") == 0) sev = SLOG2_WARNING;
        slog2f(g_slog_bufs[0], 0, sev, "%s", buf);
    }
#endif
    /* Always mirror to stderr: visible in the QEMU console and in
       the Linux test harness, and slog2 is not available there. */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    localtime_r(&ts.tv_sec, &tmv);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%S", &tmv);
    fprintf(stderr, "[%s.%03ld] healthd %-5s %s\n",
            tbuf, ts.tv_nsec / 1000000, level, buf);
    fflush(stderr);
}

/* ------------------------------------------------------------------ */
/* Config                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[MAX_NAME];
    char command[MAX_CMD];
    bool restart_always;      /* "always" (default) or "never" */
    int max_restarts;         /* 0 = unlimited */
    int restart_delay;        /* initial backoff, seconds (default 1) */
    pid_t pid;                /* 0 = not running */
    int restarts;             /* successful restarts so far */
    int consecutive_failures; /* deaths since last run > 60 s */
    int backoff;              /* current backoff, seconds */
    time_t started_at;        /* when the current incarnation started */
    time_t last_exit_at;
    int last_exit_code;       /* exit status, or -signal */
    time_t last_restart_at;
    time_t first_seen_at;
    bool exhausted;           /* hit max_restarts */
    long total_uptime;        /* seconds across all incarnations */
    time_t run_started_at;    /* start of current run, for uptime math */
} watched_t;

typedef struct {
    int check_interval;             /* seconds, default 1 */
    char status_file[MAX_PATH];     /* default /tmp/health.json */
    char events_file[MAX_PATH];     /* default /tmp/health-events.jsonl */
    watched_t procs[MAX_PROCESSES];
    int nprocs;
} config_t;

static void config_defaults(config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->check_interval = 1;
    snprintf(c->status_file, sizeof(c->status_file), "/tmp/health.json");
    snprintf(c->events_file, sizeof(c->events_file),
             "/tmp/health-events.jsonl");
}

/* Trim leading/trailing whitespace and surrounding quotes, in place. */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') ||
                   (s[0] == '\'' && s[n - 1] == '\''))) {
        s[n - 1] = '\0';
        s++;
    }
    return s;
}

/* Strip an inline "# comment" (only when preceded by space). */
static void strip_comment(char *s)
{
    for (char *p = s; *p; p++) {
        if (*p == '#' && (p == s || p[-1] == ' ' || p[-1] == '\t')) {
            *p = '\0';
            return;
        }
    }
}

static bool parse_bool_restart(const char *v, bool *out)
{
    if (strcasecmp(v, "always") == 0) { *out = true; return true; }
    if (strcasecmp(v, "never") == 0) { *out = false; return true; }
    return false;
}

/* Minimal YAML subset parser: top-level scalars + a "processes:" list
 * of "- name:" entries. No libyaml dependency — intentional, so the
 * daemon builds on a bare QNX target with qcc and no extra ports. */
static int config_load(const char *path, config_t *cfg)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        hw_log("ERROR", "cannot open config %s: %s", path, strerror(errno));
        return -1;
    }
    char line[1024];
    watched_t *cur = NULL;
    while (fgets(line, sizeof(line), f)) {
        strip_comment(line);
        char *s = trim(line);
        if (*s == '\0') continue;

        if (strncmp(s, "processes:", 10) == 0) continue;

        if (s[0] == '-' && s[1] == ' ') {
            /* "- name: foo" — start a new process entry. */
            if (cfg->nprocs >= MAX_PROCESSES) {
                hw_log("WARN", "too many processes, ignoring extras");
                cur = NULL;
                continue;
            }
            cur = &cfg->procs[cfg->nprocs];
            memset(cur, 0, sizeof(*cur));
            cur->restart_always = true;
            cur->restart_delay = 1;
            cur->backoff = 1;
            cfg->nprocs++;
            s += 2;
            s = trim(s);
            if (*s == '\0') continue;
            /* fall through: the rest of the line is "name: foo" */
        }

        char *colon = strchr(s, ':');
        if (!colon) continue;
        *colon = '\0';
        char *key = trim(s);
        char *val = trim(colon + 1);

        if (cur == NULL) {
            if (strcmp(key, "check_interval") == 0)
                cfg->check_interval = atoi(val);
            else if (strcmp(key, "status_file") == 0)
                snprintf(cfg->status_file, sizeof(cfg->status_file), "%s", val);
            else if (strcmp(key, "events_file") == 0)
                snprintf(cfg->events_file, sizeof(cfg->events_file), "%s", val);
            continue;
        }
        if (strcmp(key, "name") == 0)
            snprintf(cur->name, sizeof(cur->name), "%s", val);
        else if (strcmp(key, "command") == 0)
            snprintf(cur->command, sizeof(cur->command), "%s", val);
        else if (strcmp(key, "restart") == 0) {
            if (!parse_bool_restart(val, &cur->restart_always))
                hw_log("WARN", "unknown restart value '%s' for %s", val, cur->name);
        } else if (strcmp(key, "max_restarts") == 0)
            cur->max_restarts = atoi(val);
        else if (strcmp(key, "restart_delay") == 0) {
            cur->restart_delay = atoi(val);
            if (cur->restart_delay < 1) cur->restart_delay = 1;
            cur->backoff = cur->restart_delay;
        }
    }
    fclose(f);

    if (cfg->check_interval < 1) cfg->check_interval = 1;
    for (int i = 0; i < cfg->nprocs; i++) {
        watched_t *p = &cfg->procs[i];
        if (p->name[0] == '\0' || p->command[0] == '\0') {
            hw_log("ERROR", "process entry %d is missing name or command", i);
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Events + status                                                     */
/* ------------------------------------------------------------------ */

static void iso_now(char *buf, size_t n)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    gmtime_r(&ts.tv_sec, &tmv);
    strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

static void json_escape(FILE *f, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '"': fputs("\\\"", f); break;
        case '\\': fputs("\\\\", f); break;
        case '\n': fputs("\\n", f); break;
        case '\r': fputs("\\r", f); break;
        case '\t': fputs("\\t", f); break;
        default:
            if ((unsigned char)*s < 0x20) fprintf(f, "\\u%04x", *s);
            else fputc(*s, f);
        }
    }
}

static void record_event(config_t *cfg, const char *name, const char *event,
                         int pid, int code, int restarts)
{
    FILE *f = fopen(cfg->events_file, "a");
    if (!f) return;
    char tbuf[32];
    iso_now(tbuf, sizeof(tbuf));
    fprintf(f, "{\"ts\":\"%s\",\"process\":\"", tbuf);
    json_escape(f, name);
    fprintf(f, "\",\"event\":\"%s\",\"pid\":%d,\"exit_code\":%d,"
               "\"restarts\":%d}\n",
            event, pid, code, restarts);
    fclose(f);
}

static time_t g_daemon_started = 0;

/* Write the status snapshot DIRECTLY to the status file — NOT via
 * tmp+rename. The atomic tmp+rename pattern does not work in
 * /dev/shmem on QNX 8.0: rename() fails there (silently — the .tmp
 * file is left behind and the target never appears), verified
 * 2026-11-23 (health.json.tmp existed, health.json never did).
 * /dev/shmem is the only runtime-writable location (the boot image
 * dirs are read-only), so direct write is the fix. A reader may
 * briefly see a partial file during the write; the host tool retries
 * on parse failure. */
static void write_status(config_t *cfg)
{
    FILE *f = fopen(cfg->status_file, "w");
    if (!f) {
        hw_log("WARN", "cannot write status %s: %s", cfg->status_file, strerror(errno));
        return;
    }
    char tbuf[32];
    iso_now(tbuf, sizeof(tbuf));
    time_t now = time(NULL);
    fprintf(f, "{\n  \"version\": \"%s\",\n  \"generated_at\": \"%s\",\n"
               "  \"daemon_uptime_seconds\": %ld,\n  \"processes\": [",
            HEALTHD_VERSION, tbuf, (long)(now - g_daemon_started));
    for (int i = 0; i < cfg->nprocs; i++) {
        watched_t *p = &cfg->procs[i];
        const char *state = p->exhausted ? "exhausted"
                            : p->pid ? "running"
                            : (p->restart_always && p->restarts > 0)
                                ? "restarting" /* in backoff */
                            : "dead";
        long up = p->total_uptime +
                  (p->pid ? (long)(now - p->run_started_at) : 0);
        fprintf(f, "%s\n    {\n      \"name\": \"", i ? "," : "");
        json_escape(f, p->name);
        fprintf(f, "\",\n      \"command\": \"");
        json_escape(f, p->command);
        fprintf(f, "\",\n      \"state\": \"%s\",\n      \"pid\": %d,\n"
                   "      \"restarts\": %d,\n      \"max_restarts\": %d,\n"
                   "      \"uptime_seconds\": %ld,\n"
                   "      \"last_exit_code\": %d\n    }",
                state, (int)p->pid, p->restarts, p->max_restarts,
                up, p->last_exit_code);
    }
    fprintf(f, "%s\n  ]\n}\n", cfg->nprocs ? "" : "");
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Supervision                                                         */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static pid_t spawn_process(watched_t *p)
{
    pid_t pid = fork();
    if (pid < 0) {
        hw_log("ERROR", "fork failed for %s: %s", p->name, strerror(errno));
        return 0;
    }
    if (pid == 0) {
        /* Child: a fresh session, then run the command via sh so
           arguments and pipes in `command:` just work. */
        setsid();
        execl("/bin/sh", "sh", "-c", p->command, (char *)NULL);
        _exit(127); /* execl failed */
    }
    p->pid = pid;
    p->started_at = time(NULL);
    p->run_started_at = p->started_at;
    if (p->first_seen_at == 0) p->first_seen_at = p->started_at;
    return pid;
}

static void supervise_once(config_t *cfg)
{
    time_t now = time(NULL);
    for (int i = 0; i < cfg->nprocs; i++) {
        watched_t *p = &cfg->procs[i];
        if (p->exhausted) continue;

        if (p->pid == 0) {
            /* Not running yet (first start, or after a restart). */
            if (spawn_process(p)) {
                hw_log("INFO", "started %s (pid %d): %s",
                       p->name, (int)p->pid, p->command);
                record_event(cfg, p->name, "started", (int)p->pid, 0,
                             p->restarts);
            }
            continue;
        }

        int status = 0;
        pid_t r = waitpid(p->pid, &status, WNOHANG);
        if (r == 0) continue;              /* still alive */
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) { p->pid = 0; continue; } /* child reaped elsewhere */

        /* Child died. */
        int code = WIFEXITED(status) ? WEXITSTATUS(status)
                   : WIFSIGNALED(status) ? -WTERMSIG(status) : -1;
        time_t lived = now - p->run_started_at;
        p->total_uptime += lived;
        p->last_exit_at = now;
        p->last_exit_code = code;
        pid_t dead = p->pid;
        p->pid = 0;
        hw_log("WARN", "%s (pid %d) exited code %d after %lds",
               p->name, (int)dead, code, (long)lived);
        record_event(cfg, p->name, "exited", (int)dead, code, p->restarts);

        if (!p->restart_always) {
            hw_log("INFO", "%s: restart=never, leaving dead", p->name);
            continue;
        }
        if (p->max_restarts > 0 && p->restarts >= p->max_restarts) {
            p->exhausted = true;
            hw_log("ERROR", "%s: max_restarts (%d) reached, giving up",
                   p->name, p->max_restarts);
            record_event(cfg, p->name, "exhausted", (int)dead, code,
                         p->restarts);
            continue;
        }
        /* Reset the backoff after a healthy run, so a process that
           crashes once a day does not accumulate a huge delay. */
        if (lived >= 60) {
            p->consecutive_failures = 0;
            p->backoff = p->restart_delay;
        } else {
            p->consecutive_failures++;
        }
        int delay = p->backoff;
        p->backoff *= 2;
        if (p->backoff > MAX_BACKOFF_SEC) p->backoff = MAX_BACKOFF_SEC;
        p->restarts++;
        p->last_restart_at = now;
        hw_log("INFO", "restarting %s in %ds (restart #%d)",
               p->name, delay, p->restarts);
        record_event(cfg, p->name, "restart_scheduled", 0, code,
                     p->restarts);
        /* Publish the new state before the backoff sleep, or the
           status snapshot would go stale for up to 30 s. */
        write_status(cfg);
        /* Backoff sleep, interruptible by SIGTERM/SIGINT. */
        for (int s = 0; s < delay && !g_stop; s++) sleep(1);
        if (g_stop) return;
        if (spawn_process(p)) {
            hw_log("INFO", "restarted %s (pid %d)", p->name, (int)p->pid);
            record_event(cfg, p->name, "restarted", (int)p->pid, 0,
                         p->restarts);
        }
    }
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "healthd %s — QNX HealthWatch supervisor daemon\n"
        "usage: %s -c <config.yaml> [-f]\n"
        "  -c  config file (default: /etc/healthwatch.yaml)\n"
        "  -f  stay in foreground (default: daemonize)\n",
        HEALTHD_VERSION, prog);
}

int main(int argc, char **argv)
{
    const char *cfg_path = "/etc/healthwatch.yaml";
    bool foreground = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) cfg_path = argv[++i];
        else if (strcmp(argv[i], "-f") == 0) foreground = true;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("healthd %s\n", HEALTHD_VERSION);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

#ifdef __QNX__
    hw_slog_init();
#endif

    config_t cfg;
    config_defaults(&cfg);
    if (config_load(cfg_path, &cfg) != 0) return 1;
    if (cfg.nprocs == 0) {
        hw_log("ERROR", "no processes configured in %s", cfg_path);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    if (!foreground) {
        if (daemon(0, 0) != 0) {
            hw_log("ERROR", "daemon() failed: %s", strerror(errno));
            return 1;
        }
    }

    g_daemon_started = time(NULL);
    hw_log("INFO", "healthd %s starting: %d process(es), check every %ds, "
           "status -> %s", HEALTHD_VERSION, cfg.nprocs,
           cfg.check_interval, cfg.status_file);
    for (int i = 0; i < cfg.nprocs; i++)
        hw_log("INFO", "watching %s: %s (restart=%s, max_restarts=%d)",
               cfg.procs[i].name, cfg.procs[i].command,
               cfg.procs[i].restart_always ? "always" : "never",
               cfg.procs[i].max_restarts);

    while (!g_stop) {
        supervise_once(&cfg);
        write_status(&cfg);
        for (int s = 0; s < cfg.check_interval && !g_stop; s++) sleep(1);
    }

    hw_log("INFO", "shutting down, stopping supervised processes");
    for (int i = 0; i < cfg.nprocs; i++) {
        if (cfg.procs[i].pid) {
            kill(cfg.procs[i].pid, SIGTERM);
            record_event(&cfg, cfg.procs[i].name, "stopped",
                         (int)cfg.procs[i].pid, 0, cfg.procs[i].restarts);
        }
    }
    sleep(1);
    for (int i = 0; i < cfg.nprocs; i++)
        if (cfg.procs[i].pid) kill(cfg.procs[i].pid, SIGKILL);
    write_status(&cfg);
    hw_log("INFO", "healthd stopped");
    return 0;
}
