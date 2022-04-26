
#include <zephyr.h>
#include <kernel.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <stdbool.h>
#include <stdlib.h>
#include "task_model_p4.h"
#include <timing/timing.h>
#include <shell/shell_uart.h>

//#define DEBUG
K_CONDVAR_DEFINE(activate_signal);
K_MUTEX_DEFINE(activate_mutex);

#define DEBUG

#if defined(DEBUG) 
	#define DPRINTK(fmt, args...) printk("DEBUG: %s():%d: " fmt, \
   		 __func__, __LINE__, ##args)
#else
 	#define DPRINTK(fmt, args...) // do nothing if not defined
#endif

#define STACK_SIZE  4096

#define compiler_barrier() do { \
	__asm__ __volatile__ ("" ::: "memory"); \
} while (false)


// Global flag threads use to determine if to stop running
static bool running = true;

// k_thread structures / data
static struct k_thread thread_structs[NUM_THREADS];
static k_tid_t thread_tids[NUM_THREADS];

static int thread_index[NUM_THREADS];

static int done[NUM_THREADS];
static struct k_sem wait_sem[NUM_THREADS];

// Support up to sixteen threads' stacks
static K_THREAD_STACK_DEFINE(thread_stack_area, STACK_SIZE * NUM_THREADS);

static void timer_expiry_function(struct k_timer *timer_exp)
{
    int id = *(int *)timer_exp->user_data;
    int ret;

    ret = done[id];  
    if (ret==1) {
        k_sem_give(&wait_sem[id]);
    }
    else {
        printk("task %d misses its deadline \n", id);
        k_sem_give(&wait_sem[id]);
    }
}

// Function that is executed for each thread in the task set
static void thread(void *v_task_info, void *v_thread_id, void *unused)
{
    //wait for activation input from shell command
    k_mutex_lock(&activate_mutex, K_FOREVER);
    //k_condvar_wait(&activate_signal, &activate_mutex, K_FOREVER);
    k_mutex_unlock(&activate_mutex);

    struct task_s *task_info = (struct task_s *)v_task_info;
    int thread_id = *(int *)v_thread_id; 
 
	uint32_t period, next=0, begin = 0;

    struct k_timer task_timer;

    k_timer_init(&task_timer, timer_expiry_function, NULL);
    task_timer.user_data = v_thread_id;

    printk("task %d gets started \n", thread_id);

	period = 1000000*task_info->period; 
	begin = k_cycle_get_32();

    k_timer_start(&task_timer,K_NSEC(period), K_NSEC(period));

    while (running) {

        // Run compute
        DPRINTK("Thread %d running task\n", thread_id);

        done[thread_id]=0;

		next = next + period;
        looping(task_info->loop_iter);
        compiler_barrier();

        // Sleep for period
        DPRINTK("Thread %d sleeping for %d ms\n", thread_id, task_info->period);

        done[thread_id]=1;
        k_sem_take(&wait_sem[thread_id], K_FOREVER);
    }
}

// Start all threads defined in the task set
static void start_threads(void)
{

    for (int i = 0; i < NUM_THREADS; i++) {
        k_sem_init(&wait_sem[i], 0, 1);
        done[i]=0;
    }

    // Small delay to make sure we set each thread's name before it runs

    // Start each thread
    for (int i = 0; i < NUM_THREADS; i++) {
		thread_index[i]=i;
        thread_tids[i] = k_thread_create(&thread_structs[i],
                                         &thread_stack_area[STACK_SIZE * i],
                                         STACK_SIZE * sizeof(k_thread_stack_t),
                                         thread, (void *)&threads[i],
                                         (void *)&thread_index[i], NULL, threads[i].priority,
                                         0, K_MSEC(10));

        k_thread_name_set(thread_tids[i], threads[i].t_name);

    }
    printk("Threads Initialized!\n");
}

// "Entry point" of our code
void main(void)
{


    // Spawn the threads
    start_threads();


    // wait for signal to start timeout

    k_mutex_lock(&activate_mutex, K_FOREVER);
    //k_condvar_wait(&activate_signal, &activate_mutex, K_FOREVER);
    k_mutex_unlock(&activate_mutex);

    k_sleep(K_MSEC(TOTAL_TIME));
    // Stop the threads!
    running = false;

    //terminate waiting threads waiting on semaphore
    for (int i = 0; i < NUM_THREADS; ++i) {
        k_sem_give(&wait_sem[i]);
    }

    //wait for threads to exit
    for (int i = 0; i < NUM_THREADS; ++i) {
        k_thread_join(&thread_structs[i],K_FOREVER);
    }

    printk("Stopped threads\n");

}

static int activate_function(const struct shell *shell,
                            size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    k_mutex_lock(&activate_mutex, K_FOREVER);
    //Broadcast condition valid
    k_condvar_broadcast(&activate_signal);
    k_mutex_unlock(&activate_mutex);

    return 0;
}

SHELL_CMD_ARG_REGISTER(activate, NULL, "Activate to start program", activate_function,1,0);