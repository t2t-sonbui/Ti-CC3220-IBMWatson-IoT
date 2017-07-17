/*
 * timer_thread.h
 *
 *  Created on: Jun 20, 2017
 *      Author: Son Bui
 */
/* POSIX Header files */
#include <pthread.h>
#include <semaphore.h>
#include <ti/drivers/Timer.h>
#ifndef __TIMERTHREAD_H
#define __TIMERTHREAD_H

#ifdef __cplusplus
extern "C" {
#endif
extern pthread_t timerthread_handler;
extern void *timerThread(void *arg0);
extern void blinkTimer_Callback(Timer_Handle myHandle);

extern Timer_Handle blinkTimer;
extern Timer_Params timer_params;
extern sem_t semEnableTimer;
extern bool enableTimer;



#endif /* __TIMER_THREAD_H_ */
