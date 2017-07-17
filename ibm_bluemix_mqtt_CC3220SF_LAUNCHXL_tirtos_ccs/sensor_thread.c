/*
 * timer_thread.c
 *
 *  Created on: Jun 20, 2017
 *      Author: Son Bui
 */
/* Includes */
#include <stddef.h>
#include <time.h>
#include <stdlib.h>

/* POSIX Header Files */
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>

/* Driver Header Files */
#include <ti/drivers/I2C.h>

/* Board Header Files */
#include "Board.h"

/* Local Header Files */
#include "uart_term.h"
/* Global Variables */
pthread_t sensorthread_handler;

/* Temperature written by the temperature thread and read by console thread */
float temperatureC;
int8_t xVal, yVal, zVal;
I2C_Handle i2cHandle;

/* Mutex to protect the reading/writing of the temperature variables */
extern pthread_mutex_t temperatureMutex;

/*
 *  ======== postSem ========
 *  Function called when the timer (created in setupTimer) expires.
 */
static void postSem(sigval val)
{
    sem_t *sem = (sem_t*) (val.sival_ptr);
    int retc;

    retc = sem_post(sem);
    if (retc == -1)
    {
        while (1)
            ;
    }
}

/*
 *  ======== setupTimer ========
 *  Create a timer that will expire at the period specified by the
 *  time arguments. When the timer expires, the passed in semaphore
 *  will be posted by the postSem function.
 *
 *  A non-zero return indicates a failure.
 */
int setupTimer(sem_t *sem, timer_t *timerid, time_t sec, long nsec)
{
    sigevent sev;
    struct itimerspec its;
    int retc;

    retc = sem_init(sem, 0, 0);
    if (retc != 0)
    {
        return (retc);
    }

    /* Create the timer that wakes up the thread that will pend on the sem. */
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_value.sival_ptr = sem;
    sev.sigev_notify_function = &postSem;
    sev.sigev_notify_attributes = NULL;
    retc = timer_create(CLOCK_MONOTONIC, &sev, timerid);
    if (retc != 0)
    {
        return (retc);
    }

    /* Set the timer to go off at the specified period */
    its.it_interval.tv_sec = sec;
    its.it_interval.tv_nsec = nsec;
    its.it_value.tv_sec = sec;
    its.it_value.tv_nsec = nsec;
    retc = timer_settime(*timerid, 0, &its, NULL);
    if (retc != 0)
    {
        timer_delete(*timerid);
        return (retc);
    }

    return (0);
}

//*****************************************************************************
//
//! Function to read temperature
//!
//! \param  none
//!
//! \return SUCCESS or FAILURE
//!
//*****************************************************************************
uint8_t temperatureReading(void)
{
    int32_t status;
    float fTempRead;

    /* Read temperature axis values */
    status = TMP006DrvGetTemp(i2cHandle, &fTempRead);
    if (status != 0)
    {
        /* try to read again */
        status = TMP006DrvGetTemp(i2cHandle, &fTempRead);
        if (status != 0) /* leave previous values */
        {
            UART_PRINT(
                    "[Sensor task] Failed to read data from temperature sensor\n\r");
        }
    }

    if (status == 0)
    {
        fTempRead = (fTempRead > 100) ? 100 : fTempRead;
        temperatureC = fTempRead;
    }

    return status;
}

//*****************************************************************************
//
//! Function to read accelarometer
//!
//! \param  none
//!
//! \return SUCCESS or FAILURE
//!
//*****************************************************************************
uint8_t accelarometerReading(void)
{
    int8_t xValRead, yValRead, zValRead;
    int32_t status;

    /* Read accelarometer axis values */
    status = BMA222ReadNew(i2cHandle, &xValRead, &yValRead, &zValRead);
    if (status != 0)
    {
        /* try to read again */
        status = BMA222ReadNew(i2cHandle, &xValRead, &yValRead, &zValRead);
        if (status != 0) /* leave previous values */
        {
            UART_PRINT(
                    "[Sensor task] Failed to read data from accelarometer\n\r");
        }
    }

    if (status == 0)
    {
        xVal = xValRead;
        yVal = yValRead;
        zVal = zValRead;
    }

    return status;
}

/*
 *  ======== sensorThread ========
 *  Creates timer modules with callback functions.
 */
void *sensorThread(void *arg0)
{

    I2C_Params i2cParams;
    sem_t semTimer;
    timer_t timerid;
    int retc;
    int32_t status;

    /*
     *  Create/Open the I2C that talks to the TMP sensor
     */
    I2C_init();

    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;
    i2cHandle = I2C_open(Board_I2C0, &i2cParams);
    if (i2cHandle == NULL)
    {
        UART_PRINT("[Sensor task] Error Initializing I2C\n\r");
    }

    /*
     *  The temperature thread blocks on the semTimer semaphore, which the
     *  timerId timer will post every second. The timer is created in the
     *  setupTimer function. It's returned so the thread could change the
     *  period or delete it if desired.
     */
    retc = setupTimer(&semTimer, &timerid, 1, 0);
    if (retc != 0)
    {
        while (1)
            ;
    }

    while (1)
    {
        /*
         *  Extract degrees C from the received data; see sensor datasheet.
         *  Make sure we are updating the global temperature variables
         *  in a thread-safe manner.
         */
        pthread_mutex_lock(&temperatureMutex);
        /* Read temperature sensor values */
        status = temperatureReading();
        if (status != 0)
        {
            UART_PRINT(
                    "[Sensor task] Failed to read data from temperature sensor\n\r");
        }

        /* Read accelarometer axis values */
        status = accelarometerReading();
        if (status != 0)
        {
            UART_PRINT(
                    "[Sensor task] Failed to read data from accelarometer\n\r");
        }

        pthread_mutex_unlock(&temperatureMutex);

        /* Block until the timer posts the semaphore. */
        retc = sem_wait(&semTimer); //Sau 1 giay
        if (retc == -1)
        {
            while (1)
                ;
        }
    }
}

