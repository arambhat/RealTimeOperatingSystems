/*
 * Copyright (c) 2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * @file
 * @brief CSE 522: Assignment 1 source code file.
 * @author Ashish Kumar Rambhatla.
 */



#include <zephyr.h>
#include <sys/printk.h>
#include <shell/shell.h>
#include <version.h>
#include <logging/log.h>
#include <stdio.h>
#include "task_model.h"

/* Registering with the logger module*/
LOG_MODULE_REGISTER(app);

/* Initialising the stacks for the specified number of threads */
#define STACKSIZE 1024
K_THREAD_STACK_ARRAY_DEFINE(threadStackGlobal, NUM_THREADS, STACKSIZE);

/*
 * Defining the cutom datastructures.
 */
struct threadTimerData {
	int64_t timestamp;
	k_tid_t tid;
	int taskNumber;
	atomic_t taskCompleted;
};

struct globalTimerData {
	k_tid_t spawnedTids[NUM_THREADS];
	atomic_t exitFlag;
	const struct shell *shell; 
};

/*
 * Initialising the global datastructures.
 */
struct k_mutex mutex[NUM_MUTEXES];
struct k_thread threadStruct[NUM_THREADS];
struct threadTimerData threadSpecificData[NUM_THREADS];
struct globalTimerData gThreadData;
struct k_timer threadDeadlineTimers[NUM_THREADS]; 
struct k_timer exitTimer;


/* 
 * Forward declarations of member functions.
 */
void threadExitHandler(struct k_timer *timer);
void threadDeadlineHandler(struct k_timer *timer);
void launchTask(int taskNumber);
void threadFunction(struct task_s *taskInfo, int taskNumber, void* dummy);
void initTimerData(int taskNumber);
void taskDispatcher(void);

/*
 * This is the entry point function for the root shell command "activate".  
 */

int activate(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	gThreadData.shell = shell;
    taskDispatcher();
    return 0;
}
SHELL_CMD_REGISTER(activate, NULL, "Activating all the threads", activate);

/*
 * Definitions of member functions.
 */
void initTimerData(int taskNumber) {
	k_timer_init(&threadDeadlineTimers[taskNumber], threadDeadlineHandler, NULL);
	k_timer_user_data_set(&threadDeadlineTimers[taskNumber], (void*)&threadSpecificData[taskNumber]);
	threadSpecificData[taskNumber].timestamp = k_uptime_get();
	threadSpecificData[taskNumber].taskNumber = taskNumber;
	threadSpecificData[taskNumber].taskCompleted = ATOMIC_INIT(0);
	k_mutex_init(&mutex[threads[taskNumber].mutex_m]);
	gThreadData.exitFlag = ATOMIC_INIT(0);
}
void setTidInUserData(int taskNumber, k_tid_t tid) {
	threadSpecificData[taskNumber].tid = tid;
}
void setTidInGlobalData(int taskNumber, k_tid_t tid) {
	gThreadData.spawnedTids[taskNumber] = tid;
}
void updateTimestampInUserData(int taskNumber) {
	threadSpecificData[taskNumber].timestamp = k_uptime_get();
}
int64_t getTimestampInUserData(int taskNumber) {
	return threadSpecificData[taskNumber].timestamp;
}

/*
 * Defining timer exipiry handlers
 */

/* Handler for thread deadline expiry*/
void threadDeadlineHandler(struct k_timer *timer) {
	struct threadTimerData *threadData = (struct threadTimerData*)k_timer_user_data_get(timer);
	int taskNumber = threadData->taskNumber;
	if(atomic_get(&threadData->taskCompleted)) {
		atomic_dec(&threadData->taskCompleted);
	} else {
		printk("Deadline for the task: %d has missed\n", taskNumber);
	}
	return;
}

/* Handler for program exit expiry */
void threadExitHandler(struct k_timer *timer) {
	struct globalTimerData *gThreadData = (struct globalTimerData*)k_timer_user_data_get(timer);
	atomic_inc(&gThreadData->exitFlag);
	for (int i = 0; i< NUM_THREADS; i++) {
			k_wakeup(gThreadData->spawnedTids[i]);
	}
	return;
}

/* 
 * @function taskDispatcher
 *
 * @brief This function is called from the activate command and will initialise the 
 * 		  global datastructures, launches the individual tasks. Post intialization,
 * 		  the k_timer_status_sync will wait for the TOTAL_TIME to be elapsed. After
 * 		  the timer expires, all the spawned threads are joined into this main thread.
 */
void taskDispatcher(void) {
	/* Timier initialisations */
	k_timer_init(&exitTimer, threadExitHandler, NULL);
	k_timer_user_data_set(&exitTimer, (void*)&gThreadData);
	k_timer_start(&exitTimer, K_MSEC(TOTAL_TIME), K_NO_WAIT);
	for (int i = 0; i < NUM_THREADS; i++) {
		initTimerData(i);
	}
	/* Launching the individual threads */
	for (int i = 0; i < NUM_THREADS; i++) {
		shell_info(gThreadData.shell, "launching task: %d\n", i);
		launchTask(i);
	}
	/* Waiting for the total timer to expire */
	k_timer_status_sync(&exitTimer);
	for (int i = 0; i < NUM_THREADS; i++) {
		k_timer_stop(&threadDeadlineTimers[i]);
	}
	k_timer_stop(&exitTimer);
	/* Cleaning up the finished threads */
	for (int i = 0; i < NUM_THREADS; i++) {
		shell_info(gThreadData.shell, "joining thread: %d \n", i);
		k_thread_join(&threadStruct[i], K_FOREVER);
	}
}

/* 
 * @function launchTask
 *
 * @brief This is a helper function to intialize the thereads.
 *  
 */
void launchTask(int taskNumber) {
	struct task_s* taskInfo = &threads[taskNumber];
	k_tid_t myTid = k_thread_create(&threadStruct[taskNumber], threadStackGlobal[taskNumber],
                                 K_THREAD_STACK_SIZEOF(threadStackGlobal[taskNumber]),
                                 (k_thread_entry_t)threadFunction,
                                 taskInfo, (void*)taskNumber, NULL,
                                 taskInfo->priority, 0, K_FOREVER);
	setTidInUserData(taskNumber, myTid);
	/* 
	 * Caching the threadID of the spwaned threads to use them in 
	 * k_wakeup() api in @threadExitHandler() function call.
	 */
	setTidInGlobalData(taskNumber, myTid);
	k_thread_name_set(&threadStruct[taskNumber], taskInfo->t_name);
	k_thread_start(&threadStruct[taskNumber]);
} 

/*
 * @function compute: compute function to keep the CPU busy.
 */
void compute(int numiterations) {
	volatile uint64_t x = numiterations;
	while(x > 0)
		x--;
}

/*
 * @function threadFunction: Entry point for all the thread functions
 * 							 spawned in @launchTask() function call.
 */
void threadFunction(struct task_s *taskInfo, int taskNumber, void* dummy) {
	ARG_UNUSED(dummy);
	while(!atomic_get(&gThreadData.exitFlag)) {
		/* Starting the deadline timer */
		k_timer_start(&threadDeadlineTimers[taskNumber], K_MSEC(taskInfo->period), K_MSEC(taskInfo->period));
		updateTimestampInUserData(taskNumber);
		/* Compute sequence */
		compute(taskInfo->loop_iter[0]);
		k_mutex_lock(&mutex[taskInfo->mutex_m], K_FOREVER);
		compute(taskInfo->loop_iter[1]);
		k_mutex_unlock(&mutex[taskInfo->mutex_m]);
		compute(taskInfo->loop_iter[2]);
		/* 
		 * Setting the taskCompleted flag of the respective thread to help 
		 * the @threadDeadlineHandler() call identify if a task is completed
		 * or not.
		 */
		atomic_inc(&threadSpecificData[taskNumber].taskCompleted);
		shell_info(gThreadData.shell, "Completed the compute task and set the flag for task: %d\n", taskNumber);
		/* Putting the thread to sleep till the deadline timer expires */
		k_timer_status_sync(&threadDeadlineTimers[taskNumber]);
	}
}

void main(void)
{
	/* Nothing to be done in the main function.*/
}
