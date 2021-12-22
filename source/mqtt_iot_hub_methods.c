/******************************************************************************
* File Name: mqtt_iot_hub_methods.c
*
* Description: This file contains tasks and functions related to Azure Methods
* feature task.
*
********************************************************************************
* Copyright 2021, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

#include "azure_common.h"

#include <stdio.h>
#include "cyhal.h"
#include "cybsp.h"
#include "cy_log.h"
#include <lwip/tcpip.h>
#include <lwip/api.h>
#include <cy_retarget_io.h>
#include "cy_lwip.h"
#include <cybsp_wifi.h>
#include "cy_nw_helper.h"
#include "cy_wcm.h"
#include "cyabs_rtos.h"

#include <FreeRTOS.h>
#include <task.h>

#include "mqtt_main.h"
#include "cy_mqtt_api.h"
#include <az_core.h>
#include <az_iot.h>
#include "mqtt_iot_common.h"

#ifdef CY_TFM_PSA_SUPPORTED
#include "cy_wdt.h"
#include "tfm_multi_core_api.h"
#include "tfm_ns_interface.h"
#include "tfm_ns_mailbox.h"
#include "psa/protected_storage.h"
#endif

/*******************************************************************************
* Macros
********************************************************************************/
/* Wait duration between messages, should be in multiples of 500 */
#define MESSAGE_WAIT_LOOP_DURATION_MSEC             (120 * 1000)

#define METHODS_RESPONSE_TOPIC_BUFFER_SIZE          (128)

#define SAS_KEY_DURATION_MINUTES                    (240)

#define MQTT_CLIENT_ID_BUFFER_SIZE                  (128)

#define GET_QUEUE_TIMEOUT_MSEC                      (500)
#define PUT_QUEUE_TIMEOUT_MSEC                      (500)

/***********************************************************
* Constants
************************************************************/
static az_span const method_ping_name = AZ_SPAN_LITERAL_FROM_STR("ping");
static az_span const method_ping_response = AZ_SPAN_LITERAL_FROM_STR("{\"response\": \"pong\"}");
static az_span const method_empty_response_payload = AZ_SPAN_LITERAL_FROM_STR("{}");

/***********************************************************
* Static Variables
************************************************************/
static cy_mqtt_t                           mqtthandle;
static iot_sample_environment_variables    env_vars;
static az_iot_hub_client                   hub_client;
static char                                mqtt_client_username_buffer[IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES];
static char                                mqtt_endpoint_buffer[IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES];
static cy_queue_t                          hub_direct_method_event_queue = NULL;
static volatile bool                       connect_state = false;

#if SAS_TOKEN_AUTH
static iot_sample_credentials              sas_credentials;
static char                                device_id_buffer[IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES];
static char                                sas_token_buffer[IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES];
#endif

/* The network buffer must remain valid for the lifetime of the MQTT context. */
static uint8_t                             *buffer = NULL;

static cy_rslt_t send_method_response( az_iot_hub_client_method_request const* method_request,
                                       az_iot_status status, az_span response )
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    int rc;
    size_t topic_len;
    cy_mqtt_publish_info_t pub_msg;

    /* Get the methods response topic to publish the method response */
    char methods_response_topic_buffer[METHODS_RESPONSE_TOPIC_BUFFER_SIZE];

    memset( &methods_response_topic_buffer, 0x00, sizeof(methods_response_topic_buffer) );
    memset( &pub_msg, 0x00, sizeof( cy_mqtt_publish_info_t ) );

    rc = az_iot_hub_client_methods_response_get_publish_topic( &hub_client, method_request->request_id,
                                                               (uint16_t)status, methods_response_topic_buffer,
                                                               sizeof(methods_response_topic_buffer),
                                                               (size_t *)&topic_len );
    if( az_result_failed(rc) )
    {
        TEST_INFO(( "\r\nFailed to get the Methods Response topic: az_result return code 0x%08x.", rc ));
        return ( (cy_rslt_t)TEST_FAIL );
    }

    pub_msg.qos = (cy_mqtt_qos_t)CY_MQTT_QOS0;
    pub_msg.topic = (const char *)&methods_response_topic_buffer;
    pub_msg.topic_len = topic_len;
    pub_msg.payload = (const char *)response._internal.ptr;
    pub_msg.payload_len = (size_t)response._internal.size;

    result = cy_mqtt_publish( mqtthandle, &pub_msg );
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\r\ncy_mqtt_publish completed........\n\r" ));
    }
    else
    {
        TEST_INFO(( "\r\ncy_mqtt_publish failed with Error : [0x%X] ", (unsigned int)result ));
        return result;
    }
    TEST_INFO(( "\r\nClient published the Methods response.\r\n" ));
    TEST_INFO(( "\r\nStatus: %u\r\n", (uint16_t)status ));
    TEST_INFO(( "\r\nPayload: %.*s\r\n", (int)response._internal.size,  response._internal.ptr ));
    return result;
}

static az_span invoke_ping(void)
{
    TEST_INFO(( "\r\nPING.......!\r\n" ));
    return method_ping_response;
}

static void handle_method_request( az_iot_hub_client_method_request const* method_request )
{
    if( az_span_is_content_equal( method_ping_name, method_request->name ) )
    {
        /* Invoke method */
        az_span response = invoke_ping();
        TEST_INFO(( "\r\nClient invoked method 'ping'.\r\n" ));
        send_method_response( method_request, AZ_IOT_STATUS_OK, response );
    }
    else
    {
        TEST_INFO(( "\r\nMethod not supported :  %.*s\r\n", (int)(method_request->name._internal.size), (method_request->name._internal.ptr) ));
        send_method_response( method_request, AZ_IOT_STATUS_NOT_FOUND, method_empty_response_payload );
    }
}

static cy_rslt_t parse_hub_method_message( char* topic, int topic_len, cy_mqtt_publish_info_t *message,
                                           az_iot_hub_client_method_request* out_method_request)
{
    az_span const topic_span = az_span_create((uint8_t*)topic, topic_len);
    az_span const message_span = az_span_create((uint8_t*)message->payload, message->payload_len);

    /* Parse the message and retrieve the method_request info */
    az_result rc = az_iot_hub_client_methods_parse_received_topic( &hub_client, topic_span, out_method_request );
    if( az_result_failed(rc) )
    {
        TEST_INFO(( "\r\nMessage from unknown topic: az_result return code 0x%08x.", (unsigned int)rc ));
        TEST_INFO(( "\r\nTopic : %.*s\r\n", (int)topic_span._internal.size, topic_span._internal.ptr ));
        return ( (cy_rslt_t)TEST_FAIL );
    }
    TEST_INFO(( "\r\nClient received a valid topic response." ));
    TEST_INFO(( "\r\nTopic : %.*s\r\n", (int)topic_span._internal.size, topic_span._internal.ptr ));
    TEST_INFO(( "\r\nPayload: %.*s\r\n", (int)message_span._internal.size,  message_span._internal.ptr ));
    return CY_RSLT_SUCCESS;
}

static void mqtt_event_cb( cy_mqtt_t mqtt_handle, cy_mqtt_event_t event, void *arg )
{
    cy_mqtt_publish_info_t *received_msg;
    cy_rslt_t result = CY_RSLT_SUCCESS;

    TEST_INFO(( "\r\nMQTT App callback with handle : %p \r\n", mqtt_handle ));

    switch( event.type )
    {
        case CY_MQTT_EVENT_TYPE_DISCONNECT :
            if( event.data.reason == CY_MQTT_DISCONN_TYPE_BROKER_DOWN )
            {
                TEST_INFO(( "\r\nCY_MQTT_DISCONN_TYPE_BROKER_DOWN .....\r\n" ));
            }
            else
            {
                TEST_INFO(( "\r\nCY_MQTT_DISCONN_REASON_NETWORK_DISCONNECTION .....\r\n" ));
            }
            connect_state = false;
            break;

        case CY_MQTT_EVENT_TYPE_PUBLISH_RECEIVE :
            received_msg = &(event.data.pub_msg.received_message);
            /* Parse the method message and invoke the method */
            az_iot_hub_client_method_request method_request;
            parse_hub_method_message( (char*)received_msg->topic, received_msg->topic_len, received_msg, &method_request );
            TEST_INFO(( "\n\rClient parsed method request." ));
            TEST_INFO(( "\n\rPushing to direct method queue...." ));
            result = cy_rtos_put_queue( &hub_direct_method_event_queue, (void *)&method_request, PUT_QUEUE_TIMEOUT_MSEC, false );
            if( result != CY_RSLT_SUCCESS )
            {
                TEST_INFO(( "\n\rPushing to hub_direct_method_event_queue failed with Error : [0x%X] ", (unsigned int)result ));
            }
            break;

        default :
            TEST_INFO(( "\r\nUnknown Event .....\r\n" ));
            break;
    }
}

static cy_rslt_t disconnect_and_delete_mqtt_client( void )
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    result = cy_mqtt_disconnect( mqtthandle );
    if( result == CY_RSLT_SUCCESS )
    {
        TEST_INFO(( "\n\rcy_mqtt_disconnect ----------------------- Pass \r\n" ));
    }
    else
    {
        TEST_INFO(( "\n\rcy_mqtt_disconnect ----------------------- Fail \r\n" ));
    }
    connect_state = false;

    result = cy_mqtt_delete( mqtthandle );
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\n\rcy_mqtt_delete --------------------------- Pass \r\n" ));
    }
    else
    {
        TEST_INFO(( "\n\rcy_mqtt_delete --------------------------- Fail \r\n" ));
    }

    result = cy_mqtt_deinit();
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\n\rcy_mqtt_deinit --------------------------- Pass \r\n" ));
    }
    else
    {
        TEST_INFO(( "\n\rcy_mqtt_deinit --------------------------- Fail \r\n" ));
    }

    /* Free the network buffer */
    if( buffer != NULL )
    {
        free( buffer );
        buffer = NULL;
    }

    return result;
}

static cy_rslt_t subscribe_mqtt_client_to_iot_hub_topics( void )
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_mqtt_subscribe_info_t sub_msg;

    sub_msg.qos = (cy_mqtt_qos_t)CY_MQTT_QOS1;
    sub_msg.topic = AZ_IOT_HUB_CLIENT_METHODS_SUBSCRIBE_TOPIC;
    sub_msg.topic_len = ( ( uint16_t ) (sizeof( AZ_IOT_HUB_CLIENT_METHODS_SUBSCRIBE_TOPIC ) -1 ) );

    result = cy_mqtt_subscribe( mqtthandle, &sub_msg, 1 );
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\n\rcy_mqtt_subscribe ------------------------ Pass \r\n" ));
    }
    else
    {
        TEST_INFO(( "\n\rcy_mqtt_subscribe ------------------------ Fail \r\n" ));
    }

  return result;
}

static cy_rslt_t connect_mqtt_client_to_iot_hub(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    int rc;
    size_t username_len = 0, client_id_len = 0;
    cy_mqtt_connect_info_t connect_info;

    /* Get the MQTT client ID used for the MQTT connection */
    char mqtt_client_id_buffer[MQTT_CLIENT_ID_BUFFER_SIZE];

    memset( &connect_info, 0x00, sizeof( cy_mqtt_connect_info_t ) );
    memset( &mqtt_client_id_buffer, 0x00, sizeof( mqtt_client_id_buffer ) );

    rc = az_iot_hub_client_get_client_id( &hub_client, mqtt_client_id_buffer, sizeof(mqtt_client_id_buffer), &client_id_len );
    if( az_result_failed(rc) )
    {
        TEST_INFO(( "\r\nFailed to get MQTT client id: az_result return code 0x%08x.", rc ));
        return TEST_FAIL;
    }

    /* Get the MQTT client user name */
    rc = az_iot_hub_client_get_user_name( &hub_client, mqtt_client_username_buffer,
                                          sizeof(mqtt_client_username_buffer), &username_len );
    if( az_result_failed(rc) )
    {
        TEST_INFO(( "\r\nFailed to get MQTT client username: az_result return code 0x%08x.", rc ));
        return TEST_FAIL;
    }

    connect_info.client_id = mqtt_client_id_buffer;
    connect_info.client_id_len = client_id_len;
    connect_info.keep_alive_sec = MQTT_KEEP_ALIVE_INTERVAL_SECONDS;
    connect_info.clean_session = false;
    connect_info.will_info = NULL;

#if SAS_TOKEN_AUTH
    connect_info.username = mqtt_client_username_buffer;
    connect_info.password = (char *)sas_credentials.sas_token;
    connect_info.username_len = username_len;
    connect_info.password_len = sas_credentials.sas_token_len;
#else
    connect_info.username = mqtt_client_username_buffer;
    connect_info.password = NULL;
    connect_info.username_len = username_len;
    connect_info.password_len = 0;
#endif

    result = cy_mqtt_connect( mqtthandle, &connect_info );
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\r\ncy_mqtt_connect -------------------------- Pass \r\n" ));
        connect_state = true;
    }
    else
    {
        TEST_INFO(( "\r\ncy_mqtt_connect -------------------------- Fail \r\n" ));
        return TEST_FAIL;
    }

  return CY_RSLT_SUCCESS;
}

static cy_rslt_t create_and_configure_mqtt_client( void )
{
    int32_t rc;
    uint16_t ep_size = 0;
    cy_rslt_t result = TEST_PASS;
    cy_awsport_ssl_credentials_t credentials;
    cy_awsport_ssl_credentials_t *security = NULL;
    cy_mqtt_broker_info_t broker_info;

    memset( &mqtt_endpoint_buffer, 0x00, sizeof( mqtt_endpoint_buffer ) );

    ep_size = iot_sample_create_mqtt_endpoint( CY_MQTT_IOT_HUB, &env_vars,
                                                 mqtt_endpoint_buffer,
                                                 sizeof(mqtt_endpoint_buffer) );

    /* Initialize the hub client with the default connection options */
    rc = az_iot_hub_client_init( &hub_client, env_vars.hub_hostname, env_vars.hub_device_id, NULL );
    if( az_result_failed( rc ) )
    {
        TEST_INFO(( "\r\nFailed to initialize hub client: az_result return code 0x%08x.", (unsigned int)rc ));
        return TEST_FAIL;
    }

    /* Allocate the network buffer */
    buffer = (uint8_t *) malloc( sizeof(uint8_t) * NETWORK_BUFFER_SIZE );
    if( buffer == NULL )
    {
        TEST_INFO(( "\r\nNo memory is available for network buffer..! \r\n" ));
        return TEST_FAIL;
    }

    memset( &broker_info, 0x00, sizeof( cy_mqtt_broker_info_t ) );
    memset( &credentials, 0x00, sizeof( cy_awsport_ssl_credentials_t ) );

    result = cy_mqtt_init();
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\n\rcy_mqtt_init ----------------------------- Pass \r\n" ));
    }
    else
    {
        TEST_INFO(( "\n\rcy_mqtt_init ----------------------------- Fail \r\n" ));
        return TEST_FAIL;
    }

#if SAS_TOKEN_AUTH
    credentials.client_cert = (const char *)NULL;
    credentials.client_cert_size = 0;
    credentials.private_key = (const char *)NULL;
    credentials.private_key_size = 0;
    credentials.root_ca = (const char *) NULL;
    credentials.root_ca_size = 0;
    /* For SAS token based auth mode, RootCA verification is not required. */
    credentials.root_ca_verify_mode = CY_AWS_ROOTCA_VERIFY_NONE;
    /* Set cert and key location. */
    credentials.cert_key_location = CY_AWS_CERT_KEY_LOCATION_RAM;
    credentials.root_ca_location = CY_AWS_CERT_KEY_LOCATION_RAM;
#else
    credentials.client_cert = (const char *) &azure_client_cert;
    credentials.client_cert_size = IOT_AZURE_CLIENT_CERT_LENGTH;
    credentials.private_key = (const char *) &azure_client_key;
    credentials.private_key_size = IOT_AZURE_CLIENT_KEY_LENGTH;
    credentials.root_ca = (const char *) &azure_root_ca_certificate;
    credentials.root_ca_size = IOT_AZURE_ROOT_CA_LENGTH;
#endif
    broker_info.hostname = (const char *)&mqtt_endpoint_buffer;
    broker_info.hostname_len = ep_size;
    broker_info.port = IOT_DEMO_PORT_AZURE_S;
    security = &credentials;

    result = cy_mqtt_create( buffer, NETWORK_BUFFER_SIZE,
                             security, &broker_info,
                             (cy_mqtt_callback_t)mqtt_event_cb, NULL,
                             &mqtthandle );
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\r\ncy_mqtt_create ----------------------------- Pass \r\n" ));
    }
    else
    {
        TEST_INFO(( "\r\ncy_mqtt_create ----------------------------- Fail \r\n" ));
        return TEST_FAIL;
    }

    return CY_RSLT_SUCCESS;
}

static cy_rslt_t configure_hub_environment_variables( void )
{
    cy_rslt_t result = TEST_PASS;
#if SAS_TOKEN_AUTH
    env_vars.hub_device_id._internal.ptr = (uint8_t*) sas_credentials.device_id;
    env_vars.hub_device_id._internal.size = (int32_t) sas_credentials.device_id_len;
    env_vars.hub_sas_key._internal.ptr = (uint8_t*) sas_credentials.sas_token;
    env_vars.hub_sas_key._internal.size = (int32_t) sas_credentials.sas_token_len;
#else
    env_vars.hub_device_id._internal.ptr = (uint8_t*) MQTT_CLIENT_IDENTIFIER_AZURE_CERT;
    env_vars.hub_device_id._internal.size = MQTT_CLIENT_IDENTIFIER_AZURE_CERT_LENGTH;
    env_vars.hub_sas_key._internal.ptr = NULL;
    env_vars.hub_sas_key._internal.size = 0;
#endif
    env_vars.hub_hostname._internal.ptr = (uint8_t*) IOT_DEMO_SERVER_AZURE;
    env_vars.hub_hostname._internal.size = strlen(IOT_DEMO_SERVER_AZURE);
    env_vars.sas_key_duration_minutes = SAS_KEY_DURATION_MINUTES;
    return result;
}

void Azure_hub_method_app(void *arg)
{
    cy_rslt_t TestRes = TEST_PASS ;
    uint8_t Failcount = 0, Passcount = 0;
    uint32_t time_ms = 0;
    az_iot_hub_client_method_request method_request;
#ifdef CY_TFM_PSA_SUPPORTED
    psa_status_t uxStatus = PSA_SUCCESS;
    size_t read_len = 0;
    (void)uxStatus;
    (void)read_len;
#endif

    cy_log_init( CY_LOG_ERR, NULL, NULL );

    /*
     * Initialize the queue for hub method events
     */
    TestRes = cy_rtos_init_queue( &hub_direct_method_event_queue, 10, sizeof(az_iot_hub_client_method_request) );
    if( TestRes == CY_RSLT_SUCCESS )
    {
        TEST_INFO(( "\r\ncy_rtos_init_queue ----------- Pass \r\n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "\r\ncy_rtos_init_queue ----------- Fail \r\n" ));
        Failcount++;
        goto exit;
    }

#if SAS_TOKEN_AUTH
    (void)device_id_buffer;
    (void)sas_token_buffer;
#if ( (defined CY_TFM_PSA_SUPPORTED) && ( SAS_TOKEN_LOCATION_FLASH == false ) )
    /* Read the Device ID from the secured memory */
    uxStatus = psa_ps_get( PSA_DEVICEID_UID, 0, sizeof(device_id_buffer), device_id_buffer, &read_len );
    if( uxStatus == PSA_SUCCESS )
    {
        device_id_buffer[read_len] = '\0';
        TEST_INFO(( "\r\nRetrieved Device ID : %s\r\n", device_id_buffer ));
        sas_credentials.device_id = (uint8_t*)device_id_buffer;
        sas_credentials.device_id_len = read_len;
    }
    else
    {
        TEST_INFO(( "\r\npsa_ps_get for Device ID failed with %d\n", (int)uxStatus ));
        TEST_INFO(( "\r\nTaken Device ID from MQTT_CLIENT_IDENTIFIER_AZURE_SAS macro. \r\n" ));
        sas_credentials.device_id = (uint8_t*)MQTT_CLIENT_IDENTIFIER_AZURE_SAS;
        sas_credentials.device_id_len = MQTT_CLIENT_IDENTIFIER_AZURE_SAS_LENGTH;
    }

    read_len = 0;

    /* Read the SAS token from the secured memory */
    uxStatus = psa_ps_get( PSA_SAS_TOKEN_UID, 0, sizeof(sas_token_buffer), sas_token_buffer, &read_len );
    if( uxStatus == PSA_SUCCESS )
    {
        sas_token_buffer[read_len] = '\0';
        TEST_INFO(( "\r\nRetrieved SAS token : %s\r\n", sas_token_buffer ));
        sas_credentials.sas_token = (uint8_t*)sas_token_buffer;
        sas_credentials.sas_token_len = read_len;
    }
    else
    {
        TEST_INFO(( "\r\npsa_ps_get for sas_token failed with %d\n", (int)uxStatus ));
        TEST_INFO(( "\r\nTaken SAS Token from IOT_AZURE_PASSWORD macro. \r\n" ));
        sas_credentials.sas_token = (uint8_t *)IOT_AZURE_PASSWORD;
        sas_credentials.sas_token_len = IOT_AZURE_PASSWORD_LENGTH;
    }
#else
    sas_credentials.device_id = (uint8_t*)MQTT_CLIENT_IDENTIFIER_AZURE_SAS;
    sas_credentials.device_id_len = MQTT_CLIENT_IDENTIFIER_AZURE_SAS_LENGTH;
    sas_credentials.sas_token = (uint8_t*)IOT_AZURE_PASSWORD;
    sas_credentials.sas_token_len = IOT_AZURE_PASSWORD_LENGTH;
#endif

#endif /* SAS_TOKEN_AUTH */

    TestRes = configure_hub_environment_variables();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "\r\nconfigure_environment_variables ----------- Pass \r\n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "\r\nconfigure_environment_variables ----------- Fail \r\n" ));
        Failcount++;
        goto exit;
    }

    TestRes = create_and_configure_mqtt_client();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "\r\ncreate_and_configure_mqtt_client ----------- Pass \r\n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "\r\ncreate_and_configure_mqtt_client ----------- Fail \r\n" ));
        Failcount++;
        goto exit;
    }

    TestRes = connect_mqtt_client_to_iot_hub();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "\r\nconnect_mqtt_client_to_iot_hub ----------- Pass \r\n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "\r\nconnect_mqtt_client_to_iot_hub ----------- Fail \r\n" ));
        Failcount++;
        goto exit;
    }

    TestRes = subscribe_mqtt_client_to_iot_hub_topics();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "\r\nsubscribe_mqtt_client_to_iot_hub_topics ----------- Pass \r\n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "\r\nsubscribe_mqtt_client_to_iot_hub_topics ----------- Fail \r\n" ));
        Failcount++;
    }

    /* 2 Min check to make the app to work with CI/CD */
    time_ms = ( MESSAGE_WAIT_LOOP_DURATION_MSEC );
    while( connect_state && (time_ms > 0) )
    {
        TEST_INFO(( "\r\nWaiting for hub to client messages...... \r\n" ));

        TestRes = cy_rtos_get_queue( &hub_direct_method_event_queue, (void *)&method_request, GET_QUEUE_TIMEOUT_MSEC, false );
        if( TestRes != CY_RSLT_SUCCESS )
        {
            TEST_INFO(( "\r\ncy_rtos_get_queue returned with status [0x%X] \r\n", (unsigned int)TestRes ));
        }
        else
        {
            handle_method_request( &method_request );
            TEST_INFO(( " " ));
            TEST_INFO(( "\n\rClient received messages.\r\n" ));
        }
        time_ms = time_ms - GET_QUEUE_TIMEOUT_MSEC;
    }

    TestRes = disconnect_and_delete_mqtt_client();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "\r\ndisconnect_and_delete_mqtt_client ----------- Pass \r\n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "\r\ndisconnect_and_delete_mqtt_client ----------- Fail \r\n" ));
        Failcount++;
    }

exit:
    TEST_INFO(( "\r\nCompleted MQTT Client Test Cases --------------------------\n" ));
    TEST_INFO(( "\r\nTotal Test Cases   ---------------------- %d\n", ( Failcount + Passcount ) ));
    TEST_INFO(( "\r\nTest Cases passed  ---------------------- %d\n", Passcount ));
    TEST_INFO(( "\r\nTest Cases failed  ---------------------- %d\n", Failcount ));

    vTaskSuspend(NULL);
}

/* [] END OF FILE */
