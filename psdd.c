// psdd.c — Project 2 (Part 1): Process synchronization via POSIX semaphores
// Author: Shikshya Sharma  (solo)    
//
// Build:   make psdd
// Run:     ./psdd
// Stop:    Press Ctrl-C (SIGINT); the parent cleans up and exits.
//
// Notes:
// - Shared memory (mmap) holds a small struct with BankAccount.
// - A POSIX named semaphore enforces mutual exclusion across processes.
// - Parent (“Dear Old Dad”) and Child (“Poor Student”) loop indefinitely,
//   sleeping 0–5 seconds each loop and randomly deciding to check/deposit/withdraw.

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

#define SHM_FILE "bank.mem"
#define SEM_NAME "/bank_mutex_sem"   // leading slash required on many systems

typedef struct {
    int BankAccount;
} Shared;

static int shm_fd = -1;
static Shared *S = NULL;
static sem_t *mutex = NULL;
static pid_t child_pid = -1;
static volatile sig_atomic_t shutting_down = 0;

/* -------- utility printing (atomic-ish) -------- */
static void say(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

/* -------- cleanup -------- */
static void cleanup(void) {
    if (mutex) {
        sem_close(mutex);
        // Parent unlinks the named semaphore (safe even if already unlinked)
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
        // Optional: unlink the backing file so it doesn't linger:
        // unlink(SHM_FILE);
    }
}

/* -------- signal handler: parent only -------- */
static void on_sigint(int signo) {
    (void)signo;
    shutting_down = 1;
    say("\n[Parent] SIGINT received — shutting down...\n");
    if (child_pid > 0) {
        kill(child_pid, SIGTERM);
        // Give the child a moment to exit
        usleep(200 * 1000);
    }
    cleanup();
    _exit(0);
}

/* -------- random helpers -------- */
static void seed_rng(void) {
    unsigned s = (unsigned)time(NULL) ^ (unsigned)getpid();
    srand(s);
}
static int randi(int lo, int hi) { // inclusive
    int span = hi - lo + 1;
    return lo + (rand() % span);
}

/* -------- parent/child loops -------- */
static void dear_old_dad_loop(void) {
    seed_rng();
    while (1) {
        sleep(randi(0, 5));
        say("Dear Old Dad: Attempting to Check Balance\n");

        sem_wait(mutex);
        int localBalance = S->BankAccount;

        int r = randi(0, 1);
        if (r == 0) {
            if (localBalance < 100) {
                // Deposit path
                int amount = randi(0, 100);
                if ((amount % 2) == 0) {
                    localBalance += amount;
                    say("Dear Old Dad: Deposits $%d / Balance = $%d\n", amount, localBalance);
                    S->BankAccount = localBalance;  // write back shared
                } else {
                    say("Dear Old Dad: Doesn't have any money to give\n");
                }
            } else {
                say("Dear Old Dad: Thinks Student has enough Cash ($%d)\n", localBalance);
            }
        } else {
            say("Dear Old Dad: Last Checking Balance = $%d\n", localBalance);
        }
        sem_post(mutex);
    }
}

static void poor_student_loop(void) {
    seed_rng();
    while (1) {
        sleep(randi(0, 5));
        say("Poor Student: Attempting to Check Balance\n");

        sem_wait(mutex);
        int localBalance = S->BankAccount;

        int r = randi(0, 1);
        if (r == 0) {
            // Attempt withdraw
            int need = randi(0, 50);
            say("Poor Student needs $%d\n", need);
            if (need <= localBalance) {
                localBalance -= need;
                say("Poor Student: Withdraws $%d / Balance = $%d\n", need, localBalance);
                S->BankAccount = localBalance; // write back
            } else {
                say("Poor Student: Not Enough Cash ($%d)\n", localBalance);
            }
        } else {
            say("Poor Student: Last Checking Balance = $%d\n", localBalance);
        }
        sem_post(mutex);
    }
}

/* -------- main -------- */
int main(void) {
    /* 1) Create/initialize shared memory backing file */
    shm_fd = open(SHM_FILE, O_RDWR | O_CREAT, 0644);
    if (shm_fd < 0) { perror("open shm"); return 1; }
    if (ftruncate(shm_fd, sizeof(Shared)) < 0) { perror("ftruncate"); return 1; }

    S = mmap(NULL, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (S == MAP_FAILED) { perror("mmap"); return 1; }

    // Init shared balance once
    S->BankAccount = 0;

    /* 2) Create/open named semaphore with initial value 1 */
    mutex = sem_open(SEM_NAME, O_CREAT, 0644, 1);
    if (mutex == SEM_FAILED) {
        perror("sem_open");
        cleanup();
        return 1;
    }

    /* 3) Parent handles Ctrl-C to clean up; child inherits default */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    /* 4) Fork child (Poor Student) */
    child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        cleanup();
        return 1;
    }
    if (child_pid == 0) {
        // Child: ignore SIGINT; will get SIGTERM from parent on shutdown
        signal(SIGINT, SIG_IGN);
        poor_student_loop(); // never returns
        _exit(0);
    }

    // Parent: Dear Old Dad
    dear_old_dad_loop(); // never returns

    // Not reached
    cleanup();
    return 0;
}
