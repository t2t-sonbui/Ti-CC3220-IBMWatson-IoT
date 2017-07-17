/*
 * timer_thread.h
 *
 *  Created on: Jun 20, 2017
 *      Author: Son Bui
 */
/* POSIX Header files */
#include <pthread.h>
#include <ti/drivers/Timer.h>
#ifndef __SENSOR_THREAD_H
#define __SENSOR_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif
extern pthread_t sensorthread_handler;
extern float temperatureC;
extern int8_t      xVal, yVal, zVal;
extern void *sensorThread(void *arg0);


#endif /* __SENSOR_THREAD_H_ */
