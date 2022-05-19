/******************************************************************************
 * File Name: mqtt_iot_provisioniong.c
 *
 * Description: This file contains tasks and functions related to Azure
 * Provisioning feature task.
 *
 *******************************************************************************
 * Copyright 2021-2022, Cypress Semiconductor Corporation (an Infineon company) or
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
#include "tfm_multi_core_api.h"
#include "tfm_ns_interface.h"
#include "tfm_ns_mailbox.h"
#include "psa/protected_storage.h"
#endif

#include "cy_secure_sockets.h"
#include <azure/core/az_http_transport.h>
#include "az_iot_provisioning_client.h"

/***********************************************************************
 * Macros
 ************************************************************************/
/* Wait duration between messages, should be in multiples of 500 */
#define MESSAGE_WAIT_LOOP_DURATION_MSEC             (120 * 1000)

#define SAS_KEY_DURATION_MINUTES                    (240)

#define MQTT_CLIENT_ID_BUFFER_SIZE                  (128)

#define GET_QUEUE_TIMEOUT_MSEC                      (500)

#define DPS_APP_TIMEOUT_MSEC                        (500)

#define DPS_OPERATION_EVENT_QUEUE_MSEC              (500)

#define DPS_OPERATION_QUERY_EVENT_QUEUE_LENGTH      (10)

/***********************************************************
 * Static Variables
 ************************************************************/
/* Enable RTOS-aware debugging */
static cy_mqtt_t                mqtthandle;
static cy_queue_t               dps_operation_query_event_queue = NULL;
static volatile bool            connect_state = false;

static iot_sample_environment_variables      env_vars;
static az_iot_provisioning_client            provisioning_client;
static char                                  mqtt_client_username_buffer[IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES];
static char                                  mqtt_endpoint_buffer[IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES];

#if SAS_TOKEN_AUTH
static iot_sample_credentials              sas_credentials;
static char                                device_id_buffer[IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES];
static char                                sas_token_buffer[IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES];
#endif

/* The network buffer must remain valid for the lifetime of the MQTT context. */
static uint8_t                             *buffer = NULL;

static bool dps_app_task_end_flag=0;

/******************************************************************************
 * Function Name: send_operation_query_message
 ******************************************************************************
 * Summary:
 *  Send the operation query message to Azure IoT Hub.
 *
 * Parameters:
 *  register_response: Register or query operation response.
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield.
 *
 ******************************************************************************/
static cy_rslt_t send_operation_query_message(az_iot_provisioning_client_register_response const* register_response)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    int rc;
    size_t topic_len;
    cy_mqtt_publish_info_t pub_msg;

    /* Get the Query Status topic to publish the query status request. */
    char query_topic_buffer[256];

    memset( &query_topic_buffer, 0x00, sizeof(query_topic_buffer) );
    memset( &pub_msg, 0x00, sizeof( cy_mqtt_publish_info_t ) );

    rc = az_iot_provisioning_client_query_status_get_publish_topic( &provisioning_client,
            register_response->operation_id,
            query_topic_buffer,
            sizeof(query_topic_buffer),
            &topic_len );
    if( az_result_failed(rc) )
    {
        IOT_SAMPLE_LOG_ERROR( "Unable to get query status publish topic: az_result return code 0x%08x.", rc );
        return ( (cy_rslt_t)TEST_FAIL );
    }

    /* IMPORTANT: Wait for the recommended retry-after number of seconds before
     * query.
     */
    IOT_SAMPLE_LOG( "Querying after %u seconds...", (unsigned int)register_response->retry_after_seconds );
    vTaskDelay(pdMS_TO_TICKS(register_response->retry_after_seconds));

    /* Publish the query status request .*/

    pub_msg.qos = (cy_mqtt_qos_t)CY_MQTT_QOS0;
    pub_msg.topic = (const char *)&query_topic_buffer;
    pub_msg.topic_len = topic_len;
    pub_msg.payload = NULL;
    pub_msg.payload_len = 0;

    result = cy_mqtt_publish( mqtthandle, &pub_msg );
    if( result == CY_RSLT_SUCCESS )
    {
        TEST_INFO(( "\r\ncy_mqtt_publish completed........\n\r" ));
    }
    else
    {
        TEST_INFO(( "\r\nFailed to publish query status request with Error : [0x%X] ", (unsigned int)result ));
    }
    return result;
}

/******************************************************************************
 * Function Name: handle_device_registration_status_message
 ******************************************************************************
 * Summary:
 *  Handle for device registration message by DPS.
 *
 * Parameters:
 *  register_response: Register or query operation response.
 *
 *  ref_is_operation_complete: Flag for state of operation.
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield.
 *
 ******************************************************************************/
static cy_rslt_t handle_device_registration_status_message(az_iot_provisioning_client_register_response const* register_response,
        bool* ref_is_operation_complete)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    *ref_is_operation_complete = az_iot_provisioning_client_operation_complete( register_response->operation_status );

    /* If the operation is not complete, send query. On return, will loop to
     * receive a new operation message.
     */
    if ( !*ref_is_operation_complete )
    {
        IOT_SAMPLE_LOG( "Operation is still pending." );

        TEST_INFO(( "Pushing to dps operation query queue....\n" ));
        if( xQueueSend( dps_operation_query_event_queue, (void *)&register_response, pdMS_TO_TICKS(DPS_OPERATION_EVENT_QUEUE_MSEC) ) != pdPASS )
        {
            TEST_INFO(( "Pushing to dps operation query queue failed\n"));
        }
    }
    else /* Operation is complete. */
    {
        /* Successful assignment. */
        if( register_response->operation_status == AZ_IOT_PROVISIONING_STATUS_ASSIGNED )
        {
            IOT_SAMPLE_LOG_SUCCESS( "Device provisioned:" );
            IOT_SAMPLE_LOG_AZ_SPAN( "Hub Hostname:", register_response->registration_state.assigned_hub_hostname );
            IOT_SAMPLE_LOG_AZ_SPAN( "Device Id:", register_response->registration_state.device_id );
            IOT_SAMPLE_LOG( " " );
        }
        else /* Unsuccessful assignment (unassigned, failed, or disabled states) .*/
        {
            IOT_SAMPLE_LOG_ERROR( "Device provisioning failed:" );
            IOT_SAMPLE_LOG( "Registration state: %d", register_response->operation_status );
            IOT_SAMPLE_LOG( "Last operation status: %d", register_response->status );
            IOT_SAMPLE_LOG_AZ_SPAN( "Operation ID:", register_response->operation_id );
            IOT_SAMPLE_LOG( "Error code: %u", (unsigned int)register_response->registration_state.extended_error_code );
            IOT_SAMPLE_LOG_AZ_SPAN( "Error message:", register_response->registration_state.error_message );
            IOT_SAMPLE_LOG_AZ_SPAN( "Error timestamp:", register_response->registration_state.error_timestamp );
            IOT_SAMPLE_LOG_AZ_SPAN( "Error tracking ID:", register_response->registration_state.error_tracking_id );
            IOT_SAMPLE_LOG( "extended_error_code: %u",(unsigned int)register_response->registration_state.extended_error_code );
            result =  (cy_rslt_t)TEST_FAIL;
        }
        dps_app_task_end_flag=1;
    }
    return result;
}

/******************************************************************************
 * Function Name: parse_device_registration_status_message
 ******************************************************************************
 * Summary:
 *  Parse the message received from the Azure to retrieve registration response.
 *
 * Parameters:
 *  topic: Pointer to topic of received message.
 *
 *  topic_len: Length of received message topic.
 *
 *  message: MQTT publish/receive information structure.
 *
 *  out_register_response: Register or query operation response.
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield.
 *
 ******************************************************************************/
static cy_rslt_t parse_device_registration_status_message(char* topic, int topic_len,
        cy_mqtt_publish_info_t *message,
        az_iot_provisioning_client_register_response* out_register_response)
{
    az_result rc;
    az_span const topic_span = az_span_create( (uint8_t*)topic, topic_len );
    az_span const message_span = az_span_create( (uint8_t*)message->payload, message->payload_len );

    /* Parse the message and retrieve the register_response info. */
    rc = az_iot_provisioning_client_parse_received_topic_and_payload( &provisioning_client, topic_span,
            message_span, out_register_response );
    if( az_result_failed(rc) )
    {
        IOT_SAMPLE_LOG_ERROR( "Message from unknown topic: az_result return code 0x%08x.", (unsigned int)rc );
        IOT_SAMPLE_LOG_AZ_SPAN( "Topic:", topic_span );
        return ( (cy_rslt_t)TEST_FAIL );
    }
    IOT_SAMPLE_LOG_SUCCESS( "Client received a valid topic response:" );
    IOT_SAMPLE_LOG_AZ_SPAN( "Topic:", topic_span );
    IOT_SAMPLE_LOG_AZ_SPAN( "Payload:", message_span );
    IOT_SAMPLE_LOG( "Status: %d", out_register_response->status );
    return CY_RSLT_SUCCESS;
}

/******************************************************************************
 * Function Name: mqtt_event_cb
 ******************************************************************************
 * Summary:
 *  Callback for MQTT type events.
 *
 * Parameters:
 *  mqtt_handle: Handle to MQTT instance.
 *
 *  event: MQTT event information structure.
 *
 *  arg
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void mqtt_event_cb(cy_mqtt_t mqtt_handle, cy_mqtt_event_t event, void *arg)
{
    cy_mqtt_received_msg_info_t *received_msg = NULL;
    bool is_operation_complete = false;
    az_iot_provisioning_client_register_response *register_response = NULL;

    TEST_INFO(( "\r\nMQTT App callback with handle : %p \n", mqtt_handle ));
    switch( event.type )
    {
    case CY_MQTT_EVENT_TYPE_DISCONNECT :
        if( event.data.reason == CY_MQTT_DISCONN_TYPE_BROKER_DOWN )
        {
            TEST_INFO(( "\r\nCY_MQTT_DISCONN_TYPE_BROKER_DOWN .....\n" ));
        }
        else
        {
            TEST_INFO(( "\r\nCY_MQTT_DISCONN_REASON_NETWORK_DISCONNECTION .....\n" ));
        }
        connect_state = false;
        break;

    case CY_MQTT_EVENT_TYPE_PUBLISH_RECEIVE :
        received_msg = &(event.data.pub_msg.received_message);
        register_response = (az_iot_provisioning_client_register_response *)malloc( sizeof( az_iot_provisioning_client_register_response ) );
        if( register_response == NULL )
        {
            IOT_SAMPLE_LOG( "Memory not available for register_response." );
        }
        else
        {
            /* Parse the registration status message. */
            parse_device_registration_status_message( (char*)received_msg->topic, received_msg->topic_len,
                    received_msg, register_response );
            IOT_SAMPLE_LOG_SUCCESS( "Client parsed registration status message." );
            handle_device_registration_status_message( register_response, &is_operation_complete );
        }
        break;

    default :
        TEST_INFO(( "\r\nUnknown Event .....\n" ));
        break;
    }
}

/******************************************************************************
 * Function Name: register_device_with_provisioning_service
 ******************************************************************************
 * Summary:
 *  Function to register the Device to Azure Hub using DPS.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield
 *
 ******************************************************************************/
static cy_rslt_t register_device_with_provisioning_service(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    int rc;
    size_t topic_len;
    cy_mqtt_publish_info_t pub_msg;

    /* Get the Register topic to publish the register request. */
    char register_topic_buffer[128];

    memset( &register_topic_buffer, 0x00, sizeof(register_topic_buffer) );
    memset( &pub_msg, 0x00, sizeof( cy_mqtt_publish_info_t ) );

    rc = az_iot_provisioning_client_register_get_publish_topic( &provisioning_client,
            register_topic_buffer,
            sizeof(register_topic_buffer), (size_t *)&topic_len );
    if (az_result_failed(rc))
    {
        IOT_SAMPLE_LOG_ERROR( "Failed to get the Register topic: az_result return code 0x%08x.", rc );
        return ( (cy_rslt_t)TEST_FAIL );
    }

    pub_msg.qos = (cy_mqtt_qos_t)CY_MQTT_QOS1;
    pub_msg.topic = (const char *)&register_topic_buffer;
    pub_msg.topic_len = topic_len;
    pub_msg.payload = NULL;
    pub_msg.payload_len = 0;

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
    TEST_INFO(( "\r\nSent publish message to place register request" ));
    return result;
}

/******************************************************************************
 * Function Name: subscribe_mqtt_client_to_provisioning_service_topics
 ******************************************************************************
 * Summary:
 *  Function to subscribe to Azure DPS topic.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield
 *
 ******************************************************************************/
static cy_rslt_t subscribe_mqtt_client_to_provisioning_service_topics(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_mqtt_subscribe_info_t sub_msg[1];

    memset( &sub_msg, 0x00, sizeof( cy_mqtt_subscribe_info_t ) );
    sub_msg[0].qos = (cy_mqtt_qos_t)CY_MQTT_QOS1;
    sub_msg[0].topic = AZ_IOT_PROVISIONING_CLIENT_REGISTER_SUBSCRIBE_TOPIC;
    sub_msg[0].topic_len = ( uint16_t ) ( sizeof( AZ_IOT_PROVISIONING_CLIENT_REGISTER_SUBSCRIBE_TOPIC ) - 1 );

    result = cy_mqtt_subscribe( mqtthandle, &sub_msg[0], 1 );
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\r\ncy_mqtt_subscribe ------------------------ Pass \n" ));
    }
    else
    {
        TEST_INFO(( "\r\ncy_mqtt_subscribe ------------------------ Fail \n" ));
    }

    return result;
}

/******************************************************************************
 * Function Name: disconnect_mqtt_client_from_provisioning_service
 ******************************************************************************
 * Summary:
 *  Function to disconnect and delete MQTT client.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield
 *
 ******************************************************************************/
static cy_rslt_t disconnect_mqtt_client_from_provisioning_service(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    result = cy_mqtt_disconnect( mqtthandle );
    if( result == CY_RSLT_SUCCESS )
    {
        TEST_INFO(( "\n\rcy_mqtt_disconnect ----------------------- Pass \n" ));
    }
    else
    {
        TEST_INFO(( "\n\rcy_mqtt_disconnect ----------------------- Fail \n" ));
    }
    connect_state = false;

    result = cy_mqtt_delete( mqtthandle );
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\n\rcy_mqtt_delete --------------------------- Pass \n" ));
    }
    else
    {
        TEST_INFO(( "\n\rcy_mqtt_delete --------------------------- Fail \n" ));
    }

    result = cy_mqtt_deinit();
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\n\rcy_mqtt_deinit --------------------------- Pass \n" ));
    }
    else
    {
        TEST_INFO(( "\n\rcy_mqtt_deinit --------------------------- Fail \n" ));
    }

    /* Free the network buffer */
    if( buffer != NULL )
    {
        free( buffer );
        buffer = NULL;
    }

    return result;
}

/******************************************************************************
 * Function Name: connect_mqtt_client_to_provisioning_service
 ******************************************************************************
 * Summary:
 *  Function to establish MQTT connection to Azure.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield.
 *
 ******************************************************************************/
static cy_rslt_t connect_mqtt_client_to_provisioning_service(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    int rc;
    size_t username_len = 0, client_id_len = 0;
    cy_mqtt_connect_info_t connect_info;

    /* Get the MQTT client ID used for the MQTT connection */
    char mqtt_client_id_buffer[MQTT_CLIENT_ID_BUFFER_SIZE];

    memset( &connect_info, 0x00, sizeof( cy_mqtt_connect_info_t ) );
    memset( &mqtt_client_id_buffer, 0x00, sizeof( mqtt_client_id_buffer ) );

    rc = az_iot_provisioning_client_get_client_id( &provisioning_client, mqtt_client_id_buffer, sizeof(mqtt_client_id_buffer), &client_id_len );
    if( az_result_failed(rc) )
    {
        TEST_INFO(( "\r\nFailed to get MQTT client id: az_result return code 0x%08x.", rc ));
        return TEST_FAIL;
    }

    /* Get the MQTT client user name. */
    rc = az_iot_provisioning_client_get_user_name( &provisioning_client, mqtt_client_username_buffer,
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
        TEST_INFO(( "\r\ncy_mqtt_connect -------------------------- Pass \n" ));
        connect_state = true;
    }
    else
    {
        TEST_INFO(( "\r\ncy_mqtt_connect -------------------------- Fail \n" ));
        return TEST_FAIL;
    }

    return CY_RSLT_SUCCESS;
}

/******************************************************************************
 * Function Name: create_and_configure_mqtt_client
 ******************************************************************************
 * Summary:
 *  Function to configure MQTT client's credentials and hostname for connection
 *  with Azure.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield.
 *
 ******************************************************************************/
static cy_rslt_t create_and_configure_mqtt_client(void)
{
    int rc;
    uint16_t ep_size = 0;
    cy_rslt_t result = TEST_PASS;
    cy_awsport_ssl_credentials_t credentials;
    cy_awsport_ssl_credentials_t *security = NULL;
    cy_mqtt_broker_info_t broker_info;

    memset( &mqtt_endpoint_buffer, 0x00, sizeof(mqtt_endpoint_buffer) );

    ep_size = iot_sample_create_mqtt_endpoint( CY_MQTT_IOT_PROVISIONING, &env_vars,
            mqtt_endpoint_buffer,
            sizeof(mqtt_endpoint_buffer) );

    /* Initialize the provisioning client with the provisioning global endpoint
     * and the default connection options
     */
    rc = az_iot_provisioning_client_init( &provisioning_client,
            az_span_create_from_str(mqtt_endpoint_buffer),
            env_vars.provisioning_id_scope,
            env_vars.provisioning_registration_id, NULL );
    if( az_result_failed( rc ) )
    {
        TEST_INFO(( "\r\nFailed to initialize hub client: az_result return code 0x%08x.", (unsigned int)rc ));
        return TEST_FAIL;
    }

    /* Allocate the network buffer */
    buffer = (uint8_t *) malloc( sizeof(uint8_t) * NETWORK_BUFFER_SIZE );
    if( buffer == NULL )
    {
        TEST_INFO(( "\r\nNo memory is available for network buffer..! \n" ));
        return TEST_FAIL;
    }

    memset( &broker_info, 0x00, sizeof( cy_mqtt_broker_info_t ) );
    memset( &credentials, 0x00, sizeof( cy_awsport_ssl_credentials_t ) );

    result = cy_mqtt_init();
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\r\ncy_mqtt_init ----------------------------- Pass \n" ));
    }
    else
    {
        TEST_INFO(( "\r\ncy_mqtt_init ----------------------------- Fail \n" ));
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
    broker_info.hostname_len = (uint16_t)ep_size;
    broker_info.port = IOT_DEMO_PORT_AZURE_S;
    security = &credentials;

    result = cy_mqtt_create( buffer, NETWORK_BUFFER_SIZE,
            security, &broker_info,
            (cy_mqtt_callback_t)mqtt_event_cb, NULL,
            &mqtthandle );
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\r\ncy_mqtt_create ----------------------------- Pass \n" ));
    }
    else
    {
        TEST_INFO(( "\r\ncy_mqtt_create ----------------------------- Fail \n" ));
        return TEST_FAIL;
    }

    return CY_RSLT_SUCCESS;
}

/******************************************************************************
 * Function Name: configure_hub_environment_variables
 ******************************************************************************
 * Summary:
 *  Configure MQTT hub environment variables like device ID and hostname
 *  endpoint.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield.
 *
 ******************************************************************************/
static cy_rslt_t configure_hub_environment_variables(void)
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
    env_vars.provisioning_registration_id._internal.ptr = (uint8_t*) IOT_AZURE_DPS_REGISTRATION_ID;
    env_vars.provisioning_registration_id._internal.size = (uint8_t) IOT_AZURE_DPS_REGISTRATION_ID_LEN;
    env_vars.provisioning_id_scope._internal.ptr = (uint8_t*) IOT_AZURE_ID_SCOPE;
    env_vars.provisioning_id_scope._internal.size = (uint8_t) IOT_AZURE_ID_SCOPE_LEN;
    env_vars.sas_key_duration_minutes = SAS_KEY_DURATION_MINUTES;
    return result;
}

/******************************************************************************
 * Function Name: Azure_dps_app_task
 ******************************************************************************
 * Summary:
 *  Task to establish connection to Azure and demonstrate DPS feature task.
 *
 * Parameters:
 *  arg
 *
 * Return:
 *  void
 *
 ******************************************************************************/
void Azure_dps_app_task(void *arg)
{
    cy_rslt_t TestRes = TEST_PASS ;
    uint8_t Failcount = 0, Passcount = 0;
    uint32_t time_ms = 0;
    az_iot_provisioning_client_register_response *register_response = NULL;
#ifdef CY_TFM_PSA_SUPPORTED
    psa_status_t uxStatus = PSA_SUCCESS;
    size_t read_len = 0;
    (void)uxStatus;
    (void)read_len;
#endif

    /* Initialize the queue for hub method events */
    dps_operation_query_event_queue = xQueueCreate( DPS_OPERATION_QUERY_EVENT_QUEUE_LENGTH, sizeof( az_iot_provisioning_client_register_response *) );
    if(dps_operation_query_event_queue != NULL)
    {
        TEST_INFO(( "dps_operation_query_event_queue create ----------- Pass \n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "dps_operation_query_event_queue create ----------- Fail \n" ));
        Failcount++;
        goto exit;
    }

#if SAS_TOKEN_AUTH
    (void)device_id_buffer;
    (void)sas_token_buffer;
#if ( (defined CY_TFM_PSA_SUPPORTED) && ( SAS_TOKEN_LOCATION_FLASH == false ) )
    /* Read Device ID from Secured memory */
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
        TEST_INFO(( "\r\nTaken Device ID from MQTT_CLIENT_IDENTIFIER_AZURE_SAS macro. \n" ));
        sas_credentials.device_id = (uint8_t*)MQTT_CLIENT_IDENTIFIER_AZURE_SAS;
        sas_credentials.device_id_len = MQTT_CLIENT_IDENTIFIER_AZURE_SAS_LENGTH;
    }

    read_len = 0;

    /* Read SAS token from Secured memory */
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
        TEST_INFO(( "\r\n psa_ps_get for sas_token failed with %d\n", (int)uxStatus ));
        TEST_INFO(( "\r\n Taken SAS Token from IOT_AZURE_PASSWORD macro. \n" ));
        sas_credentials.sas_token = (uint8_t *)IOT_AZURE_PASSWORD;
        sas_credentials.sas_token_len = IOT_AZURE_PASSWORD_LENGTH;
    }
#else
    sas_credentials.device_id = (uint8_t*) MQTT_CLIENT_IDENTIFIER_AZURE_SAS;
    sas_credentials.device_id_len = MQTT_CLIENT_IDENTIFIER_AZURE_SAS_LENGTH;
    sas_credentials.sas_token = (uint8_t*) IOT_AZURE_PASSWORD;
    sas_credentials.sas_token_len = IOT_AZURE_PASSWORD_LENGTH;
#endif

#endif /* SAS_TOKEN_AUTH */

    TestRes = configure_hub_environment_variables();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "\r\nconfigure_environment_variables ----------- Pass \n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "\r\nconfigure_environment_variables ----------- Fail \n" ));
        Failcount++;
        goto exit;
    }

    TestRes = create_and_configure_mqtt_client();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "\r\ncreate_and_configure_mqtt_client ----------- Pass \n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "\r\ncreate_and_configure_mqtt_client ----------- Fail \n" ));
        Failcount++;
        goto exit;
    }

    TestRes = connect_mqtt_client_to_provisioning_service();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "\r\nconnect_mqtt_client_to_iot_hub ----------- Pass \n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "\r\nconnect_mqtt_client_to_iot_hub ----------- Fail \n" ));
        Failcount++;
        goto exit;
    }

    TestRes = subscribe_mqtt_client_to_provisioning_service_topics();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "\r\nsubscribe_mqtt_client_to_iot_hub_topics ----------- Pass \n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "\r\nsubscribe_mqtt_client_to_iot_hub_topics ----------- Fail \n" ));
        Failcount++;
        goto exit;
    }

    TestRes = register_device_with_provisioning_service();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "\r\nregister_device_with_provisioning_service ----------- Pass \n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "\r\nregister_device_with_provisioning_service ----------- Fail \n" ));
        Failcount++;
        goto exit;
    }

    /* Delay Loop for Azure dps app task */
    time_ms = ( MESSAGE_WAIT_LOOP_DURATION_MSEC );
    while( connect_state && (time_ms > 0) && (!dps_app_task_end_flag) )
    {
        if(xQueueReceive( dps_operation_query_event_queue, (void *)&register_response, pdMS_TO_TICKS(GET_QUEUE_TIMEOUT_MSEC) ) != pdPASS)
        {
            TEST_INFO(( "xQueueReceive failed for pnp_msg_event_queue\n"));
        }
        else
        {
            if( register_response != NULL )
            {
                send_operation_query_message( register_response );
                IOT_SAMPLE_LOG_SUCCESS( "Client sent operation query message." );
                free( register_response );
                register_response = NULL;
            }
        }
        time_ms = time_ms - DPS_APP_TIMEOUT_MSEC;
    }

    exit:

    printf("\n################################\n"
            "Azure Device Provisioning Service demo ends\n"
            "################################\n");

    TestRes = disconnect_mqtt_client_from_provisioning_service();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "\r\ndisconnect_mqtt_client_from_iot_hub ----------- Pass \n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "\r\ndisconnect_mqtt_client_from_iot_hub ----------- Fail \n" ));
        Failcount++;
    }

    TEST_INFO(( "\r\nCompleted MQTT Client Test Cases --------------------------\n" ));
    TEST_INFO(( "\r\nTotal Test Cases   ---------------------- %d\n", ( Failcount + Passcount ) ));
    TEST_INFO(( "\r\nTest Cases passed  ---------------------- %d\n", Passcount ));
    TEST_INFO(( "\r\nTest Cases failed  ---------------------- %d\n", Failcount ));

    vTaskSuspend(NULL);
}

/* [] END OF FILE */
