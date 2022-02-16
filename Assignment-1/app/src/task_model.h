#ifndef __TASK_MODEL_H__
#define __TASK_MODEL_H__

/*
 * 
 */

#define NUM_MUTEXES 3		// number of mutexes
#define NUM_THREADS	4		// number of threads
#define TOTAL_TIME 4000  	// total execution time in milliseconds

struct task_s
{
	char t_name[32]; 	// task name
	int priority; 		// priority of the task
	int period; 		// period for periodic task in milliseconds
	int loop_iter[3]; 	// loop iterations for compute_1, compute_2 and compute_3
	int mutex_m; 		// the mutex id to be locked and unlocked by the task
};

#define THREAD0 {"task00", 2, 50, {400000, 400000, 400000}, 1}
#define THREAD1 {"task11", 3, 160, {800000, 900000, 800000}, 0}
#define THREAD2 {"task22", 4, 220, {200000, 2000000, 400000}, 1}
#define THREAD3 {"task33", 5, 360, {200000, 2000000, 400000}, 2}


struct task_s threads[NUM_THREADS]={THREAD0, THREAD1, THREAD2, THREAD3};

#endif // __TASK_MODEL_H__
