// File:	thread-worker.c

// List all group member's name:
// username of iLab:
// iLab Server:

#include "thread-worker.h"

//Global counter for total context switches and 
//average turn around and response time
long tot_cntx_switches=0;
double avg_turn_time=0;
double avg_resp_time=0;


// INITAILIZE ALL YOUR OTHER VARIABLES HERE
// YOUR CODE HERE
ucontext_t scheduler_context;
queue_node** runqueue_head = NULL;
tcb** map = NULL;
tcb* current_tcb = NULL;
int num_thread = MAIN_THREAD_ID + 1;
int quanta = 0;
long tot_turn_time = 0;
long tot_resp_time = 0;
typedef enum {
    READY,
    RUNNING,
    BLOCKED,
    EXITED
} STATUS;

// Add a TCB to the end of the ready queue
void enqueue(queue_node* queue_head, tcb* new_tcb) {
    // printf("enqueue\n");
    queue_node* new_node = malloc(sizeof(queue_node));
    new_node->next = NULL;
    new_node->tcb = new_tcb;

    queue_node* curr_node = queue_head;
    while (curr_node->next != NULL) {
        curr_node = curr_node->next;
    }
    curr_node->next = new_node;
}

// Remove and return the first TCB from the ready queue
tcb* dequeue(queue_node* queue_head) {
    // printf("dequeue\n");
    if (queue_head->next != NULL) {
        queue_node* old_node = queue_head->next;
        queue_head->next = old_node->next;
        old_node->next = NULL;
        tcb* old_tcb = old_node->tcb;
        free(old_node);
        return old_tcb;
    } else {
        return NULL;
    }
}

void handler(int signum) {
    quanta++;
    if (current_tcb != NULL) {
        // printf("handler id %d\n", current_tcb->id);
        swapcontext(&current_tcb->context, &scheduler_context);
    }
}

void timer() {
    // printf("timer\n");
    struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &handler;
    sigaction(SIGPROF, &sa, NULL);

    struct itimerval timer;
    timer.it_value.tv_sec = TIME_QUANTUM / 1000;
    timer.it_value.tv_usec = (TIME_QUANTUM * 1000) % 1000000;
    timer.it_interval.tv_sec = TIME_QUANTUM / 1000;
    timer.it_interval.tv_usec = (TIME_QUANTUM * 1000) % 1000000;
    setitimer(ITIMER_PROF, &timer, NULL);
}

/* create a new thread */
int worker_create(worker_t *thread, pthread_attr_t *attr,
                  void *(*function)(void *), void *arg) {

    // - create Thread Control Block (TCB)
    // - create and initialize the context of this worker thread
    // - allocate space of stack for this thread to run
    // after everything is set, push this thread into run queue and 
    // - make it ready for the execution.

    // printf("create id %d\n", num_thread);
    ucontext_t new_context;
    getcontext(&new_context);
    void *new_stack = malloc(SIGSTKSZ);
    new_context.uc_link = NULL;
    new_context.uc_stack.ss_sp = new_stack;
    new_context.uc_stack.ss_size = SIGSTKSZ;
    new_context.uc_stack.ss_flags = 0;
    makecontext(&new_context, (void (*)(void)) function, 1, arg);

    tcb *new_tcb = malloc(sizeof(tcb));
    new_tcb->id = num_thread;
    new_tcb->status = READY;
    new_tcb->context = new_context;
    new_tcb->priority = 0;
    new_tcb->waiter_id = 0;
    new_tcb->quantum = 0;
    clock_gettime(CLOCK_REALTIME, &new_tcb->create_time);
    memset(&new_tcb->start_time, 0, sizeof(new_tcb->start_time));
    memset(&new_tcb->end_time, 0, sizeof(new_tcb->end_time));

    *thread = num_thread;

    if (map == NULL) {
        map = malloc(1000 * sizeof(tcb*));
        runqueue_head = malloc(TOTAL_QUEUES * sizeof(queue_node*));
        for (int i = 0; i < TOTAL_QUEUES; i++) {
            runqueue_head[i] = malloc(sizeof(queue_node));
            runqueue_head[i]->next = NULL;
        }

        getcontext(&scheduler_context);
        void *scheduler_stack = malloc(SIGSTKSZ);
        scheduler_context.uc_link = NULL;
        scheduler_context.uc_stack.ss_sp = scheduler_stack;
        scheduler_context.uc_stack.ss_size = SIGSTKSZ;
        scheduler_context.uc_stack.ss_flags = 0;
        makecontext(&scheduler_context, (void *)&schedule, 0);

        ucontext_t main_context;
        getcontext(&main_context);
        tcb *main_tcb = malloc(sizeof(tcb));
        main_tcb->id = MAIN_THREAD_ID;
        main_tcb->status = RUNNING;
        main_tcb->context = main_context;
        main_tcb->priority = 0;
        main_tcb->waiter_id = 0;
        main_tcb->quantum = 0;
        memset(&main_tcb->create_time, 0, sizeof(main_tcb->create_time));
        memset(&main_tcb->start_time, 0, sizeof(main_tcb->start_time));
        memset(&main_tcb->end_time, 0, sizeof(main_tcb->end_time));
        map[MAIN_THREAD_ID] = main_tcb;
        current_tcb = main_tcb;

        timer();
    }

    map[num_thread++] = new_tcb;
    enqueue(runqueue_head[0], new_tcb);
    getcontext(&map[MAIN_THREAD_ID]->context);

    return 0;
}

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield() {
	
	// - change worker thread's state from Running to Ready
	// - save context of this thread to its thread control block
	// - switch from thread context to scheduler context

    // printf("yield\n");
    current_tcb->status = READY;
    swapcontext(&current_tcb->context, &scheduler_context);
    return 0;
}

/* terminate a thread */
void worker_exit(void *value_ptr) {
	// - de-allocate any dynamic memory created when starting this thread

    // printf("exit\n");
    current_tcb->retval = value_ptr;
    current_tcb->status = EXITED;
    clock_gettime(CLOCK_REALTIME, &current_tcb->end_time);
    tot_turn_time += (current_tcb->end_time.tv_sec - current_tcb->create_time.tv_sec) * 1000 + (current_tcb->end_time.tv_nsec - current_tcb->create_time.tv_nsec) / 1000000;
    tot_resp_time += (current_tcb->start_time.tv_sec - current_tcb->create_time.tv_sec) * 1000 + (current_tcb->start_time.tv_nsec - current_tcb->create_time.tv_nsec) / 1000000;
    avg_turn_time = (double)tot_turn_time / (num_thread - MAIN_THREAD_ID - 1);
    avg_resp_time = (double)tot_resp_time / (num_thread - MAIN_THREAD_ID - 1);

    if (current_tcb->waiter_id != 0) {
        tcb* waiter_tcb = map[current_tcb->waiter_id];
        waiter_tcb->status = READY;
        enqueue(runqueue_head[0], waiter_tcb);
    }

    free(current_tcb->context.uc_stack.ss_sp);
    free(current_tcb);
    current_tcb = NULL;

    setcontext(&scheduler_context);
}



/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr) {
	
	// - wait for a specific thread to terminate
	// - de-allocate any dynamic memory created by the joining thread

    // printf("join %d\n", thread);
    tcb* target_tcb = map[thread];

    if (value_ptr != NULL) {
        *value_ptr = target_tcb->retval;
    }

    if (target_tcb->status != EXITED) {
        target_tcb->waiter_id = current_tcb->id;
        current_tcb->status = BLOCKED;
        swapcontext(&current_tcb->context, &scheduler_context);
    }

    return 0;
}


/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//- initialize data structures for this mutex

    // printf("init\n");
    atomic_flag_clear(&mutex->lock);
    mutex->owner_id = 0;
    mutex->waitqueue_head = malloc(sizeof(queue_node));
    mutex->waitqueue_head->next = NULL;

	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {

        // - use the built-in test-and-set atomic function to test the mutex
        // - if the mutex is acquired successfully, enter the critical section
        // - if acquiring mutex fails, push current thread into block list and
        // context switch to the scheduler thread

    // printf("lock\n");
    while (atomic_flag_test_and_set(&mutex->lock)) {
        current_tcb->status = BLOCKED;
        enqueue(mutex->waitqueue_head, current_tcb);
        swapcontext(&current_tcb->context, &scheduler_context);
    }
    mutex->owner_id = current_tcb->id;

    return 0;
}


/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
	// - release mutex and make it available again. 
	// - put threads in block list to run queue 
	// so that they could compete for mutex later.

    // printf("unlock\n");
    atomic_flag_clear(&mutex->lock);
    while (mutex->waitqueue_head->next != NULL) {
        tcb* old_tcb = dequeue(mutex->waitqueue_head);
        old_tcb->status = READY;
        enqueue(runqueue_head[0], old_tcb);
    }
    return 0;
}



/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex) {
	// - de-allocate dynamic memory created in worker_mutex_init

    // printf("destroy\n");
    mutex->waitqueue_head->next = NULL;
    free(mutex->waitqueue_head);

	return 0;
};

/* scheduler */
static void schedule() {
	// - every time a timer interrupt occurs, your worker thread library 
	// should be contexted switched from a thread context to this 
	// schedule() function

	// - invoke scheduling algorithms according to the policy (PSJF or MLFQ)

	// if (sched == PSJF)
	//		sched_psjf();
	// else if (sched == MLFQ)
	// 		sched_mlfq();

	// YOUR CODE HERE

    // printf("schedule\n");
// - schedule policy
#ifndef MLFQ
	// Choose PSJF
    sched_psjf();
#else 
	// Choose MLFQ
    sched_mlfq();
#endif

}

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf() {
	// - your own implementation of PSJF
	// (feel free to modify arguments and return types)

    tot_cntx_switches++;
    if (current_tcb != NULL) {
        current_tcb->quantum++;
        if (current_tcb->status == READY || current_tcb->status == RUNNING) {
            enqueue(runqueue_head[0], current_tcb);
        }
    }
    current_tcb = NULL;

    queue_node *prevmin_node = NULL;
    queue_node *min_node = NULL;
    int min_quantum = 2147483647;
    queue_node* prev_node = runqueue_head[0];
    queue_node* curr_node = runqueue_head[0]->next;
    while (curr_node != NULL) {
        if (curr_node->tcb->quantum < min_quantum) {
            min_quantum = curr_node->tcb->quantum;
            min_node = curr_node;
            prevmin_node = prev_node;
        }
        prev_node = curr_node;
        curr_node = curr_node->next;
    }

    if (min_node != NULL) {
        current_tcb = min_node->tcb;
        current_tcb->status = RUNNING;
        prevmin_node->next = min_node->next;
        min_node->next = NULL;
        free(min_node);
        tot_cntx_switches++;
        if (current_tcb->start_time.tv_sec == 0 && current_tcb->start_time.tv_nsec == 0) {
            clock_gettime(CLOCK_REALTIME, &current_tcb->start_time);
        }
        // printf("psjf id %d\n", current_tcb->id);
        setcontext(&current_tcb->context);
    }
}


/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	// - your own implementation of MLFQ
	// (feel free to modify arguments and return types)

    tot_cntx_switches++;
    if (current_tcb != NULL) {
        current_tcb->quantum++;
        if (current_tcb->status == READY) {
            enqueue(runqueue_head[current_tcb->priority], current_tcb);
        } else if (current_tcb->status == RUNNING) {
            int idx = (current_tcb->priority == TOTAL_QUEUES - 1) ? current_tcb->priority : ++current_tcb->priority;
            enqueue(runqueue_head[idx], current_tcb);
        }
    }
    current_tcb = NULL;

    if (quanta % AGING_QUANTA == 0) {
        for (int i = 1; i < TOTAL_QUEUES; i++) {
            while (runqueue_head[i]->next != NULL) {
                tcb* old_tcb = dequeue(runqueue_head[i]);
                enqueue(runqueue_head[0], old_tcb);
            }
        }
    }
    
    for (int i = 0; i < TOTAL_QUEUES; i++) {
        if (runqueue_head[i]->next != NULL) {
            current_tcb = dequeue(runqueue_head[i]);
            break;
        }
    }

    if (current_tcb != NULL) {
        current_tcb->status = RUNNING;
        tot_cntx_switches++;
        if (current_tcb->start_time.tv_sec == 0 && current_tcb->start_time.tv_nsec == 0) {
            clock_gettime(CLOCK_REALTIME, &current_tcb->start_time);
        }
        // printf("mlfq id %d\n", current_tcb->id);
        setcontext(&current_tcb->context);
    }
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

