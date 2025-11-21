// shm_processes.c
// Project: Synchronization of Processes Using Semaphores
// Author: Shikshya Sharma
//
// Requirements covered:
// - Uses System V shared memory to hold a shared BankAccount integer.
// - Uses a POSIX named semaphore for mutual exclusion (no race conditions).
// - Implements “Dear Old Dad” (parent) and “Poor Student” (child) rules.
// - Loops indefinitely; balances are updated atomically and printed as per spec.
// - Extra Credit: optional “Lovable Mom” and N Poor Students via CLI:
//       ./shm_proc 1 1   -> Dad + 1 Student  (default behavior)
//       ./shm_proc 1 3   -> Dad + 3 Students
//       ./shm_proc 2 10  -> Dad + Mom + 10 Students
//
// Stop: Press Ctrl-C in the terminal running ./shm_proc
//       Parent will kill children and clean up shared memory and semaphore.

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <semaphore.h>
#include <fcntl.h>

// -------- Shared memory layout --------
typedef struct {
    int BankAccount;
} Shared;

static int   ShmID       = -1;
static Shared *S         = NULL;

// -------- Semaphore --------
// Leading '/' is required for named POSIX semaphores on many systems.
#define SEM_NAME "/bank_mutex_sem_lab3"

static sem_t *mutex      = NULL;

// -------- Child PIDs (for EC) --------
static pid_t *child_pids = NULL;
static int    child_count = 0;

static volatile sig_atomic_t shutting_down = 0;

// -------- Printing helper (flush immediately) --------
static void say(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

// -------- RNG helpers --------
static void seed_rng(void) {
    unsigned s = (unsigned)time(NULL) ^ (unsigned)getpid();
    srand(s);
}

static int randi(int lo, int hi) { // inclusive
    int span = hi - lo + 1;
    if (span <= 0) span = 1;
    return lo + (rand() % span);
}

static void sleep_rand(int lo_s, int hi_s) {
    int span = hi_s - lo_s + 1;
    int s = lo_s + (rand() % (span > 0 ? span : 1));
    sleep(s);
}

// -------- Cleanup --------
static void cleanup(void) {
    if (mutex) {
        sem_close(mutex);
        sem_unlink(SEM_NAME);
        mutex = NULL;
    }

    if (S && ShmID != -1) {
        shmdt((void *)S);
        S = NULL;
    }

    if (ShmID != -1) {
        // Mark segment to be destroyed when no process is attached
        shmctl(ShmID, IPC_RMID, NULL);
        ShmID = -1;
    }

    if (child_pids) {
        free(child_pids);
        child_pids = NULL;
        child_count = 0;
    }
}

// -------- Parent SIGINT handler --------
static void on_sigint(int signo) {
    (void)signo;
    if (shutting_down) return;
    shutting_down = 1;

    say("\n[Parent] SIGINT — terminating children and cleaning up...\n");

    // Kill all children (Mom + all Students)
    for (int i = 0; i < child_count; i++) {
        if (child_pids[i] > 0) {
            kill(child_pids[i], SIGTERM);
        }
    }

    // Give them a moment to exit
    usleep(200 * 1000);

    cleanup();
    _exit(0);
}

// -------- Child SIGTERM handler --------
static void child_term(int signo) {
    (void)signo;
    _exit(0);
}

// -------- Role: Dear Old Dad (runs in the ORIGINAL parent process) --------
static void dear_old_dad_loop(void) {
    seed_rng();
    while (1) {
        // Sleep between 0–5 seconds
        sleep_rand(0, 5);
        say("Dear Old Dad: Attempting to Check Balance\n");

        sem_wait(mutex);
        int localBalance = S->BankAccount;

        int r = randi(0, 1);
        if (r == 0) {
            if (localBalance < 100) {
                // Deposit money path
                int amount = randi(0, 100);   // 0–100 (fits spec “between 0–100”)
                if ((amount % 2) == 0) {
                    localBalance += amount;
                    // Exact strings from assignment:
                    say("Dear old Dad: Deposits $%d / Balance = $%d\n",
                        amount, localBalance);
                    S->BankAccount = localBalance;
                } else {
                    say("Dear old Dad: Doesn't have any money to give\n");
                }
            } else {
                say("Dear old Dad: Thinks Student has enough Cash ($%d)\n",
                    localBalance);
            }
        } else {
            say("Dear Old Dad: Last Checking Balance = $%d\n", localBalance);
        }

        sem_post(mutex);
    }
}

// -------- Role: Lovable Mom (extra credit, runs in its own child) --------
static void lovable_mom_loop(void) {
    seed_rng();
    while (1) {
        // Sleep between 0–10 seconds
        sleep_rand(0, 10);
        say("Loveable Mom: Attempting to Check Balance\n");

        sem_wait(mutex);
        int localBalance = S->BankAccount;

        if (localBalance <= 100) {
            // Always deposit when balance <= 100
            int amount = randi(0, 125); // 0–125
            localBalance += amount;
            say("Lovable Mom: Deposits $%d / Balance = $%d\n",
                amount, localBalance);
            S->BankAccount = localBalance;
        }
        // If localBalance > 100, Mom does nothing (spec doesn’t require a print).
        sem_post(mutex);
    }
}

// -------- Role: Poor Student (child processes) --------
static void poor_student_loop(int student_index) {
    (void)student_index; // not used, but could personalize prints
    seed_rng();
    while (1) {
        // Sleep between 0–5 seconds
        sleep_rand(0, 5);
        say("Poor Student: Attempting to Check Balance\n");

        sem_wait(mutex);
        int localBalance = S->BankAccount;

        int r = randi(0, 1);
        if (r == 0) {
            // Attempt to withdraw
            int need = randi(0, 50); // 0–50
            say("Poor Student needs $%d\n", need);
            if (need <= localBalance) {
                localBalance -= need;
                say("Poor Student: Withdraws $%d / Balance = $%d\n",
                    need, localBalance);
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

// -------- main --------
int main(int argc, char *argv[]) {
    int num_parents  = 1; // 1 = Dad only, 2 = Dad + Mom
    int num_children = 1; // number of Poor Students

    // Extra credit-style CLI: ./shm_proc <num_parents{1|2}> <num_children>
    if (argc == 3) {
        num_parents  = atoi(argv[1]);
        num_children = atoi(argv[2]);
        if (num_parents < 1) num_parents = 1;
        if (num_parents > 2) num_parents = 2;
        if (num_children < 1) num_children = 1;
    } else {
        // Default: behave like 1 Dad + 1 Poor Student (original problem)
        // No need to exit on wrong argc; 
        fprintf(stderr,
                "Usage (extra credit):
