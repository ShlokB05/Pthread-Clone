// File:	thread-worker.c
// List all group member's name:
// username of iLab:
// iLab Server:

//create, join, enqueue, dequeue 


#include "thread-worker.h"
#include <ucontext.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>

//Global counter for total context switches and 
//average turn around and response time
volatile long tot_cntx_switches=0;
double avg_turn_time=0;
double avg_resp_time=0;
#define STACK_SIZE SIGSTKSZ
#define S_TIME_PERIOD 50 

static void schedule(void);
//currWorkingThrd lists the actively working thrd, mainCtx is for benchmark, SchedulerCtx is the parent thread
static ucontext_t schedulerCtx, mainCtx;
tcb *currWorkingThrd = NULL;
tcb *temp;


Queue *SchedulingQueue;
//check for the init scheduler, updates to 1 when the scheduler and schedulerThrd has been init
static int created_scheduler = 0;

Queue *Queue1;
Queue *Queue2;
Queue *Queue3;
Queue *Queue4;

int currThreadId = 1; 
static volatile int Swapper;

void resetTimer(int time){
	struct itimerval timer;
	memset (&timer, 0, sizeof (timer));
	timer.it_value.tv_usec = (time%1000) * 1000; //
	timer.it_value.tv_sec = time/1000;
	timer.it_interval = timer.it_value;
	setitimer(ITIMER_PROF, &timer, NULL);

}


void onTimerRing(int signum){
	(void)signum;
	Swapper =1;
	
	if((currWorkingThrd != NULL && currWorkingThrd->CurrThreadState == RUNNING )){
		currWorkingThrd->quantumsRanFor++;
		Swapper = 0;
		worker_yield();
}
}

void initTimer(){
	struct sigaction sa;
	memset (&sa, 0, sizeof (sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = onTimerRing;
	sigaction (SIGPROF, &sa, NULL);	
}


void initializeQueue(Queue* q){
	q->head = NULL;
    q->tail = NULL;
}

static void firstInitScheduler(){
	printf("Sched has been init");
	getcontext(&mainCtx);
	getcontext(&schedulerCtx);

	//Main context makes the threads
	void *stack = malloc(SIGSTKSZ);
	schedulerCtx.uc_stack.ss_size = SIGSTKSZ;
	schedulerCtx.uc_stack.ss_sp = stack;
	schedulerCtx.uc_stack.ss_flags = 0;
	schedulerCtx.uc_link = &mainCtx;
	makecontext(&schedulerCtx, schedule, 0);	
	created_scheduler = 1; 
	initTimer();

	SchedulingQueue = malloc(sizeof *SchedulingQueue);
	initializeQueue(SchedulingQueue);//Now we have a init queue that won't be erased. 
}

bool isEmpty(Queue* q){
	return (q->head == NULL);
}

void enqueue(Queue* q, tcb *thread1){
	if(isEmpty(q)){
		q->head = thread1;
		q->tail = thread1;
		thread1->next = NULL;
	}
	else{ //tcb set the next to null. 
		thread1->next = NULL;
		q->tail->next = thread1;
		q->tail = thread1;
	}

}

tcb *Mutex_Dequeue(Queue* q){
	if (isEmpty(q)) {
        printf("Nothing to dequeue\n");
        return NULL;
    }
    else{
		tcb *temp = q->head; 
		q->head = temp->next;
		return temp;

		if(!q->head){
			q->tail = NULL; 
		}
	}
}
void dequeue(Queue* q){
	if (isEmpty(q)) {
        printf("Nothing to dequeue\n");
        return;
    }
    else{
		tcb *temp = q->head; 
		q->head = temp->next;
		return;
	}
}


//timer base logic

void dummyFunc(void){
	worker_exit(currWorkingThrd->MakeCtxRequiredSytnax(currWorkingThrd->arg));
}
/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg) {

		(void)attr;
		if(created_scheduler == 0){firstInitScheduler();  }
		tcb *thrd = malloc(sizeof *thrd);
		thrd->stack = malloc(STACK_SIZE);
		if(thrd->stack == NULL){
			printf("Failed to Allocate Memory for Stack");
			return -1;}
		
		thrd->ThreadID = currThreadId; currThreadId++;
		thrd->priority = 0; //base priority
		thrd->next = NULL;
		thrd->CurrThreadState = READY;

		if (getcontext(&thrd->threadCtx) < 0){
		perror("getcontext");
		free(thrd->stack);
		free(thrd);
		exit(1);
	}
		//cctx.uc_link=NULL;
		//cctx.uc_stack.ss_sp=stack;
		//cctx.uc_stack.ss_size=STACK_SIZE;
		//cctx.uc_stack.ss_flags=0;
		thrd->threadCtx.uc_link = &mainCtx;
		thrd->threadCtx.uc_stack.ss_sp = thrd->stack;
		thrd->threadCtx.uc_stack.ss_size = STACK_SIZE;
		thrd->threadCtx.uc_stack.ss_flags = 0;
		thrd->MakeCtxRequiredSytnax = function; //checkAgain
		thrd->arg = arg;

//makecontext(&cctx,(void *)&simplef,0);
		makecontext(&thrd->threadCtx, dummyFunc,0); //Make context requires a (void (*)(void))

	//	*thread = thrd->ThreadID; 
		*thread = thrd;
		enqueue(SchedulingQueue, thread);
		if(currWorkingThrd == NULL){
			swapcontext(&schedulerCtx, &mainCtx);
		}
    
    return 0;
};

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield() {
	currWorkingThrd->CurrThreadState = READY;
	enqueue(SchedulingQueue, currWorkingThrd);
	swapcontext(&currWorkingThrd->threadCtx, &schedulerCtx);
	
	// - change worker thread's state from Running to Ready
	// - save context of this thread to its thread control block
	// - switch from thread context to scheduler context
	
	return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr) {
//This worker_exit function is an explicit call to the worker_exit library to end the worker thread that called
//it. If the value_ptr isnt nULL, any return value from the worker thread will be saved. Think about what
//things you should clean up or change in the worker thread state and scheduler state when a thread is exiting"""";

	currWorkingThrd->ptrValue = value_ptr; // we will store even if null; 
	currWorkingThrd->CurrThreadState = REMOVED;
	swapcontext(&currWorkingThrd->threadCtx, &schedulerCtx);
	// - de-allocate any dynamic memory created when starting this thread
};








/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr) {
	tcb *joiningAfter = NULL; 
	tcb *TEM = SchedulingQueue->head;
	tcb *prev = SchedulingQueue->head;
	printf("entered ThreadJoin");

	if(TEM == NULL){return -1;} //if head is null that means there is no queue
	while(TEM != NULL){ //loop queue
		if(TEM->ThreadID == thread){ //got thread
			prev = TEM; 
			joiningAfter = TEM; //the specific thread
			break;
		} 
		TEM = TEM->next;}

		if(joiningAfter == NULL){return 0;} //we did not find the thread
		while(joiningAfter->CurrThreadState != REMOVED){ //meaning we found a thread
			worker_yield();}
		
		if(value_ptr != NULL){
		*value_ptr = joiningAfter->ptrValue;}
		return 0; 


	//joiningAfter has the instance of the tcb that is to join. 
	// - wait for a specific thread to terminate
	// - de-allocate any dynamic memory created by the joining thread
  
	// YOUR CODE HERE
	return 0;
};
/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr) {
	(void)mutexattr; 
	//make the runstack for this mutex

	//only the thread that init lock, can unlock. SO go off id. 
	mutex->user = NULL;
	mutex->q = malloc(sizeof *mutex->q); 	initializeQueue(mutex->q);
	mutex->IsLocked = false;
	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {
	if(mutex->IsLocked == false){
		//if free
		mutex->user = currWorkingThrd;
		mutex->workerId = currThreadId;
		mutex->IsLocked = true; 
		return 0;
	}
	else{
		currWorkingThrd->CurrThreadState = WAITING_FOR_MUTEX;
		enqueue(mutex->q, currWorkingThrd);
		swapcontext(&currWorkingThrd->threadCtx, &schedulerCtx);
		mutex->IsLocked = true; 
		mutex->user = currWorkingThrd;
	}


        // - use the built-in test-and-set atomic function to test the mutex
        // - if the mutex is acquired successfully, enter the critical section
        // - if acquiring mutex fails, push current thread into block list and
        // context switch to the scheduler thread

        // YOUR CODE HERE
        return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {

	if(mutex->IsLocked == false){return 0;} //we have freed
	else{
		if(mutex->user == currWorkingThrd){
			mutex->IsLocked = false; 
			mutex->user = NULL;
		}
		else{
			return -1;
		}
	}
	mutex->user = NULL; //clean mutex
	tcb *asdf = Mutex_Dequeue(mutex->q);
	if(asdf != NULL){ // if it exists
		asdf->CurrThreadState = READY; //we swap from blocked/Waiting from mutex
		enqueue(SchedulingQueue, asdf);} //enqueue to main queue
		
	


	// - release mutex and make it available again. 
	// - put threads in block list to run queue 
	// so that they could compete for mutex later.

	// YOUR CODE HERE
	return 0;
};


/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex) {
	// - de-allocate dynamic memory created in worker_mutex_init
	if(mutex->IsLocked == false){
		if(isEmpty(mutex->q)){
			free(mutex->q);
			return 0; 
		}
	}
	return -1;
};

tcb *PSJF_Next(Queue *q){
	tcb *TEMP = q->head;
	tcb *forward = q->head; forward = forward->next; //cause of tcb struct. forward = q.head.next;
	tcb *Previous = q->head;
	tcb *returnPrev = NULL;
	tcb *returnThread = TEMP; //so now both temp and returnThread point to head.

	if(TEMP == NULL){return NULL;}
	if(TEMP->next == NULL){return TEMP;} //if empty queue
	while(forward != NULL){
		Previous = TEMP;
		TEMP = forward;
		forward = forward->next;
		if(TEMP->quantumsRanFor < returnThread->quantumsRanFor){
			returnThread = TEMP;
			returnPrev = Previous;
		}
	}

	if(returnPrev == NULL){
		//so that would mean head is lowest priority. 
		q->head = returnThread->next;
		return returnThread;

	}
	if(returnThread == q->tail){
		returnPrev->next = NULL;
		return returnThread;
	}
	returnPrev->next = returnThread->next; //we popped out the return thread. 
	return returnThread;

}






/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf() {
	printf("ran sch");
	for(;;){
		tcb *next = PSJF_Next(SchedulingQueue);
		currWorkingThrd = next;
		currWorkingThrd->threadCtx = mainCtx;
		tot_cntx_switches++;
		swapcontext(&schedulerCtx,&currWorkingThrd->threadCtx );

		}

	// - your own implementation of PSJF
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
}

void initialize4Queues(){
	Queue1 = malloc(sizeof *Queue1);
	Queue2 = malloc(sizeof *Queue2);
	Queue3 = malloc(sizeof *Queue3);
	Queue4 = malloc(sizeof *Queue4);
	initializeQueue(Queue1);
	initializeQueue(Queue2);
	initializeQueue(Queue3);
	initializeQueue(Queue4);
}

static int sTimeCounter = 0;

static void promote(){

	for(int i = 2; i <= 4; i++){
		Queue *src;
		if(i==2){
			src = Queue2;
		}
		if(i==3){
			src = Queue3;
		}
		if(i==4){
			src = Queue4;
		}
		while(!isEmpty(src)){
			tcb *temp = Mutex_Dequeue(src);
			enqueue(Queue1, temp);
		}
	}
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	printf("entered the mlfq algo");
	// - your own implementation of MLFQ
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
	//first initialize high priority queue with everything 
	if(isEmpty(Queue1) && isEmpty(Queue2) && isEmpty(Queue3) && isEmpty(Queue4)){
		while(!isEmpty(SchedulingQueue)){
			tcb *temp = Mutex_Dequeue(SchedulingQueue);
			enqueue(Queue1, temp);
			printf("populated queue in the mlfq algo");

		}
	}

	while(true){
		sTimeCounter++;
		if(sTimeCounter >= S_TIME_PERIOD){
			promote();
			sTimeCounter = 0;
		}

		Queue *currentQueue = NULL;
		int level = 0;

		if(!isEmpty(Queue1)){
			currentQueue = Queue1;
			level = 1;
		}
		else if(!isEmpty(Queue2)){
			currentQueue = Queue2;
			level = 2;
		}
		else if(!isEmpty(Queue3)){
			currentQueue = Queue3;
			level = 3;
		}
		else if(!isEmpty(Queue4)){
			currentQueue = Queue4;
			level = 4;
		}
		else{
			continue; 
		}

		tcb *next = Mutex_Dequeue(currentQueue);
		currWorkingThrd = next;
		currWorkingThrd->CurrThreadState = RUNNING;
		tot_cntx_switches++;

		swapcontext(&schedulerCtx,&currWorkingThrd->threadCtx );


		//prevent blocked or waiting for mutex from being requeued
		if(currWorkingThrd->CurrThreadState == BLOCKED || currWorkingThrd->CurrThreadState == WAITING_FOR_MUTEX){
			continue; 
		}

		//demote queues not blocked or waiting for mutex
		if(currWorkingThrd->quantumsRanFor % QUANTUM == 0){
			if(level <4 ){
				currWorkingThrd->priority = level + 1;
				if(level ==1){
					enqueue(Queue2, currWorkingThrd);
				}
				else if(level ==2){
					enqueue(Queue3, currWorkingThrd);
				}
				else{ //level == 3
					enqueue(Queue4, currWorkingThrd);
				}
			}
			else{
				enqueue(Queue4, currWorkingThrd);
			}
		}
		else{
			enqueue(currentQueue, currWorkingThrd);
		}

		

	}

	/* Step-by-step guidances */
	// Step1: Calculate the time current thread actually ran
	// Step2.1: If current thread uses up its allotment, demote it to the low priority queue (Rule 4)
	// Step2.2: Otherwise, push the thread back to its origin queue
	// Step3: If time period S passes, promote all threads to the topmost queue (Rule 5)
	// Step4: Apply RR on the topmost queue with entries and run next thread
}

/* Completely fair scheduling algorithm */
static void sched_cfs(){
	// - your own implementation of CFS
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE

	/* Step-by-step guidances */

	// Step1: Update current thread's vruntime by adding the time it actually ran
	// Step2: Insert current thread into the runqueue (min heap)
	// Step3: Pop the runqueue to get the thread with a minimum vruntime
	// Step4: Calculate time slice based on target_latency (TARGET_LATENCY), number of threads within the runqueue
	// Step5: If the ideal time slice is smaller than minimum_granularity (MIN_SCHED_GRN), use MIN_SCHED_GRN instead
	// Step5: Setup next time interrupt based on the time slice
	// Step6: Run the selected thread
}


 /*
static void firstInitScheduler(){
	getcontext(&mainCtx);
	getcontext(&schedulerCtx);

	//Main context makes the threads
	void *stack = malloc(SIGSTKSZ);
	schedulerCtx.uc_stack.ss_size = SIGSTKSZ;
	schedulerCtx.uc_stack.ss_sp = stack;
	schedulerCtx.uc_stack.ss_flags = 0;
	schedulerCtx.uc_link = &mainCtx;
	makecontext(&schedulerCtx, schedule, 0);	
	created_scheduler = 1; 
	initTimer();

	SchedulingQueue = malloc(sizeof *SchedulingQueue);
	initializeQueue(SchedulingQueue);//Now we have a init queue that won't be erased. 

} */

int timeCalc(int minGran, int totalTime, int numWorkers){
	if(minGran > (totalTime)/numWorkers){return minGran;}
	return(totalTime/numWorkers);
}

//So if we init queue in Schedule it will erase nonstop, so we init it first in the above function and then return that queue to do all the heavy lifting.

/* scheduler */
static void schedule() {

	if(created_scheduler == 0){firstInitScheduler(); } //so we can have the scheduler made first run
	resetTimer(QUANTUM);



	
	
	// - every time a timer interrupt occurs, your worker thread library 
	// should be contexted switched from a thread context to this 
	// schedule() function
	
	
	// - invoke scheduling algorithms according to the policy (PSJF or MLFQ or CFS)
#if defined(PSJF)
    	sched_psjf();
#elif defined(MLFQ)
	sched_mlfq();
#elif defined(CFS)
    	sched_cfs();  
#else
	# error "Define one of PSJF, MLFQ, or CFS when compiling. e.g. make SCHED=MLFQ"
#endif
}



//DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void) {

       fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
       fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
       fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}


// Feel free to add any other functions you need

// YOUR CODE HERE
