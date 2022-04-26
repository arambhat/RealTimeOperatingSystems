#ifndef __TASK_MODEL_H__
#define __TASK_MODEL_H__

/*
 * 
 */
//#define DEBUG 

#if defined(DEBUG) 
	#define DPRINTK(fmt, args...) printk("DEBUG: %s():%d: " fmt, \
   		 __func__, __LINE__, ##args)
#else
 	#define DPRINTK(fmt, args...) // do nothing if not defined
#endif

#define compiler_barrier() do { \
	__asm__ __volatile__ ("" ::: "memory"); \
} while (false)

#define STACK_SIZE  4096

#define NUM_THREADS	4		// number of threads
#define TOTAL_TIME 30000  	// total execution time in milliseconds
#define MAX_RECORD 150

struct task_s           // struct for periodic task
{
	char t_name[32]; 	// task name
	int priority; 		// priority of the task
	int period; 		// period for periodic task in milliseconds
	int loop_iter; 	   // loop iterations for compute
};

#define THREAD0 {"task00", 5, 50, 1680000}
#define THREAD1 {"task11", 8, 160, 3500000}
#define THREAD2 {"task22", 9, 220, 3640000}
#define THREAD3 {"task33", 10, 360, 3640000}

struct task_s threads[NUM_THREADS]={THREAD0, THREAD1, THREAD2, THREAD3};

struct task_aps         // struct for polling server
{
	char t_name[32]; 	// task name
	int priority; 		// assigned priority of the task
	int period; 		// replenishment period for polling task in milliseconds
	int budget; 		// budget of polling task in milliseconds
	k_tid_t poll_tid;   // thread id for the polling server
	uint32_t last_switched_in;     
	int left_budget;		// remaining budget in nanoseconds
};

#define POLL_PRIO   6
#define BUDGET 30       // execution budget for polling server in milliseconds

struct task_aps poll_info = {"polling_t", POLL_PRIO, 120, BUDGET, NULL, 0, 1000000*BUDGET};

struct req_type {       // struct for aperiodic requests
    uint32_t id;
    uint32_t iterations;    // loop iterations for compute
    uint32_t arr_time;      // the arrival time of the request
};

static void req_expiry_function(struct k_timer *timer_exp);

K_MSGQ_DEFINE(req_msgq, sizeof(struct req_type), 20, 4);
K_TIMER_DEFINE(req_timer, req_expiry_function, NULL);

// variance to generate random numbers
#define VAR_R    0
#define VAR_A    0.4
#define VAR_L    0.5

// Loop to emulate task execution
void looping(int loop_count) 
{
    volatile int t1 = loop_count;

    while (t1 > 0) t1--;
    compiler_barrier();
}

// generate random number between base*(1-var) and base
static uint32_t rand_dist(int base, float var)  
{
    int iter;
    int range;

    range = base*var;
    iter = base; 
    if (range > 0) {
        iter = iter * (1-var) + rand() % range;
    }
    return iter;

}

// end-start, if end < start, add overflow
uint64_t sub32(uint32_t start, uint32_t end)  
{  
    uint64_t a, b;

    a = (uint64_t) start;
    b = (uint64_t) end;

    if (b>a) 
            return (b-a);
    else {
        return ((b + 1<32) -a);
    }
}

#define ARR_TIME 15000      // interarrival time of aperiodic requests in microseconds
#define REQ_LOOP 420000     // aperiodic request loop count
int total_req=0;

// Timer allback function to generate aperiodic requests
static void req_expiry_function(struct k_timer *timer_exp)
{
    uint32_t stime;
    struct req_type data;

    data.id = total_req;
    data.iterations = rand_dist(REQ_LOOP, VAR_R);
    data.arr_time = k_cycle_get_32();
    k_msgq_put(&req_msgq, &data, K_NO_WAIT);
    
    total_req++;

    stime = rand_dist(ARR_TIME, VAR_A);
    k_timer_start(&req_timer, K_USEC(stime), K_NO_WAIT);

}

#endif // __TASK_MODEL_H__
