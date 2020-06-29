/*
 * Copyright (C) 2015-2018 Alibaba Group Holding Limited
 */

#include "aos/kernel.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "linkkit/infra/infra_types.h"
#include "linkkit/infra/infra_defs.h"
#include "linkkit/infra/infra_compat.h"
#include "linkkit/dev_model_api.h"
#include "linkkit/infra/infra_config.h"
#include "linkkit/wrappers/wrappers.h"

#ifdef INFRA_MEM_STATS
    #include "linkkit/infra/infra_mem_stats.h"
#endif

#include "cJSON.h"
#ifdef ATM_ENABLED
    #include "at_api.h"
#endif
#include "app_entry.h"

// for demo only
// #define PRODUCT_KEY      "a1FxISeKbq9"
// #define PRODUCT_SECRET   "ThNbP5iNUQ1lQe2Q"
// #define DEVICE_NAME      "alen-activate-test"
// #define DEVICE_SECRET    "jcumDL5AJRgU7zRNcCcnHRiQmtii0vDn"
// 廖锦堂 智能卫浴TEST1
#define PRODUCT_KEY      "a1UW1xMD00k"
#define PRODUCT_SECRET   "MX4gqOyrVhP5D1MH"
#define DEVICE_NAME      "jintang01"
#define DEVICE_SECRET    "tuGvJDh5ehjoORlv9DzVTPOtmlH58bVt"


#define EXAMPLE_TRACE(...)                                          \
    do {                                                            \
        HAL_Printf("\033[1;32;40:%s.%d: ", __func__, __LINE__);     \
        HAL_Printf(__VA_ARGS__);                                    \
        HAL_Printf("]\r\n");                                  \
    } while (0)

#define EXAMPLE_MASTER_DEVID            (0)
#define EXAMPLE_YIELD_TIMEOUT_MS        (200)

typedef struct {
    int master_devid;
    int cloud_connected;
    int master_initialized;
} user_example_ctx_t;

/**
 * These PRODUCT_KEY|PRODUCT_SECRET|DEVICE_NAME|DEVICE_SECRET are listed for demo only
 *
 * When you created your own devices on iot.console.com, you SHOULD replace them with what you got from console
 *
 */

static user_example_ctx_t g_user_example_ctx;
void Serial_write(char* p,int cnt)
{
    char i =0;
    extern void uart_tx_one_char(char uart, char TxChar);
    for(i=0;i<cnt;i++)
    {
        uart_tx_one_char(0,*(p+i));
    }
}
uint8_t tub_faucet = 0;//浴缸龙头开关
uint8_t tub_shower = 0;//浴缸花洒开关
uint8_t tub_drain = 0;//浴缸排水开关
uint16_t tub_temp = 15;//浴缸水温
uint8_t tub_lock = 0;//浴缸童锁开关
uint8_t command_resend_remain = 0;//命令重发剩余次数

#include "aos/hal/uart.h"
uint32_t Serial_read(char* inbuf,int cnt)
{
    uart_dev_t uart_stdio;

    int32_t  ret       = 0;
    uint32_t recv_size = 0;

    memset(&uart_stdio, 0, sizeof(uart_dev_t));
    uart_stdio.port = 0;

    ret = hal_uart_recv_II(&uart_stdio, inbuf, cnt, &recv_size, 200);

    if ((ret == 0) && (recv_size !=0)) {
        return recv_size;
    } else {
        return 0;
    }
}
char * serial_data_of_bt_rf_command_excute( char function_code, char value_code)//生成AXENT COM32字节数据
{
    static char data_of_bt_rf[32]; 
  char custom_code = 0X30;
    data_of_bt_rf[0] = 0X02;
    data_of_bt_rf[1] = 0X0A;
    data_of_bt_rf[2] = custom_code;
    data_of_bt_rf[3] = function_code;
    data_of_bt_rf[4] = value_code;
    data_of_bt_rf[5] = function_code+value_code;
    data_of_bt_rf[6] = 0X00;
    data_of_bt_rf[7] = 0X00;
    data_of_bt_rf[8] = 0X00;
    data_of_bt_rf[9] = 0X00;
    data_of_bt_rf[10] = 0X00;
    data_of_bt_rf[11] = 0X00;
    data_of_bt_rf[12] = 0X00;
    data_of_bt_rf[13] = 0X00;
    data_of_bt_rf[14] = 0X00;
    data_of_bt_rf[15] = 0X00;
    data_of_bt_rf[16] = 0X00;
    data_of_bt_rf[17] = 0X00;
    data_of_bt_rf[18] = 0X00;
    data_of_bt_rf[19] = 0X00;
    data_of_bt_rf[20] = 0X00;
    data_of_bt_rf[21] = 0X00;
    data_of_bt_rf[22] = 0X00;
    data_of_bt_rf[23] = 0X00;
    data_of_bt_rf[24] = 0X00;
    data_of_bt_rf[25] = 0X00;
    data_of_bt_rf[26] = 0X00;
    data_of_bt_rf[27] = 0x49;//标记符 IOT
    data_of_bt_rf[28] = 0x4F;//标记符 IOT
    data_of_bt_rf[29] = 0x54;//标记符 IOT
    unsigned char  i ;
  for(i=2;i<29;i++)
    {
        data_of_bt_rf[29] ^= data_of_bt_rf[i];
    }
    data_of_bt_rf[30] = 0X0B;
    data_of_bt_rf[31] = 0X04;
    return data_of_bt_rf;
}  

/** cloud connected event callback */
static int user_connected_event_handler(void)
{
    EXAMPLE_TRACE("Cloud Connected");
    g_user_example_ctx.cloud_connected = 1;

    return 0;
}

/** cloud disconnected event callback */
static int user_disconnected_event_handler(void)
{
    EXAMPLE_TRACE("Cloud Disconnected");
    g_user_example_ctx.cloud_connected = 0;

    return 0;
}

/* device initialized event callback */
static int user_initialized(const int devid)
{
    EXAMPLE_TRACE("Device Initialized");
    g_user_example_ctx.master_initialized = 1;

    return 0;
}

/** recv property post response message from cloud **/
static int user_report_reply_event_handler(const int devid, const int msgid, const int code, const char *reply,
        const int reply_len)
{
    // EXAMPLE_TRACE("Message Post Reply Received, Message ID: %d, Code: %d, Reply: %.*s", msgid, code,
    //               reply_len,
    //               (reply == NULL) ? ("NULL") : (reply));
    return 0;
}

/** recv event post response message from cloud **/
static int user_trigger_event_reply_event_handler(const int devid, const int msgid, const int code, const char *eventid,
        const int eventid_len, const char *message, const int message_len)
{
    // EXAMPLE_TRACE("Trigger Event Reply Received, Message ID: %d, Code: %d, EventID: %.*s, Message: %.*s",
    //               msgid, code,
    //               eventid_len,
    //               eventid, message_len, message);

    return 0;
}

/** recv event post response message from cloud **/
static int user_property_set_event_handler(const int devid, const char *request, const int request_len)
{
    int res = 0;
    // EXAMPLE_TRACE("Property Set Received, Request: %s", request);
    char i =0,temp=0;
    char *p;
    res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_POST_PROPERTY,
                             (unsigned char *)request, request_len);
    // EXAMPLE_TRACE("Post Property Message ID: %d", res);
    const char * payload = request;
    if(strstr((char *)payload,"Stop\":")) //停止命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0,0),32);
    }
    else if(strstr((char *)payload,"RearWash\":")) //臀洗命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0x61,0x66),32);
    }
    else if(strstr((char *)payload,"LadyWash\":")) //妇洗命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0x62,0x66),32);
    }    
    else if(strstr((char *)payload,"Dry\":")) //烘干命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0x04,0x66),32);
    }    
    else if(strstr((char *)payload,"Nozzle\":")) //喷管位置命令
    {  
        p = strstr((char *)payload,"Nozzle\":");
        temp = *(p+8)-0x30 - 1;
        Serial_write(serial_data_of_bt_rf_command_excute(0X36,temp),32);
    }
    else if(strstr((char *)payload,"\"WaterTemp\":")) //水温命令
    {  
        p = strstr((char *)payload,"WaterTemp\":");
        temp = *(p+11)-0x30;
        Serial_write(serial_data_of_bt_rf_command_excute(0X16,temp),32);    
    }
    else if(strstr((char *)payload,"WaterFlow\":")) //流量调节命令
    {  
        p = strstr((char *)payload,"WaterFlow\":");
        temp = *(p+11)-0x30 - 1;
        Serial_write(serial_data_of_bt_rf_command_excute(0X46,temp),32);    
    }
    else if(strstr((char *)payload,"DryTemp\":")) //风温命令
    {  
        p = strstr((char *)payload,"DryTemp\":");
        temp = *(p+9)-0x30;
        Serial_write(serial_data_of_bt_rf_command_excute(0X06,temp),32);    
    }
    else if(strstr((char *)payload,"SeatTemp\":")) //座温命令
    {  
        p = strstr((char *)payload,"SeatTemp\":");
        temp = *(p+10)-0x30;
        Serial_write(serial_data_of_bt_rf_command_excute(0X26,temp),32);    
    }      
    else if(strstr((char *)payload,"LidSeatClose\":")) //上盖座圈关闭命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0x07,0x00),32);
    }    
    else if(strstr((char *)payload,"LidSeatOpen\":")) //上盖座圈开启命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0x07,0x02),32);
    }
    else if(strstr((char *)payload,"LidOpenSeatClose\":")) //上盖开座圈关命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0x07,0x01),32);
    }
    else if(strstr((char *)payload,"AutoFlush\":")) //冲刷命令
    {          
        p = strstr((char *)payload,"AutoFlush\":");
        temp = *(p+11)-0x30;
        Serial_write(serial_data_of_bt_rf_command_excute(0X29,temp),32);    
    }
    else if(strstr((char *)payload,"MassageMove\":")) //移动按摩命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0X15,0x01),32);
    }
    else if(strstr((char *)payload,"MassagePulse\":")) //强弱按摩命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0X15,0x02),32);
    }
    else if(strstr((char *)payload,"MassageCycle\":")) //循环按摩命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0X15,0x08),32);
    }
    else if(strstr((char *)payload,"DisplayMode\":")) //展厅模式命令
    {  
        p = strstr((char *)payload,"DisplayMode\":");
        temp = *(p+13)-0x30;
        Serial_write(serial_data_of_bt_rf_command_excute(0X3B,temp),32);       
    }
    else if(strstr((char *)payload,"NightLamp\":")) //夜灯设置命令
    {  
        p = strstr((char *)payload,"NightLamp\":");
        temp = *(p+11)-0x30;
        Serial_write(serial_data_of_bt_rf_command_excute(0X5A,temp),32);       
    }
    else if(strstr((char *)payload,"CoversAutoOpen\":")) //自动翻盖设置命令
    {  
        p = strstr((char *)payload,"CoversAutoOpen\":");
        temp = *(p+16)-0x30;
        Serial_write(serial_data_of_bt_rf_command_excute(0X77,temp),32);       
    }
    else if(strstr((char *)payload,"AutoFlush\":")) //自动冲刷设置命令
    {  
        p = strstr((char *)payload,"AutoFlush\":");
        temp = *(p+11)-0x30;
        Serial_write(serial_data_of_bt_rf_command_excute(0X29,temp),32);       
    }
    else if(strstr((char *)payload,"AutoDeo\":")) //自动除臭设置命令
    {  
        p = strstr((char *)payload,"AutoDeo\":");
        temp = *(p+9)-0x30;
        Serial_write(serial_data_of_bt_rf_command_excute(0X4A,temp),32);       
    }
    //一下为浴缸等其他数据处理
    else if(strstr((char *)payload,"Faucet\":")) //浴缸龙头开关命令
    {  
        p = strstr((char *)payload,"Faucet\":");
        tub_faucet = *(p+8)-0x30;
        command_resend_remain = 2;//重发设置
    }
    else if(strstr((char *)payload,"Shower\":")) //浴缸花洒开关命令
    {  
        p = strstr((char *)payload,"Shower\":");
        tub_shower = *(p+8)-0x30;
        command_resend_remain = 2;//重发设置
    }
    else if(strstr((char *)payload,"Drain\":")) //浴缸排水开关命令
    {  
        p = strstr((char *)payload,"Drain\":");
        tub_drain = *(p+7)-0x30;
        command_resend_remain = 2;//重发设置
    }
    else if(strstr((char *)payload,"TubWaterTemp\":")) //浴缸水温命令
    {  
        p = strstr((char *)payload,"TubWaterTemp\":");
        tub_temp = ((*(p+14)-0x30)*10) + (*(p+15)-0x30);
        command_resend_remain = 2;//重发设置
    }    
    else if(strstr((char *)payload,"TubLock\":")) //浴缸水温命令
    {  
        p = strstr((char *)payload,"TubLock\":");
        tub_lock = *(p+9)-0x30;
        command_resend_remain = 2;//重发设置
    }
    
    else//不能处理的云端数据直接打印出来
    {
        // EXAMPLE_TRACE("Property Set Received, Request: %s", payload);
        printf("Property Set Received: %s\r\n", payload);

    }


    return 0;
}


static int user_service_request_event_handler(const int devid, const char *serviceid, const int serviceid_len,
        const char *request, const int request_len,
        char **response, int *response_len)
{
    int add_result = 0;
    int ret = -1;
    cJSON *root = NULL, *item_number_a = NULL, *item_number_b = NULL;
    const char *response_fmt = "{\"Result\": %d}";
    // EXAMPLE_TRACE("Service Request Received, Service ID: %s, Payload: %s", serviceid, request);

    // /* Parse Root */
    // root = cJSON_Parse(request);
    // if (root == NULL || !cJSON_IsObject(root)) {
    //     EXAMPLE_TRACE("JSON Parse Error");
    //     return -1;
    // }
    // do{
    //     if (strlen("Operation_Service") == serviceid_len && memcmp("Operation_Service", serviceid, serviceid_len) == 0) {
    //         /* Parse NumberA */
    //         item_number_a = cJSON_GetObjectItem(root, "NumberA");
    //         if (item_number_a == NULL || !cJSON_IsNumber(item_number_a)) {
    //             break;
    //         }
    //         EXAMPLE_TRACE("NumberA = %d", item_number_a->valueint);

    //         /* Parse NumberB */
    //         item_number_b = cJSON_GetObjectItem(root, "NumberB");
    //         if (item_number_b == NULL || !cJSON_IsNumber(item_number_b)) {
    //             break;
    //         }
    //         EXAMPLE_TRACE("NumberB = %d", item_number_b->valueint);
    //         add_result = item_number_a->valueint + item_number_b->valueint;
    //         ret = 0;
    //         /* Send Service Response To Cloud */
    //     }
    // }while(0);

    // *response_len = strlen(response_fmt) + 10 + 1;
    // *response = (char *)HAL_Malloc(*response_len);
    // if (*response != NULL) {
    //     memset(*response, 0, *response_len);
    //     HAL_Snprintf(*response, *response_len, response_fmt, add_result);
    //     *response_len = strlen(*response);
    // }

    // cJSON_Delete(root);
    // return ret;
    ret = 0;
    if(strstr((char *)serviceid,"LidAndSeatClose")) //全关命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0x07,0x00),32);
    }    
    else if(strstr((char *)serviceid,"LidOpenAndSeatClose")) //半开命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0x07,0x01),32);
    }
    else if(strstr((char *)serviceid,"LidAndSeatOpen")) //全开命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0x07,0x02),32);
    }    
    else if(strstr((char *)serviceid,"StopService")) //停止命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0,0),32);
    }
    else if(strstr((char *)serviceid,"RearWashService")) //臀洗命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0x31,0x33),32);
    }
    else if(strstr((char *)serviceid,"LadyWashService")) //妇洗命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0x32,0x33),32);
    }    
    else if(strstr((char *)serviceid,"DryService")) //烘干命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0x04,0x33),32);
    }      
    else if(strstr((char *)serviceid,"FlushService")) //冲刷命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0X09,0x01),32);
    }
    else if(strstr((char *)serviceid,"MassageCycleService")) //循环按摩命令
    {  
        Serial_write(serial_data_of_bt_rf_command_excute(0X15,0x08),32);
    }
    else
    {
        ret = -1;
        // EXAMPLE_TRACE("Service Request Received, Service ID: %s, Payload: %s", serviceid, request);
        printf("Service Received, SID: %s,Request: %s\r\n", serviceid, request);
    }
    

}

static int user_timestamp_reply_event_handler(const char *timestamp)
{
    EXAMPLE_TRACE("Current Timestamp: %s", timestamp);

    return 0;
}

/** fota event handler **/
static int user_fota_event_handler(int type, const char *version)
{
    char buffer[128] = {0};
    int buffer_length = 128;

    /* 0 - new firmware exist, query the new firmware */
    if (type == 0) {
        EXAMPLE_TRACE("New Firmware Version: %s", version);

        IOT_Linkkit_Query(EXAMPLE_MASTER_DEVID, ITM_MSG_QUERY_FOTA_DATA, (unsigned char *)buffer, buffer_length);
    }

    return 0;
}

/* cota event handler */
static int user_cota_event_handler(int type, const char *config_id, int config_size, const char *get_type,
                                   const char *sign, const char *sign_method, const char *url)
{
    char buffer[128] = {0};
    int buffer_length = 128;

    /* type = 0, new config exist, query the new config */
    if (type == 0) {
        EXAMPLE_TRACE("New Config ID: %s", config_id);
        EXAMPLE_TRACE("New Config Size: %d", config_size);
        EXAMPLE_TRACE("New Config Type: %s", get_type);
        EXAMPLE_TRACE("New Config Sign: %s", sign);
        EXAMPLE_TRACE("New Config Sign Method: %s", sign_method);
        EXAMPLE_TRACE("New Config URL: %s", url);

        IOT_Linkkit_Query(EXAMPLE_MASTER_DEVID, ITM_MSG_QUERY_COTA_DATA, (unsigned char *)buffer, buffer_length);
    }

    return 0;
}

void user_post_property(void)
{
    static int cnt = 0;
    int res = 0;

    char property_payload[30] = {0};
    HAL_Snprintf(property_payload, sizeof(property_payload), "{\"Counter\": %d}", cnt++);

    // res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_POST_PROPERTY,
    //                          (unsigned char *)property_payload, strlen(property_payload));

    // EXAMPLE_TRACE("Post Property Message ID: %d", res);
}

void user_post_event(void)
{
    int res = 0;
    char *event_id = "HardwareError";
    char *event_payload = "{\"ErrorCode\": 0}";

    // res = IOT_Linkkit_TriggerEvent(EXAMPLE_MASTER_DEVID, event_id, strlen(event_id),
    //                                event_payload, strlen(event_payload));
    // EXAMPLE_TRACE("Post Event Message ID: %d", res);
}

void user_deviceinfo_update(void)
{
    int res = 0;
    char *device_info_update = "[{\"attrKey\":\"abc\",\"attrValue\":\"hello,world\"}]";

    res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_DEVICEINFO_UPDATE,
                             (unsigned char *)device_info_update, strlen(device_info_update));
    EXAMPLE_TRACE("Device Info Update Message ID: %d", res);
}

void user_deviceinfo_delete(void)
{
    int res = 0;
    char *device_info_delete = "[{\"attrKey\":\"abc\"}]";

    res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_DEVICEINFO_DELETE,
                             (unsigned char *)device_info_delete, strlen(device_info_delete));
    EXAMPLE_TRACE("Device Info Delete Message ID: %d", res);
}

static int user_cloud_error_handler(const int code, const char *data, const char *detail)
{
    EXAMPLE_TRACE("code =%d ,data=%s, detail=%s", code, data, detail);
    return 0;
}

void set_iotx_info()
{
    char _device_name[IOTX_DEVICE_NAME_LEN + 1] = {0};
    HAL_GetDeviceName(_device_name);
    if (strlen(_device_name) == 0) {
        HAL_SetProductKey(PRODUCT_KEY);
        HAL_SetProductSecret(PRODUCT_SECRET);
        HAL_SetDeviceName(DEVICE_NAME);
        HAL_SetDeviceSecret(DEVICE_SECRET);
    }
}

int linkkit_main(void *paras)
{
    int res = 0;
    int cnt = 0;
    int auto_quit = 0;
    iotx_linkkit_dev_meta_info_t master_meta_info;
    int domain_type = 0, dynamic_register = 0, post_reply_need = 0, fota_timeout = 30;
    int   argc = 0;
    char  **argv = NULL;
    printf("\r\n------jintang linkkit_main\r\n");

    if (paras != NULL) {
        argc = ((app_main_paras_t *)paras)->argc;
        argv = ((app_main_paras_t *)paras)->argv;
    }
#ifdef ATM_ENABLED
    if (IOT_ATM_Init() < 0) {
        EXAMPLE_TRACE("IOT ATM init failed!\n");
        return -1;
    }
#endif


    if (argc >= 2 && !strcmp("auto_quit", argv[1])) {
        auto_quit = 1;
        cnt = 0;
    }
    memset(&g_user_example_ctx, 0, sizeof(user_example_ctx_t));

    memset(&master_meta_info, 0, sizeof(iotx_linkkit_dev_meta_info_t));
    printf("\r\n------jintang HAL_GetProductKey(master_meta_info.product_key);\r\n");
    HAL_GetProductKey(master_meta_info.product_key);
    HAL_GetDeviceName(master_meta_info.device_name);
    HAL_GetProductSecret(master_meta_info.product_secret);
    HAL_GetDeviceSecret(master_meta_info.device_secret);

    IOT_SetLogLevel(IOT_LOG_INFO);
    IOT_SetLogLevel(IOT_LOG_WARNING);// change by jintang


    /* Register Callback */
    IOT_RegisterCallback(ITE_CONNECT_SUCC, user_connected_event_handler);
    IOT_RegisterCallback(ITE_DISCONNECTED, user_disconnected_event_handler);
    IOT_RegisterCallback(ITE_SERVICE_REQUEST, user_service_request_event_handler);
    IOT_RegisterCallback(ITE_PROPERTY_SET, user_property_set_event_handler);
    IOT_RegisterCallback(ITE_REPORT_REPLY, user_report_reply_event_handler);
    IOT_RegisterCallback(ITE_TRIGGER_EVENT_REPLY, user_trigger_event_reply_event_handler);
    IOT_RegisterCallback(ITE_TIMESTAMP_REPLY, user_timestamp_reply_event_handler);
    IOT_RegisterCallback(ITE_INITIALIZE_COMPLETED, user_initialized);
    IOT_RegisterCallback(ITE_FOTA, user_fota_event_handler);
    IOT_RegisterCallback(ITE_COTA, user_cota_event_handler);
    IOT_RegisterCallback(ITE_CLOUD_ERROR, user_cloud_error_handler);


    domain_type = IOTX_CLOUD_REGION_SHANGHAI;
    IOT_Ioctl(IOTX_IOCTL_SET_DOMAIN, (void *)&domain_type);

    /* Choose Login Method */
    dynamic_register = 0;
    IOT_Ioctl(IOTX_IOCTL_SET_DYNAMIC_REGISTER, (void *)&dynamic_register);

    /* post reply doesn't need */
    post_reply_need = 1;
    IOT_Ioctl(IOTX_IOCTL_RECV_EVENT_REPLY, (void *)&post_reply_need);

    IOT_Ioctl(IOTX_IOCTL_FOTA_TIMEOUT_MS, (void *)&fota_timeout);



    
#if defined(USE_ITLS)
    {
        char url[128] = {0};
        int port = 1883;
        snprintf(url, 128, "%s.itls.cn-shanghai.aliyuncs.com",master_meta_info.product_key);
        IOT_Ioctl(IOTX_IOCTL_SET_MQTT_DOMAIN, (void *)url);
        IOT_Ioctl(IOTX_IOCTL_SET_CUSTOMIZE_INFO, (void *)"authtype=id2");
        IOT_Ioctl(IOTX_IOCTL_SET_MQTT_PORT, &port);
    } 
#endif
    /* Create Master Device Resources */
    do {
        g_user_example_ctx.master_devid = IOT_Linkkit_Open(IOTX_LINKKIT_DEV_TYPE_MASTER, &master_meta_info);
        if (g_user_example_ctx.master_devid >= 0) {
            break;
        }
        EXAMPLE_TRACE("IOT_Linkkit_Open failed! retry after %d ms\n", 2000);
        HAL_SleepMs(2000);
    } while (1);
    /* Start Connect Aliyun Server */
    do {
        printf("\r\n------jintang IOT_Linkkit_Connect(g_user_example_ctx.master_devid)\r\n");

        res = IOT_Linkkit_Connect(g_user_example_ctx.master_devid);
        if (res >= 0) {
            break;
        }
        EXAMPLE_TRACE("IOT_Linkkit_Connect failed! retry after %d ms\n", 5000);
        HAL_SleepMs(5000);
    } while (1);
    printf("------jintang IOT_Linkkit_Connect() done %d\r\n",500);
    printf("------jintang cli_task_cancel\r\n");
    cli_task_cancel();//jintang modify for malta 云浴缸
    while (1) {
        static uint8_t temp[32];
        uint8_t recv_cnt = 0;
        int res = 0;
        char property_payload[30] = {0};
        // IOT_Linkkit_Yield(EXAMPLE_YIELD_TIMEOUT_MS);        
        IOT_Linkkit_Yield(10);
        recv_cnt=Serial_read(temp,32);//jintang modify for malta 云浴缸
        if(recv_cnt!=0)
        {
            if((temp[0] == 0x02)&&(temp[1] == 0x3a)&&(temp[2] == 0x03)) //浴缸32字节命令
            {
                 if((temp[3] == 0xff)&&(temp[4] == 0x01))
                {
                    printf("do_awss_reset()");//---jintang modify
                    extern  void do_awss_reset();
                    do_awss_reset();
                }
                else if((temp[3] == 0x00))//查询命令
                {
                    if(tub_faucet < 5)//龙头新命令
                    {
                        temp[3] = 0x01;
                        temp[4] = tub_faucet;
                        if(command_resend_remain!=0)command_resend_remain--;//重发
                        else
                        tub_faucet += 100;//＋100表示命令已经处理了
                    }
                    else if(tub_shower < 5)//花洒新命令
                    {
                        temp[3] = 0x01;
                        temp[4] = tub_shower ?  2 : 0;
                        if(command_resend_remain!=0)command_resend_remain--;//重发
                        else
                        tub_shower += 100;//＋100表示命令已经处理了
                    }
                    else if(tub_drain < 5)//排水新命令
                    {
                        temp[3] = 0x02;
                        temp[4] = tub_drain;
                        if(command_resend_remain!=0)command_resend_remain--;//重发
                        else
                        tub_drain += 100;//＋100表示命令已经处理了
                    }
                    else if(tub_temp < 80)//温度设置新命令
                    {
                        temp[3] = 0x04;
                        temp[4] = 00;
                        temp[5] = (tub_temp*10)>>8;
                        temp[6] = (tub_temp*10);
                        if(command_resend_remain!=0)command_resend_remain--;//重发
                        else
                        tub_drain += 100;//＋100表示命令已经处理了
                    }
                    else if(tub_lock < 5)//童锁设置新命令
                    {
                        temp[3] = 0x09;
                        temp[4] = tub_lock;
                        if(command_resend_remain!=0)command_resend_remain--;//重发
                        else
                        tub_lock += 100;//＋100表示命令已经处理了
                    }
                    else
                    {
                        if((temp[12] & 0x01) != (tub_faucet -100))//本地出水发生变化
                        {
                            tub_faucet = (temp[12] & 0x01)? 101:100;
                            HAL_Snprintf(property_payload, sizeof(property_payload), "{\"Faucet\":%d}",(temp[12] & 0x01)? 1:0);
                            res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_POST_PROPERTY,
                                (unsigned char *)property_payload, strlen(property_payload));
                        }
                        if((temp[12] & 0x02) != (tub_shower -100))//本地出水发生变化
                        {
                            tub_shower = (temp[12] & 0x02)? 101:100;
                            HAL_Snprintf(property_payload, sizeof(property_payload), "{\"Shower\":%d}",(temp[12] & 0x02)? 1:0);
                            res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_POST_PROPERTY,
                                (unsigned char *)property_payload, strlen(property_payload));
                        }
                        if((temp[15] & 0x01) != (tub_drain -100))//本地排水发生变化
                        {                        
                            tub_drain = (temp[15] & 0x01)? 101:100;
                            HAL_Snprintf(property_payload, sizeof(property_payload), "{\"Drain\":%d}",(temp[15] & 0x01)? 1:0);
                            res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_POST_PROPERTY,
                                (unsigned char *)property_payload, strlen(property_payload));
                        }
                        if(((temp[5] << 8) + temp[6])!= tub_temp)//本地温度发生变化
                        {
                            tub_temp = ((temp[5] << 8) + temp[6]);
                            HAL_Snprintf(property_payload, sizeof(property_payload), "{\"TubWaterTemp\": %d}", tub_temp/10);
                            res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_POST_PROPERTY,
                                                (unsigned char *)property_payload, strlen(property_payload));
                        }
                        if((temp[17] & 0x01) != (tub_lock -100))//本地童锁发生变化
                        {                        
                            tub_lock = (temp[17] & 0x01)? 101:100;
                            HAL_Snprintf(property_payload, sizeof(property_payload), "{\"TubLock\":%d}",(temp[17] & 0x01)? 1:0);
                            res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_POST_PROPERTY,
                                                    (unsigned char *)property_payload, strlen(property_payload));
                        }
                    }
                }
                
                temp[29] = 0;
                uint8_t i;
                for(i=2;i<29;i++)
                {
                    temp[29] ^= temp[i];
                }
                temp[1] = 0xa3;
                temp[30] = 0x0f;
                temp[31] = 0x04;
                Serial_write(temp,32);      
            }
        }

        /* Post Proprety Example */

        if ((cnt % 20) == 0) {
            user_post_property();
        }

        /* Post Event Example */
        if ((cnt % 50) == 0) {
            user_post_event();
        }
        cnt++;
        if (auto_quit == 1 && cnt > 3600) {
            break;
        }
    }
    printf("\r\n------jintang IOT_Linkkit_Close(g_user_example_ctx.master_devid);\r\n");
    IOT_Linkkit_Close(g_user_example_ctx.master_devid);

    IOT_DumpMemoryStats(IOT_LOG_DEBUG);

    return 0;
}
