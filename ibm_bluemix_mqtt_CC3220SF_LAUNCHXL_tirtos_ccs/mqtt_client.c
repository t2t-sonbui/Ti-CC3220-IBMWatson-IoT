/* Standard includes                                                          */
#include <stdlib.h>
#include <stdio.h>

/* Hardware includes                                                          */
#include <ti/devices/cc32xx/inc/hw_types.h>
#include <ti/devices/cc32xx/inc/hw_ints.h>
#include <ti/devices/cc32xx/inc/hw_memmap.h>

/* Driverlib includes                                                         */
#include <ti/devices/cc32xx/driverlib/rom.h>
#include <ti/devices/cc32xx/driverlib/rom_map.h>
#include <ti/devices/cc32xx/driverlib/timer.h>

/* TI-Driver includes                                                         */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/SPI.h>

/* Simplelink includes                                                        */
#include <ti/drivers/net/wifi/simplelink.h>

/* MQTT Library includes                                                      */
#include <ti/net/mqtt/mqtt_client.h>

/* Common interface includes                                                  */
#include "network_if.h"
#include "uart_term.h"

/* Application includes                                                       */
#include "Board.h"
#include "client_cbs.h"
#include "pthread.h"
#include "mqueue.h"
#include "time.h"
#include "unistd.h"
#include <semaphore.h>
/* Local Header Files */
#include "timer_thread.h"
#include "sensor_thread.h"

/* Mutex to protect the reading/writing of the temperature variables */
extern pthread_mutex_t temperatureMutex;

//*****************************************************************************
//                          LOCAL DEFINES
//*****************************************************************************
#define ENABLE_SERVER           0
#define ENABLE_CLIENT           1

#define CLIENT_INIT_STATE       (0x01)
#define SERVER_INIT_STATE       (0x02)
#define MQTT_INIT_STATE         (0x04)

#define APPLICATION_VERSION     "1.1.1"
#define APPLICATION_NAME        "MQTT client"

/* Operate Lib in MQTT 3.1 mode.                                              */
#define MQTT_3_1_1              false
#define MQTT_3_1                true

#define WILL_TOPIC              "iot-2/evt/status/fmt/json"
#define WILL_MSG                "{\"d\":{\"state\":\"Not connect\"}}"
#define WILL_QOS                MQTT_QOS_0
#define WILL_RETAIN             false

/* Defining Broker IP address and port Number                                 */

#define SERVER_ADDRESS          "gq2nr0.messaging.internetofthings.ibmcloud.com"//Ibm WsIot server
//#define SERVER_ADDRESS          "m2m.eclipse.org"
#define SERVER_IP_ADDRESS       "192.168.178.67"
#define PORT_NUMBER             1883
#define SECURED_PORT_NUMBER     8883
#define LOOPBACK_PORT           1882

/* Clean session flag                                                         */
#define CLEAN_SESSION           true

/* Retain Flag. Used in publish message.                                      */
#define RETAIN                  1

/* Defining Number of topics                                                  */
#define SUB_TOPIC_COUNT         4

/* Defining Subscription Topic Values                                         */
#define TOPIC0                  "iotdm-1/response"
#define TOPIC1                  "iotdm-1/device/update"
#define TOPIC2                  "iotdm-1/mgmt/initiate/device/reboot"
#define TOPIC3                  "iot-2/cmd/led/fmt/json"
#define TOPIC4                  "iotdm-1/mgmt/initiate/device/factory_reset"

/* Spawn task priority and Task and Thread Stack Size                         */
#define OSI_STACK_SIZE          2048
#define TASKSTACKSIZE           2048
#define RXTASKSIZE              4096
#define MQTTTHREADSIZE          2048
#define SPAWN_TASK_PRIORITY     9

/* secured client requires time configuration, in order to verify server      */
/* certificate validity (date).                                               */

/* Day of month (DD format) range 1-31                                        */
#define DAY                     11
/* Month (MM format) in the range of 1-12                                     */
#define MONTH                   8
/* Year (YYYY format)                                                         */
#define YEAR                    2016
/* Hours in the range of 0-23                                                 */
#define HOUR                    12
/* Minutes in the range of 0-59                                               */
#define MINUTES                 33
/* Seconds in the range of 0-59                                               */
#define SEC                     21   

#define CLIENT_NUM_SECURE_FILES 1

//*****************************************************************************
//                      LOCAL FUNCTION PROTOTYPES
//*****************************************************************************
void pushButtonInterruptHandler2(uint_least8_t index);
void pushButtonInterruptHandler3(uint_least8_t index);
void TimerPeriodicIntHandler(sigval val);
void LedTimerConfigNStart();
void LedTimerDeinitStop();
static void DisplayBanner(char * AppName);
void *MqttClient(void *pvParameters);
void Mqtt_ClientStop(uint8_t disconnect);
void Mqtt_ServerStop();
void Mqtt_Stop();
void Mqtt_start();
int32_t Mqtt_IF_Connect();
int32_t MqttServer_start();
int32_t MqttClient_start();
int32_t MQTT_SendMsgToQueue(struct msgQueue *queueElement);

//*****************************************************************************
//                 GLOBAL VARIABLES
//*****************************************************************************
uint32_t gInitState = 0;
int32_t gApConnectionState = -1;
uint32_t memPtrCounterAlloc = 0;
uint32_t memPtrCounterfree = 0;
uint32_t gWaitForExternalRestart = 0;
_SlLockObj_t reportLockObj;
static NetAppHandle_t gMqttClient;
MQTTClient_Attrib_t MqttClientExmple_params;
unsigned short g_usTimerInts;
sem_t semSendData;

/* Receive task handle                                                        */
pthread_t g_rx_task_hndl = (pthread_t) NULL;
pthread_t g_server_task_hndl = (pthread_t) NULL;
uint32_t gUiConnFlag = 0;

/* AP Security Parameters                                                     */
SlWlanSecParams_t SecurityParams = { 0 };

/* Client ID,User Name and Password                                           */
//char ClientId[64];//d:kexa3u:CC3220FS-LauchXL:Office
char *ClientId = "d:gq2nr0:CC3220FS-LauchXL:Office";//Create client id from information of IBM Iot Device Dashboard
char *Username = "use-token-auth"; //use-token-auth
char *Password = "8tbN7k4m*H(7TB?Mow"; //token

/* Subscription topics and qos values                                         */
char *topic[SUB_TOPIC_COUNT] = { TOPIC0, TOPIC1, TOPIC2, TOPIC3 };

unsigned char qos[SUB_TOPIC_COUNT] = { MQTT_QOS_0, MQTT_QOS_0, MQTT_QOS_0,
MQTT_QOS_0 };

struct BridgeHandle
{
    /* Handle to the client context                                           */
    void *ClientCtxHndl;
} BridgeHndl;

/* Message Queue                                                              */
mqd_t g_PBQueue;
pthread_t mqttThread = (pthread_t) NULL;
pthread_t appThread = (pthread_t) NULL;
timer_t g_timer;

/* Publishing topics and messages                                             */

const char *pub_topic = { "iot-2/evt/status/fmt/json" };
const char *data = { "{\"d\":{\"temp\":30,\"ax\":10,\"ay\":20,\"az\":30}}" };
char pub_data[128];

//*****************************************************************************
//                 Banner VARIABLES
//*****************************************************************************
char lineBreak[] = "\n\r";
/* enables user and password client  */
#define CLNT_USR_PWD
/* enables secured client                                                     */
//#define SECURE_CLIENT
#ifdef  SECURE_CLIENT

char *Mqtt_Client_secure_files[CLIENT_NUM_SECURE_FILES] =
{   "ca-cert.pem"};

/* Initialization structure to be used with sl_ExtMqtt_Init API. In order to  */
/* use secured socket method, cipher, n_files and secure_files must be        */
/* configured. certificates also must be programmed  ("ca-cert.pem").         */
MQTTClient_NetAppConnParams_t Mqtt_ClientCtx =
{
    0,
    SERVER_IP_ADDRESS, //SERVER_ADDRESS,
    SECURED_PORT_NUMBER,//  PORT_NUMBER
    SL_SO_SEC_METHOD_SSLv3_TLSV1_2,
    SL_SEC_MASK_SECURE_DEFAULT,
    CLIENT_NUM_SECURE_FILES,
    Mqtt_Client_secure_files
};

void setTime()
{
    SlDateTime_t dateTime =
    {   0};
    dateTime.tm_day = (uint32_t)DAY;
    dateTime.tm_mon = (uint32_t)MONTH;
    dateTime.tm_year = (uint32_t)YEAR;
    dateTime.tm_hour = (uint32_t)HOUR;
    dateTime.tm_min = (uint32_t)MINUTES;
    dateTime.tm_sec = (uint32_t)SEC;
    sl_DeviceSet(SL_DEVICE_GENERAL, SL_DEVICE_GENERAL_DATE_TIME, sizeof(SlDateTime_t), (uint8_t *)(&dateTime));
}
#else
MQTTClient_NetAppConnParams_t Mqtt_ClientCtx = {
MQTTCLIENT_NETCONN_URL,
                                                 SERVER_ADDRESS,
                                                 PORT_NUMBER,
                                                 0, 0, 0,
                                                 NULL };
#endif

MQTTClient_Will_t will_param = {
WILL_TOPIC,
                                 WILL_MSG,
                                 WILL_QOS,
                                 WILL_RETAIN };

//*****************************************************************************
//
//! MQTT_SendMsgToQueue - Utility function that receive msgQueue parameter and 
//! tries to push it the queue with minimal time for timeout of 0.
//! If the queue isn't full the parameter will be stored and the function
//! will return 0.
//! If the queue is full and the timeout expired (because the timeout parameter
//! is 0 it will expire immediately), the parameter is thrown away and the
//! function will return -1 as an error for full queue.
//!
//! \param[in] struct msgQueue *queueElement
//!
//! \return 0 on success, -1 on error
//
//*****************************************************************************
int32_t MQTT_SendMsgToQueue(struct msgQueue *queueElement)
{
    struct timespec abstime = { 0 };

    clock_gettime(CLOCK_REALTIME, &abstime);
    if (g_PBQueue)
    {
        /* send message to the queue                                         */
        if (mq_timedsend(g_PBQueue, (char *) queueElement,
                         sizeof(struct msgQueue), 0, &abstime) == 0)
        {
            return 0;
        }
    }
    return -1;
}

//*****************************************************************************
//
//! Timer call to publish a message. Write message into message queue signaling the
//! event publish messages
//!
//! \param none
//!
//! return none
//
//*****************************************************************************
void *SendDataFxn(void *arg0)
{
    int rc;

    /* Initialize Semaphore */
    rc = sem_init(&semSendData, 0, 0);
    if (rc == -1)
    {
        while (1)
            ;
    }
    while (1)
    {
        /* Timer tells when to send data*/
        rc = sem_wait(&semSendData);
        if (rc == 0)
        {
            //Post meg
            struct msgQueue queueElement;

            queueElement.event = PUB_SEND_DATA_BY_TIMER;
            queueElement.msgPtr = NULL;

            /* write message indicating publish message                              */
            if (MQTT_SendMsgToQueue(&queueElement))
            {
                UART_PRINT("\n\n\rQueue is full\n\n\r");
            }
        }
    }
}

//*****************************************************************************
//
//! Push Button Handler1(GPIOSW2). Press push button1 (GPIOSW2) Whenever user
//! wants to publish a message. Write message into message queue signaling the
//! event publish messages
//!
//! \param none
//!
//! return none
//
//*****************************************************************************
void pushButtonInterruptHandler2(uint_least8_t index)
{
    struct msgQueue queueElement;

    queueElement.event = PUB_PUSH_BUTTON_PRESSED;
    queueElement.msgPtr = NULL;

    /* write message indicating publish message                              */
    if (MQTT_SendMsgToQueue(&queueElement))
    {
        UART_PRINT("\n\n\rQueue is full\n\n\r");
    }

}

//*****************************************************************************
//
//! Push Button Handler2(GPIOSW3). Press push button3 Whenever user wants to
//! disconnect from the remote broker. Write message into message queue
//! indicating disconnect from broker.
//!
//! \param none
//!
//! return none
//
//*****************************************************************************
void pushButtonInterruptHandler3(uint_least8_t index)
{
    struct msgQueue queueElement;
    struct msgQueue queueElemRecv;

    queueElement.event = DISC_PUSH_BUTTON_PRESSED;
    queueElement.msgPtr = NULL;

    /* write message indicating disconnect push button pressed message       */
    if (MQTT_SendMsgToQueue(&queueElement))
    {
        UART_PRINT(
                "\n\n\rQueue is full, throw first msg and send the new one\n\n\r");
        mq_receive(g_PBQueue, (char*) &queueElemRecv, sizeof(struct msgQueue),
        NULL);
        MQTT_SendMsgToQueue(&queueElement);
    }

}

void enableLogData()
{

    int rc;
    enableTimer = 1;
    /* Post to enable timer. */
    rc = sem_post(&semEnableTimer);
    if (rc == -1)
    {
        while (1)
            ;
    }
}
void disableLogData()
{

    int rc;
    enableTimer = 0;
    /* Post to enable timer. */
    rc = sem_post(&semEnableTimer);
    if (rc == -1)
    {
        while (1)
            ;
    }
}

//*****************************************************************************
//
//! Periodic Timer Interrupt Handler
//!
//! \param None
//!
//! \return None
//
//*****************************************************************************
void TimerPeriodicIntHandler(sigval val)
{
    unsigned long ulInts;

    /* Clear all pending interrupts from the timer we are currently using.    */
    ulInts = MAP_TimerIntStatus(TIMERA0_BASE, true);
    MAP_TimerIntClear(TIMERA0_BASE, ulInts);

    /* Increment our interrupt counter.                                       */
    g_usTimerInts++;

    if (!(g_usTimerInts & 0x1))
    {
        /* Turn Led Off                                                       */
        GPIO_write(Board_LED0, Board_LED_OFF);
    }
    else
    {
        /* Turn Led On                                                        */
        GPIO_write(Board_LED0, Board_LED_ON);
    }
}

//*****************************************************************************
//
//! Function to configure and start timer to blink the LED while device is
//! trying to connect to an AP
//!
//! \param none
//!
//! return none
//
//*****************************************************************************
void LedTimerConfigNStart()
{
    struct itimerspec value;
    sigevent sev;

    /* Create Timer                                                           */
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_notify_function = &TimerPeriodicIntHandler;
    timer_create(2, &sev, &g_timer);

    /* start timer                                                            */
    value.it_interval.tv_sec = 0;
    value.it_interval.tv_nsec = 100 * 1000000;
    value.it_value.tv_sec = 0;
    value.it_value.tv_nsec = 0;

    timer_settime(g_timer, 0, &value, NULL);
}

//*****************************************************************************
//
//! Disable the LED blinking Timer as Device is connected to AP
//!
//! \param none
//!
//! return none
//
//*****************************************************************************
void LedTimerDeinitStop()
{
    /* Disable the LED blinking Timer as Device is connected to AP.           */
    timer_delete(g_timer);
}

//*****************************************************************************
//
//! Application startup display on UART
//!
//! \param  none
//!
//! \return none
//!
//*****************************************************************************
static void DisplayBanner(char * AppName)
{
    UART_PRINT("\n\n\n\r");
    UART_PRINT("\t\t *************************************************\n\r");
    UART_PRINT("\t\t    CC3220 %s Application       \n\r", AppName);
    UART_PRINT("\t\t *************************************************\n\r");
    UART_PRINT("\n\n\n\r");
}

void *MqttClientThread(void * pvParameters)
{
    struct msgQueue queueElement;
    struct msgQueue queueElemRecv;

    MQTTClient_run((NetAppHandle_t) pvParameters);

    queueElement.event = BROKER_DISCONNECTION;
    queueElement.msgPtr = NULL;

    /* write message indicating disconnect Broker message.                    */
    if (MQTT_SendMsgToQueue(&queueElement))
    {
        UART_PRINT(
                "\n\n\rQueue is full, throw first msg and send the new one\n\n\r");
        mq_receive(g_PBQueue, (char*) &queueElemRecv, sizeof(struct msgQueue),
        NULL);
        MQTT_SendMsgToQueue(&queueElement);
    }

    pthread_exit(0);

    return NULL;
}

//*****************************************************************************
//
//! Task implementing MQTT Server plus client bridge
//!
//! This function
//!    1. Initializes network driver and connects to the default AP
//!    2. Initializes the mqtt client ans server libraries and set up MQTT
//!       with the remote broker.
//!    3. set up the button events and their callbacks(for publishing)
//!    4. handles the callback signals
//!
//! \param  none
//!
//! \return None
//!
//*****************************************************************************
void * MqttClient(void *pvParameters)
{
    uint8_t localAx,localAy,localAz;
    float localTemperatureC;
    struct msgQueue queueElemRecv;
    struct publishMsgHeader msgHead;
    uint32_t uiTopOffset = 0;
    uint32_t uiPayloadOffset = 0;
    long lRetVal = -1;
    char *tmpBuff;
    uint32_t flags;

    /* enable interrupt for the GPIO 13 (SW3) and GPIO 22 (SW2).               */
    GPIO_setCallback(Board_BUTTON0, pushButtonInterruptHandler2);
    GPIO_enableInt(Board_BUTTON0); // SW2

    GPIO_setCallback(Board_BUTTON1, pushButtonInterruptHandler3);
    GPIO_enableInt(Board_BUTTON1); // SW3

    /* Initializing Client and Subscribing to the Broker.                     */
#if ENABLE_CLIENT
    if (gApConnectionState >= 0)
    {
        lRetVal = MqttClient_start();
        if (lRetVal == -1)
        {
            UART_PRINT("MQTT Client lib initialization failed\n\r");
            pthread_exit(0);
            return NULL;
        }
    }
#endif

    /* handling the signals from various callbacks including the push button  */
    /* prompting the client to publish a msg on PUB_TOPIC OR msg received by  */
    /* the server on enrolled topic(for which the on-board client ha enrolled)*/
    /* from a local client(will be published to the remote broker by the      */
    /* client) OR msg received by the client from the remote broker (need to  */
    /* be sent to the server to see if any local client has subscribed on the */
    /* same topic).                                                           */
    for (;;)
    {
        /* waiting for signals                                                */
        mq_receive(g_PBQueue, (char*) &queueElemRecv, sizeof(struct msgQueue),
        NULL);

        switch (queueElemRecv.event)
        {
        case PUB_PUSH_BUTTON_PRESSED:

            /* send publish message                                       */
            lRetVal = MQTTClient_publish(gMqttClient, (char*) pub_topic,
                                         strlen((char*) pub_topic),
                                         (char*) data, strlen((char*) data),
                                         MQTT_QOS_0 | MQTT_PUBLISH_RETAIN);

            UART_PRINT("\n\r CC3200 Publishes the following message \n\r");
            UART_PRINT("Topic: %s\n\r", pub_topic);
            UART_PRINT("Data: %s\n\r", data);
            break;

            /* msg received by server (on the enrolled topic by on-board  */
            /* client) publish it to the remote broker                    */

        case PUB_SEND_DATA_BY_TIMER:

            pthread_mutex_lock(&temperatureMutex);
            localTemperatureC =  temperatureC;
            localAx=xVal;
            localAy=yVal;
            localAz=zVal;
            pthread_mutex_unlock(&temperatureMutex);

            sprintf(pub_data,
                    "{\"d\":{\"temp\":%2.2f,\"ax\":%u,\"ay\":%u,\"az\":%u}}",
                    localTemperatureC,localAx,localAy,localAz);
            /* send publish message                                       */
            lRetVal = MQTTClient_publish(gMqttClient, (char*) pub_topic,
                                         strlen((char*) pub_topic), pub_data,
                                         strlen(pub_data),
                                         MQTT_QOS_0 | MQTT_PUBLISH_RETAIN);

            UART_PRINT(
                    "\n\r CC3200 Publishes message by timer callback sempost \n\r");
            UART_PRINT("Topic: %s\n\r", pub_topic);
            UART_PRINT("Data: %s\n\r", pub_data);
            break;

            /* msg received by server (on the enrolled topic by on-board  */
            /* client) publish it to the remote broker                    */
        case MSG_RECV_BY_SERVER:
            if (gUiConnFlag == 0)
            {
                free(queueElemRecv.msgPtr);
                memPtrCounterfree++;
                break;
            }

            memcpy(&msgHead, queueElemRecv.msgPtr, sizeof(msgHead));
            uiTopOffset = sizeof(msgHead);
            uiPayloadOffset = uiTopOffset + msgHead.topicLen + 1;
            flags = msgHead.qos;
            if (msgHead.retain)
            {
                flags = flags | MQTT_PUBLISH_RETAIN;
            }

            lRetVal = MQTTClient_publish(
                    gMqttClient, ((char*) (queueElemRecv.msgPtr) + uiTopOffset),
                    strlen(((char *) (queueElemRecv.msgPtr) + uiTopOffset)),
                    ((char*) (queueElemRecv.msgPtr) + uiPayloadOffset),
                    msgHead.payLen, flags);
            free(queueElemRecv.msgPtr);
            memPtrCounterfree++;
            break;

            /* msg received by client from remote broker (on a topic      */
            /* subscribed by local client)                                */
        case MSG_RECV_BY_CLIENT:
            tmpBuff = (char *) ((char *) queueElemRecv.msgPtr + 12);
            UART_PRINT("MSG_RECV_BY_CLIENT: %s\n\r", tmpBuff);
//            if (strncmp(tmpBuff, TOPIC1, queueElemRecv.topLen) == 0)
//            {
//                GPIO_toggle(Board_LED0);
//            }
//            else if (strncmp(tmpBuff, TOPIC2, queueElemRecv.topLen) == 0)
//            {
//                GPIO_toggle(Board_LED1);
//            }
//            else if (strncmp(tmpBuff, TOPIC3, queueElemRecv.topLen) == 0)//rec cmd from UI
//            {
//                GPIO_toggle(Board_LED2);
//            }

            free(queueElemRecv.msgPtr);
            memPtrCounterfree++;
            break;

            /* on-board client disconnected from remote broker, only      */
            /* local MQTT network will work                               */
        case BROKER_DISCONNECTION:
            gUiConnFlag = 0;
            break;

            /* push button for full restart check                         */
        case DISC_PUSH_BUTTON_PRESSED:
            gWaitForExternalRestart = 1;
            break;
        case THREAD_TERMINATE_REQ:
            gUiConnFlag = 0;
            pthread_exit(0);
            return NULL;
        default:
            sleep(1);
            break;
        }
    }
}

//*****************************************************************************
//
//! MQTT interface connect
//!
//!
//! \param  none
//!
//! \return None
//!
//*****************************************************************************
int32_t Mqtt_IF_Connect()
{
    int32_t lRetVal;
    char SSID_Remote_Name[32];
    int8_t Str_Length;

    memset(SSID_Remote_Name, '\0', sizeof(SSID_Remote_Name));
    Str_Length = strlen(SSID_NAME);

    if (Str_Length)
    {
        /* Copy the Default SSID to the local variable                        */
        strncpy(SSID_Remote_Name, SSID_NAME, Str_Length);
    }

    /* Display Application Banner                                             */
    DisplayBanner(APPLICATION_NAME);

    GPIO_write(Board_LED0, Board_LED_OFF);
    GPIO_write(Board_LED1, Board_LED_OFF);
    GPIO_write(Board_LED2, Board_LED_OFF);

    /* Reset The state of the machine                                         */
    Network_IF_ResetMCUStateMachine();

    /* Start the driver                                                       */
    lRetVal = Network_IF_InitDriver(ROLE_STA);
    if (lRetVal < 0)
    {
        UART_PRINT("Failed to start SimpleLink Device\n\r", lRetVal);
        return -1;
    }

    /* switch on Green LED to indicate Simplelink is properly up.             */
    GPIO_write(Board_LED2, Board_LED_ON);

    /* Start Timer to blink Red LED till AP connection                        */
    LedTimerConfigNStart();

    /* Initialize AP security params                                          */
    SecurityParams.Key = (signed char *) SECURITY_KEY;
    SecurityParams.KeyLen = strlen(SECURITY_KEY);
    SecurityParams.Type = SECURITY_TYPE;

    /* Connect to the Access Point                                            */
    lRetVal = Network_IF_ConnectAP(SSID_Remote_Name, SecurityParams);
    if (lRetVal < 0)
    {
        UART_PRINT("Connection to an AP failed\n\r");
        return -1;
    }

    /* Disable the LED blinking Timer as Device is connected to AP.           */
    LedTimerDeinitStop();

    /* Switch ON RED LED to indicate that Device acquired an IP.              */
    GPIO_write(Board_LED0, Board_LED_ON);

    sleep(1);

    GPIO_write(Board_LED0, Board_LED_OFF);
    GPIO_write(Board_LED1, Board_LED_OFF);
    GPIO_write(Board_LED2, Board_LED_OFF);

    return 0;
}

//*****************************************************************************
//
//!  MQTT Start
//!
//!
//! \param  none
//!
//! \return None
//!
//*****************************************************************************
void Mqtt_start()
{
    int32_t threadArg = 100;
    pthread_attr_t pAttrs;
    struct sched_param priParam;
    int32_t retc = 0;
    mq_attr attr;
    unsigned mode = 0;

    /* sync object for inter thread communication                             */
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(struct msgQueue);
    g_PBQueue = mq_open("g_PBQueue", O_CREAT, mode, &attr);

    if (g_PBQueue == NULL)
    {
        UART_PRINT("MQTT Message Queue create fail\n\r");
        gInitState &= ~MQTT_INIT_STATE;
        return;
    }

    /* Set priority and stack size attributes                                 */
    pthread_attr_init(&pAttrs);
    priParam.sched_priority = 2;
    retc = pthread_attr_setschedparam(&pAttrs, &priParam);
    retc |= pthread_attr_setstacksize(&pAttrs, MQTTTHREADSIZE);
    retc |= pthread_attr_setdetachstate(&pAttrs, PTHREAD_CREATE_DETACHED);

    if (retc != 0)
    {
        gInitState &= ~MQTT_INIT_STATE;
        UART_PRINT("MQTT thread create fail\n\r");
        return;
    }

    retc = pthread_create(&mqttThread, &pAttrs, MqttClient,
                          (void *) &threadArg);
    if (retc != 0)
    {
        gInitState &= ~MQTT_INIT_STATE;
        UART_PRINT("MQTT thread create fail\n\r");
        return;
    }
    gInitState &= ~MQTT_INIT_STATE;

}

void Mqtt_Stop()
{
    struct msgQueue queueElement;
    struct msgQueue queueElemRecv;

#if ENABLE_CLIENT
    if (gApConnectionState >= 0)
    {
        Mqtt_ClientStop(1);
    }
#endif

    queueElement.event = THREAD_TERMINATE_REQ;
    queueElement.msgPtr = NULL;

    /* write message indicating publish message                               */
    if (MQTT_SendMsgToQueue(&queueElement))
    {
        UART_PRINT(
                "\n\n\rQueue is full, throw first msg and send the new one\n\n\r");
        mq_receive(g_PBQueue, (char*) &queueElemRecv, sizeof(struct msgQueue),
        NULL);
        MQTT_SendMsgToQueue(&queueElement);
    }

    sleep(2);

    mq_close(g_PBQueue);
    g_PBQueue = NULL;

    sl_Stop(200);
    UART_PRINT("\n\r Client Stop completed\r\n");
}

int32_t MqttClient_start()
{
    int32_t lRetVal = -1;
    int32_t iCount = 0;

    int32_t threadArg = 100;
    pthread_attr_t pAttrs;
    struct sched_param priParam;

    gInitState |= CLIENT_INIT_STATE;
    /* Initialize MQTT client lib                                              */
    MqttClientExmple_params.clientId = ClientId;
    MqttClientExmple_params.connParams = &Mqtt_ClientCtx;
    MqttClientExmple_params.mqttMode31 = MQTT_3_1;
    MqttClientExmple_params.blockingSend = true;

    gMqttClient = MQTTClient_create(MqttClientCallback,
                                    &MqttClientExmple_params);
    if (gMqttClient == NULL)
    {
        /* lib initialization failed                                          */
        gInitState &= ~CLIENT_INIT_STATE;
        return -1;
    }

    /* Open Client Receive Thread start the receive task. Set priority and    */
    /* stack size attributes                                                  */
    pthread_attr_init(&pAttrs);
    priParam.sched_priority = 2;
    lRetVal = pthread_attr_setschedparam(&pAttrs, &priParam);
    lRetVal |= pthread_attr_setstacksize(&pAttrs, RXTASKSIZE);
    lRetVal |= pthread_attr_setdetachstate(&pAttrs, PTHREAD_CREATE_DETACHED);
    lRetVal |= pthread_create(&g_rx_task_hndl, &pAttrs, MqttClientThread,
                              (void *) &threadArg);
    if (lRetVal != 0)
    {
        UART_PRINT("Client Thread Create Failed failed\n\r");
        gInitState &= ~CLIENT_INIT_STATE;
        return -1;
    }
#ifdef SECURE_CLIENT
    setTime();
#endif

    /* setting will parameters                                                */
    MQTTClient_set(gMqttClient, MQTT_CLIENT_WILL_PARAM, &will_param,
                   sizeof(will_param));

#ifdef CLNT_USR_PWD
    /* Set user name for client connection                                    */
    MQTTClient_set(gMqttClient, MQTT_CLIENT_USER_NAME, Username,
                   strlen((char*) Username));

    /* Set password                                                           */
    MQTTClient_set(gMqttClient, MQTT_CLIENT_PASSWORD, Password,
                   strlen((char*) Password));
#endif
    /* Initiate MQTT Connect                                                  */
    if (gApConnectionState >= 0)
    {
#if CLEAN_SESSION == false
        bool clean = CLEAN_SESSION;
        lRetVal |= MQTTClient_set(gMqttClient, MQTT_CLIENT_CLEAN_CONNECT, &clean, sizeof(bool));
#endif
        lRetVal |= MQTTClient_connect(gMqttClient);

        if (lRetVal != 0)
        {
            /* lib initialization failed                                      */
            UART_PRINT("Connection to broker failed\n\r");

            gUiConnFlag = 0;
        }
        else
        {
            gUiConnFlag = 1;
        }
        /* Subscribe to topics                                                */
        if (gUiConnFlag == 1)
        {
            uint8_t subIndex;
            MQTTClient_SubscribeParams_t subInfo[SUB_TOPIC_COUNT];

            for (subIndex = 0; subIndex < SUB_TOPIC_COUNT; subIndex++)
            {
                subInfo[subIndex].topic = topic[subIndex];
                subInfo[subIndex].qos = qos[subIndex];
            }

            if (MQTTClient_subscribe(gMqttClient, subInfo, SUB_TOPIC_COUNT) < 0)
            {
                UART_PRINT("\n\r Subscription Error \n\r");
                MQTTClient_disconnect(gMqttClient);
                gUiConnFlag = 0;
            }
            else
            {
                for (iCount = 0; iCount < SUB_TOPIC_COUNT; iCount++)
                {
                    UART_PRINT("Client subscribed on %s\n\r,", topic[iCount]);
                }
            }
        }
    }

    gInitState &= ~CLIENT_INIT_STATE;

    return 0;
}

void Mqtt_ClientStop(uint8_t disconnect)
{
    uint32_t iCount;

    MQTTClient_UnsubscribeParams_t subInfo[SUB_TOPIC_COUNT];

    for (iCount = 0; iCount < SUB_TOPIC_COUNT; iCount++)
    {
        subInfo[iCount].topic = topic[iCount];
    }

    MQTTClient_unsubscribe(gMqttClient, subInfo, SUB_TOPIC_COUNT);
    for (iCount = 0; iCount < SUB_TOPIC_COUNT; iCount++)
    {
        UART_PRINT("Unsubscribed from the topic %s\r\n", topic[iCount]);
    }
    gUiConnFlag = 0;

    /* exiting the Client library                                             */
    MQTTClient_delete(gMqttClient);

}

void Timer_trigger_init()
{
    pthread_t SendDataFxn_Handle;
    pthread_attr_t pAttrs;
    struct sched_param priParam;
    int32_t retc = 0;

    /* Set priority and stack size attributes                                 */
    pthread_attr_init(&pAttrs);
    priParam.sched_priority = 2;
    retc = pthread_attr_setschedparam(&pAttrs, &priParam);
    retc |= pthread_attr_setdetachstate(&pAttrs, PTHREAD_CREATE_DETACHED);
    if (retc != 0)
    {
        /* pthread_attr_setdetachstate() failed */
        while (1)
            ;
    }
    retc |= pthread_attr_setstacksize(&pAttrs, 1024);
    if (retc != 0)
    {
        /* pthread_attr_setstacksize() failed */
        while (1)
            ;
    }
    /* Create SendDataFxn thread */
    retc = pthread_create(&SendDataFxn_Handle, &pAttrs, SendDataFxn,
    NULL);
    if (retc != 0)
    {
        /* pthread_create() failed */
        while (1)
            ;
    }

}

void printBorder(char ch, int n)
{
    int i = 0;

    for (i = 0; i < n; i++)
        putch(ch);
}

int32_t DisplayAppBanner(char* appName, char* appVersion)
{
    int32_t ret = 0;
    uint8_t macAddress[SL_MAC_ADDR_LEN];
    uint16_t macAddressLen = SL_MAC_ADDR_LEN;
    uint16_t ConfigSize = 0;
    uint8_t ConfigOpt = SL_DEVICE_GENERAL_VERSION;
    SlDeviceVersion_t ver = { 0 };
    uint8_t Index;

    ConfigSize = sizeof(SlDeviceVersion_t);

    /* Print device version info. */
    ret = sl_DeviceGet(SL_DEVICE_GENERAL, &ConfigOpt, &ConfigSize,
                       (uint8_t*) (&ver));

    /* Print device Mac address */
    ret = sl_NetCfgGet(SL_NETCFG_MAC_ADDRESS_GET, 0, &macAddressLen,
                       &macAddress[0]);

    UART_PRINT(lineBreak);
    UART_PRINT("\t");
    printBorder('=', 44);
    UART_PRINT(lineBreak);
    UART_PRINT("\t   %s Example Ver: %s", appName, appVersion);
    UART_PRINT(lineBreak);
    UART_PRINT("\t");
    printBorder('=', 44);
    UART_PRINT(lineBreak);
    UART_PRINT(lineBreak);
    UART_PRINT("\t CHIP: 0x%x", ver.ChipId);
    UART_PRINT(lineBreak);
    UART_PRINT("\t MAC:  %d.%d.%d.%d", ver.FwVersion[0], ver.FwVersion[1],
               ver.FwVersion[2], ver.FwVersion[3]);
    UART_PRINT(lineBreak);
    UART_PRINT("\t PHY:  %d.%d.%d.%d", ver.PhyVersion[0], ver.PhyVersion[1],
               ver.PhyVersion[2], ver.PhyVersion[3]);
    UART_PRINT(lineBreak);
    UART_PRINT("\t NWP:  %d.%d.%d.%d", ver.NwpVersion[0], ver.NwpVersion[1],
               ver.NwpVersion[2], ver.NwpVersion[3]);
    UART_PRINT(lineBreak);
    UART_PRINT("\t ROM:  %d", ver.RomVersion);
    UART_PRINT(lineBreak);
    UART_PRINT("\t HOST: %s", SL_DRIVER_VERSION);
    UART_PRINT(lineBreak);
    UART_PRINT("\t MAC address: %02x:%02x:%02x:%02x:%02x:%02x", macAddress[0],
               macAddress[1], macAddress[2], macAddress[3], macAddress[4],
               macAddress[5]);
    UART_PRINT(lineBreak);
    UART_PRINT(lineBreak);
    UART_PRINT("\t");
    printBorder('=', 44);
    UART_PRINT(lineBreak);
    UART_PRINT(lineBreak);
    UART_PRINT("\t MQTT ClientId: %s", ClientId);
    UART_PRINT(lineBreak);

//     for (Index = 0; Index < 6; Index++)
//     {
//         ClientId[Index] = (char)macAddress[Index];
//     }

    return ret;
}

void mainThread(void * args)
{

    uint32_t count = 0;
    pthread_t spawn_thread = (pthread_t) NULL;
    pthread_attr_t pAttrs_spawn;
    struct sched_param priParam;
    int32_t retc = 0;
    UART_Handle tUartHndl;

    Board_initSPI();

    /* Configure the UART                                                     */
    tUartHndl = InitTerm();
    /* remove uart receive from LPDS dependency                               */
    UART_control(tUartHndl, UART_CMD_RXDISABLE, NULL);

    /* Create the sl_Task                                                     */
    pthread_attr_init(&pAttrs_spawn);
    priParam.sched_priority = SPAWN_TASK_PRIORITY;
    retc = pthread_attr_setschedparam(&pAttrs_spawn, &priParam);
    retc |= pthread_attr_setstacksize(&pAttrs_spawn, TASKSTACKSIZE);
    retc |= pthread_attr_setdetachstate(&pAttrs_spawn, PTHREAD_CREATE_DETACHED);

    retc = pthread_create(&spawn_thread, &pAttrs_spawn, sl_Task, NULL);

    if (retc != 0)
    {
        UART_PRINT("could not create simplelink task\n\r");
        while (1)
            ;
    }

    retc = sl_Start(0, 0, 0);
    if (retc < 0)
    {
        /* Handle Error */
        UART_PRINT("\n sl_Start failed\n");
        while (1)
            ;
    }

    Timer_trigger_init(); //init call update data from timer callback event
    /* Output device information to the UART terminal */
    retc = DisplayAppBanner(APPLICATION_NAME, APPLICATION_VERSION);

    retc = sl_Stop(SL_STOP_TIMEOUT);
    if (retc < 0)
    {
        /* Handle Error */
        UART_PRINT("\n sl_Stop failed\n");
        while (1)
            ;
    }

    if (retc < 0)
    {
        /* Handle Error */
        UART_PRINT("mqtt_client - Unable to retrieve device information \n");
        while (1)
            ;
    }

    while (1)
    {
        gWaitForExternalRestart = 0;
        topic[0] = TOPIC0;
        topic[1] = TOPIC1;
        topic[2] = TOPIC2;
        topic[3] = TOPIC3;
        gInitState = 0;

        /* Connect to AP                                                      */
        gApConnectionState = Mqtt_IF_Connect();

        gInitState |= MQTT_INIT_STATE;
        /* Run MQTT Main Thread (it will open the Client and Server)          */
        Mqtt_start();
        enableLogData();

        /* Wait for Init to be completed!!!                                   */
        while (gInitState != 0)
        {
            UART_PRINT(".");
            sleep(1);
        }
        UART_PRINT(".\r\n");

        while (gWaitForExternalRestart == 0)
            ;

        UART_PRINT("TO Complete - Closing all threads and resources\r\n");

        /* Stop the MQTT Process                                              */
        Mqtt_Stop();
        disableLogData();

        UART_PRINT("reopen MQTT # %d  \r\n", ++count);

    }
}

//*****************************************************************************
//
// Close the Doxygen group.
//! @}
//
//*****************************************************************************
