// psdd_ec.c — Project 2 (Part 1) Extra Credit
// Multi-process synchronization using POSIX named semaphore + mmap.
//
// Usage:
//   ./psdd_ec 1 3     # Dad + 3 students
//   ./psdd_ec 2 10    # Dad + Mom + 10 students
//
// Build: make psdd_ec
// Stop:  Ctrl-C (parent will SIGTERM all children and cleanup)

#define _POSIX_C_SOURCE 200809L
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>

#define SHM_FILE "bank.mem"
#define SEM_NAME "/bank_mutex_sem_ec"

typedef struct {
    int BankAccount;
} Shared;

static int shm_fd = -1;
static Shared *S = NULL;
static sem_t *mutex = NULL;

static pid_t *child_pids = NULL;
static int child_count = 0;

static volatile sig_atomic_t shutting_down = 0;

/* ------- printing helper ------- */
static void say(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

/* ------- nanosleep helpers ------- */
static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}
static void sleep_rand(int lo_s, int hi_s) { // inclusive seconds
    int span = hi_s - lo_s + 1;
    int s = lo_s + (rand() % (span > 0 ? span : 1));
    sleep_ms(s * 1000L);
}

/* ------- RNG ------- */
static void seed_rng(void) {
    unsigned s = (unsigned)time(NULL) ^ (unsigned)getpid();
    srand(s);
}
static int randi(int lo, int hi) {  // inclusive
    int span = hi - lo + 1;
    return lo + (rand() % (span > 0 ? span : 1));
}

/* ------- cleanup ------- */
static void cleanup(void) {
    if (mutex) {
        sem_close(mutex);
        sem_unlink(SEM_NAME);
        mutex = NULL;
    }
    if (S) {
        munmap(S, sizeof(*S));
        S = NULL;
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
        // unlink(SHM_FILE); // optional
    }
}

/* ------- signal handling in parent ------- */
static void on_sigint(int signo) {
    (void)signo;
    if (shutting_down) return;
    shutting_down = 1;

    say("\n[Parent] SIGINT — terminating children and cleaning up...\n");
    for (int i = 0; i < child_count; i++) {
        if (child_pids[i] > 0) kill(child_pids[i], SIGTERM);
    }

    // Reap children
    for (int i = 0; i < child_count; i++) {
        if (child_pids[i] > 0) {
            int st = 0;
            (void)waitpid(child_pids[i], &st, 0);
        }
    }

    cleanup();
    _exit(0);
}

/* ------- child SIGTERM -> exit quickly ------- */
static void child_term(int signo) {
    (void)signo;
    _exit(0);
}

/* ------- Roles ------- */
static void dear_old_dad_loop(void) {
    seed_rng();
    while (1) {
        sleep_rand(0,5);
        say("Dear Old Dad: Attempting to Check Balance\n");

        sem_wait(mutex);
        int localBalance = S->BankAccount;

        int r = randi(0,1);
        if (r == 0) {
            if (localBalance < 100) {
                int amount = randi(0,100);
                if ((amount % 2) == 0) {
                    localBalance += amount;
                    say("Dear Old Dad: Deposits $%d / Balance = $%d\n", amount, localBalance);
                    S->BankAccount = localBalance;
                } else {
                    say("Dear Old Dad: Doesn't have any money to give\n");
                }
            } else {
                say("Dear old Dad: Thinks Student has enough Cash ($%d)\n", localBalance);
            }
        } else {
            say("Dear Old Dad: Last Checking Balance = $%d\n", localBalance);
        }
        sem_post(mutex);
    }
}

static void lovable_mom_loop(void) {
    seed_rng();
    while (1) {
        sleep_rand(0,10);
        say("Loveable Mom: Attempting to Check Balance\n");

        sem_wait(mutex);
        int localBalance = S->BankAccount;

        if (localBalance <= 100) {
            int amount = randi(0,125);
            localBalance += amount;
            say("Lovable Mom: Deposits $%d / Balance = $%d\n", amount, localBalance);
            S->BankAccount = localBalance;
        } else {
            // spec doesn’t require a print here, but we can keep it quiet
        }
        sem_post(mutex);
    }
}

static void poor_student_loop(int idx) {
    (void)idx; // could be used to personalize prints
    seed_rng();
    while (1) {
        sleep_rand(0,5);
        say("Poor Student: Attempting to Check Balance\n");

        sem_wait(mutex);
        int localBalance = S->BankAccount;

        int r = randi(0,1);
        if (r == 0) {
            int need = randi(0,50);
            say("Poor Student needs $%d\n", need);
            if (need <= localBalance) {
                localBalance -= need;
                say("Poor Student: Withdraws $%d / Balance = $%d\n", need, localBalance);
                S->BankAccount = localBalance;
            } else {
                say("Poor Student: Not Enough Cash ($%d)\n", localBalance);
            }
        } else {
            say("Poor Student: Last Checking Balance = $%d\n", localBalance);
        }
        sem_post(mutex);
    }
}

/* ------- main ------- */
int main(int argc, char **argv) {
    int num_parents = 1;   // 1= Dad only, 2= Dad+Mom
    int num_children = 1;

    if (argc == 3) {
        num_parents = atoi(argv[1]);
        num_children = atoi(argv[2]);
    } else {
        fprintf(stderr, "Usage: %s <num_parents{1|2}> <num_children>=1..N\n", argv[0]);
        fprintf(stderr, "Defaulting to: Dad only + 1 Student\n");
    }
    if (num_parents < 1) num_parents = 1;
    if (num_parents > 2) num_parents = 2;
    if (num_children < 1) num_children = 1;

    /* create shared mem file */
    shm_fd = open(SHM_FILE, O_RDWR | O_CREAT, 0644);
    if (shm_fd < 0) { perror("open"); return 1; }
    if (ftruncate(shm_fd, sizeof(Shared)) < 0) { perror("ftruncate"); return 1; }

    S = mmap(NULL, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (S == MAP_FAILED) { perror("mmap"); return 1; }
    S->BankAccount = 0;

    /* open semaphore */
    mutex = sem_open(SEM_NAME, O_CREAT, 0644, 1);
    if (mutex == SEM_FAILED) { perror("sem_open"); cleanup(); return 1; }

    /* parent SIGINT -> cleanup */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    /* allocate pid array: Dad (1) + optional Mom (1) + children */
    child_count = num_children + (num_parents >= 1 ? 1 : 0) + (num_parents == 2 ? 1 : 0);
    child_pids = calloc(child_count, sizeof(pid_t));
    if (!child_pids) { perror("calloc"); cleanup(); return 1; }

    int idx = 0;

    /* fork Dad (always) */
    {
        pid_t p = fork();
        if (p < 0) { perror("fork dad"); on_sigint(SIGINT); }
        if (p == 0) {
            signal(SIGINT, SIG_IGN);
            signal(SIGTERM, child_term);
            dear_old_dad_loop();
            _exit(0);
        }
        child_pids[idx++] = p;
    }

    /* fork Mom if requested */
    if (num_parents == 2) {
        pid_t p = fork();
        if (p < 0) { perror("fork mom"); on_sigint(SIGINT); }
        if (p == 0) {
            signal(SIGINT, SIG_IGN);
            signal(SIGTERM, child_term);
            lovable_mom_loop();
            _exit(0);
        }
        child_pids[idx++] = p;
    }

    /* fork N students */
    for (int i = 0; i < num_children; i++) {
        pid_t p = fork();
        if (p < 0) { perror("fork student"); on_sigint(SIGINT); }
        if (p == 0) {
            signal(SIGINT, SIG_IGN);
            signal(SIGTERM, child_term);
            poor_student_loop(i);
            _exit(0);
        }
        child_pids[idx++] = p;
    }

    /* Parent just idles; Ctrl-C cleans up */
    say("Started: %s (parents=%d, students=%d)\n", argv[0], num_parents, num_children);
    while (1) pause(); // wait for signals

    // not reached
    cleanup();
    return 0;
}
