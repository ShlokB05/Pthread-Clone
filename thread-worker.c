// File:	thread-worker.c
// List all group member's name:
// username of iLab:
// iLab Server:

#include "thread-worker.h"
#include <ucontext.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

volatile long tot_cntx_switches = 0;
double avg_turn_time = 0;
double avg_resp_time = 0;

#define STACK_SIZE (SIGSTKSZ*64)
#define S_TIME_PERIOD 50

tcb *ThreadExists[1000] = {0};

static void schedule(void);

static ucontext_t schedulerCtx, mainCtx;
static tcb *currWorkingThrd = NULL;

static Queue *SchedulingQueue;
static int created_scheduler = 0;

static Queue *Queue1;
static Queue *Queue2;
static Queue *Queue3;
static Queue *Queue4;

static volatile int Swapper = 1;
static volatile sig_atomic_t inSchedulerLock = 1;

static void block_timer(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPROF);
    sigprocmask(SIG_BLOCK, &set, NULL);
}

static void unblock_timer(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPROF);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
}

static unsigned long long now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000000ULL + (unsigned long long)tv.tv_usec;
}

static void resetTimer(int time_ms) {
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_usec = (time_ms % 1000) * 1000;
    timer.it_value.tv_sec = time_ms / 1000;
    timer.it_interval = timer.it_value;
    setitimer(ITIMER_PROF, &timer, NULL);
}

static void initializeQueue(Queue *q) {
    if (!q) return;
    q->head = NULL;
    q->tail = NULL;
}

static bool isEmpty(Queue *q) {
    return ((q == NULL) || (q->head == NULL));
}

static void enqueue(Queue *q, tcb *t) {
    if (!q || !t) return;

    t->next = NULL;
    if (q->head == NULL) {
        q->head = t;
        q->tail = t;
        return;
    }
    q->tail->next = t;
    q->tail = t;
}

static tcb *dequeue(Queue *q) {
    if ((!q) || (!q->head)) return NULL;

    tcb *t = q->head;
    q->head = t->next;
    if (q->head == NULL) q->tail = NULL;
    t->next = NULL;
    return t;
}

static void initialize4Queues(void) {
    Queue1 = malloc(sizeof *Queue1);
    Queue2 = malloc(sizeof *Queue2);
    Queue3 = malloc(sizeof *Queue3);
    Queue4 = malloc(sizeof *Queue4);

    initializeQueue(Queue1);
    initializeQueue(Queue2);
    initializeQueue(Queue3);
    initializeQueue(Queue4);
}

static void onTimerRing(int signum) {
    (void)signum;

    if (inSchedulerLock) return;
    if (currWorkingThrd == NULL) return;
    if (currWorkingThrd->CurrThreadState != RUNNING) return;

    currWorkingThrd->quantumsRanFor++;
    Swapper = 0;
    currWorkingThrd->CurrThreadState = READY;

    inSchedulerLock = 1;
    swapcontext(&currWorkingThrd->threadCtx, &schedulerCtx);
}

static void initTimer(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = onTimerRing;
    sigaction(SIGPROF, &sa, NULL);
}

static void firstInitScheduler(void) {
    getcontext(&mainCtx);
    getcontext(&schedulerCtx);

    sigemptyset(&schedulerCtx.uc_sigmask);
    sigaddset(&schedulerCtx.uc_sigmask, SIGPROF);

    initialize4Queues();

    void *stack = malloc(STACK_SIZE);
    schedulerCtx.uc_stack.ss_size = STACK_SIZE;
    schedulerCtx.uc_stack.ss_sp = stack;
    schedulerCtx.uc_stack.ss_flags = 0;
    schedulerCtx.uc_link = &mainCtx;

    makecontext(&schedulerCtx, schedule, 0);

    created_scheduler = 1;
    initTimer();

    SchedulingQueue = malloc(sizeof *SchedulingQueue);
    initializeQueue(SchedulingQueue);
}

static void dummyFunc(void) {
    void *ret = currWorkingThrd->MakeCtxRequiredSytnax(currWorkingThrd->arg);
    worker_exit(ret);
}
int worker_create(worker_t *thread, pthread_attr_t *attr,
                  void *(*function)(void *), void *arg) {
    (void)attr;

    if (!created_scheduler) firstInitScheduler();

    tcb *newThread = malloc(sizeof *newThread);
    if (!newThread) return -1;

    newThread->stack = malloc(STACK_SIZE);
    if (!newThread->stack) {
        free(newThread);
        return -1;
    }

    newThread->ThreadID = (worker_t)1;
    while (newThread->ThreadID < 1000 && ThreadExists[newThread->ThreadID] != NULL) {
        newThread->ThreadID++;
    }
    if (newThread->ThreadID >= 1000) {
        free(newThread->stack);
        free(newThread);
        return -1;
    }

    newThread->priority = 0;
    newThread->next = NULL;
    newThread->CurrThreadState = READY;
    newThread->quantumsRanFor = 0;
    newThread->ptrValue = NULL;
    newThread->vruntime = 0.0;

    if (getcontext(&newThread->threadCtx) < 0) {
        free(newThread->stack);
        free(newThread);
        return -1;
    }

    sigemptyset(&newThread->threadCtx.uc_sigmask);

    newThread->threadCtx.uc_link = &schedulerCtx;
    newThread->threadCtx.uc_stack.ss_sp = newThread->stack;
    newThread->threadCtx.uc_stack.ss_size = STACK_SIZE;
    newThread->threadCtx.uc_stack.ss_flags = 0;

    newThread->MakeCtxRequiredSytnax = function;
    newThread->arg = arg;

    makecontext(&newThread->threadCtx, dummyFunc, 0);

    *thread = newThread->ThreadID;
    ThreadExists[newThread->ThreadID] = newThread;

    enqueue(SchedulingQueue, newThread);
    return 0;
}

int worker_yield() {
    if (!currWorkingThrd) return 0;

    block_timer();
    Swapper = 1;
    currWorkingThrd->CurrThreadState = READY;

    inSchedulerLock = 1;
    swapcontext(&currWorkingThrd->threadCtx, &schedulerCtx);

    unblock_timer();
    return 0;
}


void worker_exit(void *value_ptr) {
    currWorkingThrd->ptrValue = value_ptr;
    currWorkingThrd->CurrThreadState = REMOVED;

    inSchedulerLock = 1;
    setcontext(&schedulerCtx);
}

int worker_join(worker_t thread, void **value_ptr) {
    tcb *t = (thread < 1000) ? ThreadExists[thread] : NULL;
    if (!t) return -1;

    if (currWorkingThrd == NULL) {
        while (t->CurrThreadState != REMOVED) {
            inSchedulerLock = 1;
            swapcontext(&mainCtx, &schedulerCtx);
        }
    } else {
        while (t->CurrThreadState != REMOVED) {
            worker_yield();
        }
    }

    if (value_ptr) *value_ptr = t->ptrValue;
    return 0;
}

int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr) {
    (void)mutexattr;
    if (!mutex) return -1;

    mutex->user = NULL;
    mutex->q = malloc(sizeof *mutex->q);
    initializeQueue(mutex->q);
    mutex->IsLocked = false;
    mutex->workerId = 0;
    return 0;
}

int worker_mutex_lock(worker_mutex_t *mutex) {
    if (!mutex) return -1;

    block_timer();

    if (!mutex->IsLocked) {
        mutex->IsLocked = true;
        mutex->user = currWorkingThrd;
        if (currWorkingThrd) mutex->workerId = currWorkingThrd->ThreadID;
        else mutex->workerId = 0;
        unblock_timer();
        return 0;
    }

    currWorkingThrd->CurrThreadState = WAITING_FOR_MUTEX;
    enqueue(mutex->q, currWorkingThrd);


    inSchedulerLock = 1;
    swapcontext(&currWorkingThrd->threadCtx, &schedulerCtx);
    unblock_timer();

    return 0;
}

int worker_mutex_unlock(worker_mutex_t *mutex) {
    if (!mutex) return -1;

    block_timer();

    if (!mutex->IsLocked || mutex->user != currWorkingThrd) {
        unblock_timer();
        return -1;
    }

    tcb *w = dequeue(mutex->q);
    if (w) {
        mutex->IsLocked = true;
        mutex->user = w;
        mutex->workerId = w->ThreadID;

        w->CurrThreadState = READY;
        enqueue(SchedulingQueue, w);
    } else {
        mutex->IsLocked = false;
        mutex->user = NULL;
        mutex->workerId = 0;
    }

    unblock_timer();
    return 0;
}

void print_app_stats(void) {
    fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
    //fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
    //fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}


int worker_mutex_destroy(worker_mutex_t *mutex) {
    if (!mutex) return -1;

    if (!mutex->IsLocked && isEmpty(mutex->q)) {
        free(mutex->q);
        mutex->q = NULL;
        return 0;
    }
    return -1;
}

static tcb *PSJF_Next(Queue *q) {
    if (!q || !q->head) return NULL;

    tcb *best = q->head;
    tcb *bestPrev = NULL;

    tcb *prev = q->head;
    tcb *cur = q->head->next;

    while (cur) {
        if (cur->quantumsRanFor < best->quantumsRanFor) {
            best = cur;
            bestPrev = prev;
        }
        prev = cur;
        cur = cur->next;
    }

    if (bestPrev == NULL) q->head = best->next;
    else bestPrev->next = best->next;

    if (q->tail == best) q->tail = bestPrev;

    best->next = NULL;
    return best;
}

static void sched_psjf(void) {
    for (;;) {
        tcb *next = PSJF_Next(SchedulingQueue);
        if (!next) setcontext(&mainCtx);

        currWorkingThrd = next;
        currWorkingThrd->CurrThreadState = RUNNING;
        tot_cntx_switches++;
        Swapper = 1;

        inSchedulerLock = 0;
        swapcontext(&schedulerCtx, &currWorkingThrd->threadCtx);
        inSchedulerLock = 1;

        if (currWorkingThrd->CurrThreadState == READY) {
            enqueue(SchedulingQueue, currWorkingThrd);
        }
    }
}

static int sTimeCounter = 0;

static void promote(void) {
    Queue *srcs[3] = { Queue2, Queue3, Queue4 };

    for (int i = 0; i < 3; i++) {
        while (!isEmpty(srcs[i])) {
            tcb *t = dequeue(srcs[i]);
            enqueue(Queue1, t);
        }
    }
}
static void sched_mlfq(void) {
    for (;;) {
        block_timer();
        while (!isEmpty(SchedulingQueue)) {
            tcb *t = dequeue(SchedulingQueue);
            enqueue(Queue1, t);
           // fprintf(stderr, "entered queue logic \n");
        }
        if (isEmpty(Queue1) && isEmpty(Queue2) && isEmpty(Queue3) && isEmpty(Queue4)) {
        if (isEmpty(SchedulingQueue)) {
        unblock_timer();
        setcontext(&mainCtx);
        }
        unblock_timer();
        continue;}

        
        unblock_timer();

        Queue *q = NULL;
        int level = 0;

        if (!isEmpty(Queue1)) { q = Queue1; level = 1;             //fprintf(stderr, "entered Q1 \n");
        }
        else if (!isEmpty(Queue2)) { q = Queue2; level = 2;   //fprintf(stderr, "entered Q2 \n");
            }
        else if (!isEmpty(Queue3)) { q = Queue3; level = 3;  //fprintf(stderr, "entered Q3 \n" );
            }
        else { q = Queue4; level = 4; // fprintf(stderr, "entered Q4"); 
        }

        tcb *next = dequeue(q);
        if (!next) continue;

        currWorkingThrd = next;
        currWorkingThrd->CurrThreadState = RUNNING;
        tot_cntx_switches++;
        Swapper = 1;

        inSchedulerLock = 0;
        swapcontext(&schedulerCtx, &currWorkingThrd->threadCtx);
        //fprintf(stderr, "entered scheduler \n");
        inSchedulerLock = 1;

        if (Swapper == 0) {
            sTimeCounter++;
            if (sTimeCounter >= S_TIME_PERIOD) {
                promote();
                sTimeCounter = 0;
            }
        }

        if (currWorkingThrd->CurrThreadState == READY) {
            if (Swapper == 0) {
                if (level == 1) enqueue(Queue2, currWorkingThrd);
                else if (level == 2) enqueue(Queue3, currWorkingThrd);
                else enqueue(Queue4, currWorkingThrd);
            } else {
                enqueue(q, currWorkingThrd);
            }
        }
    }
}


static int timeCalc(int minGran, int totalTime, int numWorkers) {
    if (numWorkers <= 0) numWorkers = 1;
    int slice = totalTime / numWorkers;
    if (slice < minGran) slice = minGran;
    if (slice <= 0) slice = 1;
    return slice;
}

static tcb *CFS_NEXT(Queue *q) {
    if (!q || !q->head) return NULL;

    tcb *best = q->head;
    tcb *bestPrev = NULL;

    tcb *prev = q->head;
    tcb *cur = q->head->next;

    while (cur) {
        if (cur->vruntime < best->vruntime) {
            best = cur;
            bestPrev = prev;
        }
        prev = cur;
        cur = cur->next;
    }

    if (bestPrev == NULL) q->head = best->next;
    else bestPrev->next = best->next;

    if (q->tail == best) q->tail = bestPrev;

    best->next = NULL;
    return best;
}

static void sched_cfs(void) {
    for (;;) {
        int runnable = 0;
        for (tcb *c = SchedulingQueue->head; c; c = c->next) runnable++;

        tcb *next = CFS_NEXT(SchedulingQueue);
        if (!next) setcontext(&mainCtx);

        int slice_ms = timeCalc(MIN_SCHED_GRN, TARGET_LATENCY, runnable);
        resetTimer(slice_ms);

        currWorkingThrd = next;
        currWorkingThrd->CurrThreadState = RUNNING;
        tot_cntx_switches++;

        unsigned long long start = now_us();

        inSchedulerLock = 0;
        swapcontext(&schedulerCtx, &currWorkingThrd->threadCtx);
        inSchedulerLock = 1;

        unsigned long long ran = now_us() - start;
        currWorkingThrd->vruntime += (double)ran / 1000.0;

        if (currWorkingThrd->CurrThreadState == READY) {
            enqueue(SchedulingQueue, currWorkingThrd);
        }
    }
}

static void schedule(void) {
    if (!created_scheduler) firstInitScheduler();
    resetTimer(QUANTUM);

#if defined(PSJF)
    sched_psjf();
#elif defined(MLFQ)
    sched_mlfq();
#elif defined(CFS)
    sched_cfs();
#else
# error "Define one of PSJF, MLFQ, or CFS when compiling."
#endif
}
