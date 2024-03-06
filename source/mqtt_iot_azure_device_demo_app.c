/******************************************************************************
 * File Name: mqtt_iot_azure_device_demo_app.c
 *
 * Description: This file contains tasks and functions related to Azure device
 * demo task.
 *
 ********************************************************************************
 * Copyright 2021-2024, Cypress Semiconductor Corporation (an Infineon company) or
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

/* Header file includes */
#include "azure_common.h"

#include <stdio.h>
#include "cyhal.h"
#include "cybsp.h"
#include "cy_log.h"
#include <lwip/tcpip.h>
#include <lwip/api.h>
#include <cy_retarget_io.h>

#include <cybsp_wifi.h>
#include "cy_nw_helper.h"
#include "cy_wcm.h"
#include "cyabs_rtos.h"

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>

#include "mqtt_main.h"
#include "cy_mqtt_api.h"
#include <az_core.h>
#include <az_iot.h>
#include "mqtt_iot_common.h"

/* Wi-Fi connection manager header files. */
#include "cy_wcm.h"

/* IoT SDK, Secure Sockets, and MQTT initialization */
#include "cy_tcpip_port_secure_sockets.h"

/* Trusted Firmware header file */
#ifdef CY_TFM_PSA_SUPPORTED
#include "tfm_multi_core_api.h"
#include "tfm_ns_interface.h"
#include "tfm_ns_mailbox.h"
#include "psa/protected_storage.h"
#endif

/*******************************************************************************
 * Macros
 ********************************************************************************/
#define MAX_TELEMETRY_MESSAGE_COUNT                 (5)
#define TELEMETRY_SEND_INTERVAL_SEC                 (1)
#define MAX_MESSAGE_COUNT                           (100)

/* Wait duration between messages, should be in multiples of 5 */
#define MESSAGE_WAIT_LOOP_DURATION_SEC              (5)

#define MESSAGE_WAIT_DELAY_MSEC                     (1000 * 5)

#define SAS_KEY_DURATION_MINUTES                    (240)

#define MQTT_CLIENT_ID_BUFFER_SIZE                  (128)

#define TELEMETRY_TOPIC_BUFFER_SIZE                 (128)

/* Defines for methods app */
#define METHODS_RESPONSE_TOPIC_BUFFER_SIZE          (128)

#define GET_QUEUE_TIMEOUT_MSEC                      (500)

#define PUT_QUEUE_TIMEOUT_MSEC                      (500)

#define METHOD_WAIT_LOOP_DURATION_MSEC              (120 * 1000)

/* Defines for twin app */
#define MAX_TWIN_MESSAGE_COUNT                      (5)

#define MESSAGE_SEND_RECEIVE_INTERVAL_MSEC          (10 * 1000)

#define GET_SEMAPHORE_TIMEOUT_MSEC                  (500)

#define DEVICE_TWIN_WAIT_LOOP_DURATION_MSEC         (120 * 1000)

#define DEVICE_TWIN_TIMEOUT_MSEC                    (500)

#define TWIN_MESSAGE_WAIT_DELAY_MSEC                (1000)

#define DEVICE_DEMO_APP_TIMEOUT_MSEC                (5)

#define HUB_DIRECT_METHOD_EVENT_QUEUE_LENGTH        (10)

/***********************************************************
 * Constants
 ************************************************************/
static az_span const method_ping_name = AZ_SPAN_LITERAL_FROM_STR("ping");
static az_span const method_ping_response = AZ_SPAN_LITERAL_FROM_STR("{\"response\": \"pong\"}");
static az_span const method_empty_response_payload = AZ_SPAN_LITERAL_FROM_STR("{}");

static az_span const twin_document_topic_request_id = AZ_SPAN_LITERAL_FROM_STR("get_twin");
static az_span const twin_patch_topic_request_id = AZ_SPAN_LITERAL_FROM_STR("reported_prop");
static az_span const version_name = AZ_SPAN_LITERAL_FROM_STR("$version");
static az_span const desired_device_count_property_name = AZ_SPAN_LITERAL_FROM_STR("Test_count");
static int32_t device_count_value = 0;

/************************************************************
 * Static Variables
 ************************************************************/
static cy_mqtt_t                           mqtthandle;
static iot_sample_environment_variables    env_vars;
static az_iot_hub_client                   hub_client;
static char                                mqtt_client_username_buffer[IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES];
static char                                mqtt_endpoint_buffer[IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES];
static volatile bool                       connect_state = false;

static QueueHandle_t                       hub_direct_method_event_queue = NULL;

static cy_semaphore_t                      twin_app_sem = NULL;

#if SAS_TOKEN_AUTH
static iot_sample_credentials              sas_credentials;
static char                                device_id_buffer[IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES];
static char                                sas_token_buffer[IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES];
#endif

/* The network buffer must remain valid for the lifetime of the MQTT context. */
static uint8_t                             *buffer = NULL;

/*******************************************************************************
 * Function Name: send_method_response
 *******************************************************************************
 * Summary:
 *  This function publishes methods response to Azure Hub.
 *
 * Parameters:
 *  method_request: A method request received from IoT Hub.
 *
 *  status: Azure IoT service status codes.
 *
 *  az_span response: Represents a "view" over a byte buffer that represents a
 *  contiguous region of memory. It contains a pointer to the start of the byte
 *  buffer and the buffer's size.
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield.
 *
 *******************************************************************************/
static cy_rslt_t send_method_response(az_iot_hub_client_method_request const* method_request,
        az_iot_status status, az_span response)
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

/******************************************************************************
 * Function Name: invoke_ping
 ******************************************************************************
 * Summary:
 *  Returns method response string.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  az_span: Represents a "view" over a byte buffer that represents a
 *  contiguous region of memory. It contains a pointer to the start of the byte
 *  buffer and the buffer's size.
 *
 ******************************************************************************/
static az_span invoke_ping(void)
{
    TEST_INFO(( "\r\nPING.......!\r\n" ));
    return method_ping_response;
}

/******************************************************************************
 * Function Name: handle_method_request
 ******************************************************************************
 * Summary:
 *  Matches the method request name with required value - "ping" and handles
 *  the response accordingly.
 *
 * Parameters:
 *  method_request: A method request received from IoT Hub.
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void handle_method_request(az_iot_hub_client_method_request const* method_request)
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

/******************************************************************************
 * Function Name: parse_hub_method_message
 ******************************************************************************
 * Summary:
 *  Parses the message received over MQTT from the Azure IoT hub.
 *
 * Parameters:
 *  topic: Pointer to topic of received message.
 *
 *  topic_len: Length of topic of received message.
 *
 *  message: MQTT publish/receive information structure.
 *
 *  out_method_request: A method request received from IoT Hub.
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield.
 *
 ******************************************************************************/
static cy_rslt_t parse_hub_method_message(char* topic, int topic_len, cy_mqtt_publish_info_t *message,
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

/******************************************************************************
 * Function Name: build_reported_property
 ******************************************************************************
 * Summary:
 *  This function builds the JSON format property to report back to Azure Hub.
 *  The reported property contains the updated value of "Test_count".
 *
 * Parameters:
 *  reported_property_payload: Structure to hold reported property
 *  payload buffer.
 *
 *  out_reported_property_payload: Pointer to JSON text written to the
 *  underlying buffer so far, in the last provided destination buffer.
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void build_reported_property(
        az_span reported_property_payload,
        az_span* out_reported_property_payload)
{
    char const* const log = "Failed to build reported property payload";

    az_json_writer jw;
    IOT_SAMPLE_EXIT_IF_AZ_FAILED(az_json_writer_init(&jw, reported_property_payload, NULL), log);
    IOT_SAMPLE_EXIT_IF_AZ_FAILED(az_json_writer_append_begin_object(&jw), log);
    IOT_SAMPLE_EXIT_IF_AZ_FAILED(
            az_json_writer_append_property_name(&jw, desired_device_count_property_name), log);
    IOT_SAMPLE_EXIT_IF_AZ_FAILED(az_json_writer_append_int32(&jw, device_count_value), log);
    IOT_SAMPLE_EXIT_IF_AZ_FAILED(az_json_writer_append_end_object(&jw), log);

    *out_reported_property_payload = az_json_writer_get_bytes_used_in_destination(&jw);
}

/******************************************************************************
 * Function Name: send_reported_property
 ******************************************************************************
 * Summary:
 *  Function to create and send the JSON format of the property that is to be
 *  reported.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void send_reported_property(void)
{
    int rc;
    uint16_t topic_len = 0;
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_mqtt_publish_info_t pub_msg;
    char reported_property_payload_buffer[128];
    char twin_patch_topic_buffer[128];

    memset( &pub_msg, 0x00, sizeof( cy_mqtt_publish_info_t ) );
    memset( &reported_property_payload_buffer, 0x00, sizeof( reported_property_payload_buffer ) );
    memset( &twin_patch_topic_buffer, 0x00, sizeof( twin_patch_topic_buffer ) );

    IOT_SAMPLE_LOG("\n\rClient sending reported property to service.");

    /* Get the Twin Patch topic to publish a reported property update */
    rc = az_iot_hub_client_twin_patch_get_publish_topic(
            &hub_client,
            twin_patch_topic_request_id,
            twin_patch_topic_buffer,
            sizeof(twin_patch_topic_buffer),
            (size_t*)&topic_len);
    if (az_result_failed(rc))
    {
        IOT_SAMPLE_LOG_ERROR("\n\rFailed to get the Twin Patch topic: az_result return code 0x%08x.", rc);
        return;
    }

    az_span reported_property_payload = AZ_SPAN_FROM_BUFFER(reported_property_payload_buffer);
    build_reported_property( reported_property_payload, &reported_property_payload );

    pub_msg.qos = (cy_mqtt_qos_t)CY_MQTT_QOS0;
    pub_msg.topic = (const char *)&twin_patch_topic_buffer;
    pub_msg.topic_len = topic_len;
    pub_msg.payload = (const char *)reported_property_payload._internal.ptr;
    pub_msg.payload_len = (size_t)reported_property_payload._internal.size;

    /* Publish the reported property update */
    result = cy_mqtt_publish( mqtthandle, &pub_msg );
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\r\ncy_mqtt_publish completed........\n\r" ));
    }
    else
    {
        TEST_INFO(( "\r\ncy_mqtt_publish failed with Error : [0x%X] ", (unsigned int)result ));
        return;
    }

    TEST_INFO(( "\r\nClient published the Twin Patch reported property message." ));
    TEST_INFO(( "\r\nPayload: %.*s\r\n", (int)reported_property_payload._internal.size, reported_property_payload._internal.ptr ));
}

/******************************************************************************
 * Function Name: update_device_count_property
 ******************************************************************************
 * Summary:
 *  Function to update the "Test_count" received from the Azure Hub to the
 *  device_count variable.
 *
 * Parameters:
 *  device_count: Global variable to hold "Test_count" value.
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void update_device_count_property(int32_t device_count)
{
    device_count_value = device_count;
    TEST_INFO(( "Client updated `%.*s` locally to %d.\n",
            (int)az_span_size(desired_device_count_property_name),
            az_span_ptr(desired_device_count_property_name),
            (int)device_count_value ));
}

/******************************************************************************
 * Function Name: parse_desired_device_count_property
 ******************************************************************************
 * Summary:
 *  Parse the message received from Azure Hub to retrieve desired "Test_count"
 *  property.
 *
 * Parameters:
 *  message_span: Structure to hold the received message.
 *
 *  out_parsed_device_count: Used to store the received value of
 *  "Test_count".
 *
 * Return:
 *  bool
 *
 ******************************************************************************/
static bool parse_desired_device_count_property(az_span message_span,
        int32_t* out_parsed_device_count)
{
    char const* const log = "Failed to parse for desired `%.*s` property";
    az_span property = desired_device_count_property_name;

    bool property_found = false;
    *out_parsed_device_count = 0;

    /* Parse message_span */
    az_json_reader jr;
    IOT_SAMPLE_EXIT_IF_AZ_FAILED( az_json_reader_init(&jr, message_span, NULL), log, property );
    IOT_SAMPLE_EXIT_IF_AZ_FAILED( az_json_reader_next_token(&jr), log, property );
    if( jr.token.kind != AZ_JSON_TOKEN_BEGIN_OBJECT )
    {
        TEST_INFO(( "\r\n`%.*s` property was not found in desired property response.",
                (int)az_span_size(property),
                az_span_ptr(property) ));
        return false;
    }

    IOT_SAMPLE_EXIT_IF_AZ_FAILED(az_json_reader_next_token(&jr), log, property);
    while (!property_found && (jr.token.kind != AZ_JSON_TOKEN_END_OBJECT))
    {
        if( az_json_token_is_text_equal(&jr.token, desired_device_count_property_name) )
        {
            /* Move to the value token */
            IOT_SAMPLE_EXIT_IF_AZ_FAILED( az_json_reader_next_token(&jr), log, property );
            IOT_SAMPLE_EXIT_IF_AZ_FAILED( az_json_token_get_int32(&jr.token, out_parsed_device_count), log, property);
            property_found = true;
        }
        else if( az_json_token_is_text_equal(&jr.token, version_name) )
        {
            break;
        }
        else
        {
            IOT_SAMPLE_EXIT_IF_AZ_FAILED(az_json_reader_skip_children(&jr), log, property);
        }
        IOT_SAMPLE_EXIT_IF_AZ_FAILED(az_json_reader_next_token(&jr), log, property);
    }

    if( property_found )
    {
        IOT_SAMPLE_LOG( "Parsed desired `%.*s`: %d",
                (int)az_span_size(property),
                az_span_ptr(property),
                (int)*out_parsed_device_count );
    }
    else
    {
        IOT_SAMPLE_LOG( "`%.*s` property was not found in desired property response.",
                (int)az_span_size(property),
                az_span_ptr(property) );
        return false;
    }

    return true;
}

/******************************************************************************
 * Function Name: handle_device_twin_message
 ******************************************************************************
 * Summary:
 *  Handle for Device Twin type message.
 *
 * Parameters:
 *  message: MQTT publish information structure.
 *
 *  twin_response: Structure for Twin response.
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void handle_device_twin_message(cy_mqtt_publish_info_t *message,
        az_iot_hub_client_twin_response const* twin_response)
{
    az_span const message_span = az_span_create((uint8_t*)message->payload, message->payload_len);
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* Invoke the appropriate action per response type (3 types only) */
    switch( twin_response->response_type )
    {
    case AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_TYPE_GET:
        IOT_SAMPLE_LOG("Message Type: GET");
        break;

    case AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_TYPE_REPORTED_PROPERTIES:
        IOT_SAMPLE_LOG("Message Type: Reported Properties");
        break;

    case AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_TYPE_DESIRED_PROPERTIES:
        IOT_SAMPLE_LOG("Message Type: Desired Properties");

        /* Parse for the device count property */
        int32_t desired_device_count = 0;
        if( parse_desired_device_count_property( message_span, &desired_device_count ) )
        {
            IOT_SAMPLE_LOG(" "); /* Formatting */
            /* Update the device count locally and report the update to
             * the server
             */
            update_device_count_property( desired_device_count );

            result = xSemaphoreGive( twin_app_sem );
            if( result != pdTRUE )
            {
                TEST_INFO(( "Releasing twin_app_sem failed with Error : [0x%X]\n", (unsigned int)result ));
            }

        }
        break;
    }
}

/******************************************************************************
 * Function Name: parse_device_twin_message
 ******************************************************************************
 * Summary:
 *  Parse the message and retrieve twin_response info.
 *
 * Parameters:
 *  topic: Pointer to topic of received message.
 *
 *  topic_len: Length of topic of received message.
 *
 *  message: MQTT publish/subscribe information structure.
 *
 *  out_twin_response: Device twin response.
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void parse_device_twin_message(char* topic, int topic_len,
        cy_mqtt_publish_info_t *message,
        az_iot_hub_client_twin_response* out_twin_response)
{
    az_span const topic_span = az_span_create( (uint8_t*)topic, topic_len);
    az_span const message_span = az_span_create( (uint8_t*)message->payload, message->payload_len );

    /* Parse the message and retrieve twin_response info */
    az_result rc = az_iot_hub_client_twin_parse_received_topic( &hub_client, topic_span, out_twin_response );
    if( az_result_failed(rc) )
    {
        IOT_SAMPLE_LOG_ERROR( "Message from unknown topic: az_result return code 0x%08x.", (unsigned int)rc );
        IOT_SAMPLE_LOG_AZ_SPAN( "Topic:", topic_span );
        return;
    }
    IOT_SAMPLE_LOG_SUCCESS( "Client received a valid topic response." );
    IOT_SAMPLE_LOG_AZ_SPAN( "Topic:", topic_span );
    IOT_SAMPLE_LOG_AZ_SPAN( "Payload:", message_span );
    IOT_SAMPLE_LOG( "Status: %d", out_twin_response->status );
}

/******************************************************************************
 * Function Name: get_device_twin_document
 ******************************************************************************
 * Summary:
 *  Function to receive device twin document from Azure IoT hub.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void get_device_twin_document(void)
{
    int rc;
    uint16_t topic_len = 0;
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_mqtt_publish_info_t pub_msg;
    char twin_document_topic_buffer[128];

    memset( &pub_msg, 0x00, sizeof( cy_mqtt_publish_info_t ) );
    memset( &twin_document_topic_buffer, 0x00, sizeof( twin_document_topic_buffer ) );
    IOT_SAMPLE_LOG("Client requesting device twin document from service.");

    rc = az_iot_hub_client_twin_document_get_publish_topic(
            &hub_client,
            twin_document_topic_request_id,
            twin_document_topic_buffer,
            sizeof(twin_document_topic_buffer),
            (size_t *)&topic_len);
    if (az_result_failed(rc))
    {
        IOT_SAMPLE_LOG_ERROR(
                "Failed to get the Twin Document topic: az_result return code 0x%08x.", rc);
        exit(rc);
    }

    pub_msg.qos = (cy_mqtt_qos_t)CY_MQTT_QOS0;
    pub_msg.topic = (const char *)&twin_document_topic_buffer;
    pub_msg.topic_len = topic_len;
    pub_msg.payload = (const char *)NULL;
    pub_msg.payload_len = (size_t)0;

    /* Publish the twin document request */
    result = cy_mqtt_publish( mqtthandle, &pub_msg );
    if( result == TEST_PASS )
    {
        TEST_INFO(( "\r\ncy_mqtt_publish completed........\n\r" ));
    }
    else
    {
        TEST_INFO(( "\r\ncy_mqtt_publish failed with Error : [0x%X] ", (unsigned int)result ));
        return;
    }
}

/******************************************************************************
 * Function Name: send_and_receive_device_twin_messages
 ******************************************************************************
 * Summary:
 *  Function to send/receive device twin property between Azure Hub and device.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield.
 *
 ******************************************************************************/
static cy_rslt_t send_and_receive_device_twin_messages(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    get_device_twin_document();

    vTaskDelay(pdMS_TO_TICKS(MESSAGE_SEND_RECEIVE_INTERVAL_MSEC));

    IOT_SAMPLE_LOG(" ");

    send_reported_property();

    IOT_SAMPLE_LOG_SUCCESS("Client received messages.");
    return result;
}

/******************************************************************************
 * Function Name: parse_c2d_message
 ******************************************************************************
 * Summary:
 *  Parse the message and retrieve the c2d_request information.
 *
 * Parameters:
 *  topic: Topic of received MQTT message.
 *
 *  topic_len: Length of received message topic.
 *
 *  message: MQTT publish/subscribe information structure.
 *
 *  out_c2d_request: The Cloud-To-Device Request.
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void parse_c2d_message(char* topic, uint16_t topic_len,  cy_mqtt_publish_info_t *message,
        az_iot_hub_client_c2d_request* out_c2d_request)
{
    az_span const topic_span = az_span_create( (uint8_t*)topic, topic_len );
    az_span const message_span = az_span_create( (uint8_t*)message->payload, message->payload_len );

    /* Parse the message and retrieve the c2d_request information */
    az_result rc = az_iot_hub_client_c2d_parse_received_topic( &hub_client, topic_span, out_c2d_request );
    if( az_result_failed(rc) )
    {
        TEST_INFO(( "\r\nMessage from unknown topic: az_result return code 0x%08x.", (unsigned int)rc ));
        TEST_INFO(( "\r\nTopic : %.*s\r\n", (int)topic_span._internal.size, topic_span._internal.ptr ));
        return;
    }

    TEST_INFO(( "\r\nClient received a valid topic response." ));
    TEST_INFO(( "\r\nTopic : %.*s\r\n", (int)topic_span._internal.size, topic_span._internal.ptr ));
    TEST_INFO(( "\r\nPayload: %.*s\r\n", (int)message_span._internal.size,  message_span._internal.ptr ));
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
    cy_mqtt_publish_info_t *received_msg;
    az_iot_hub_client_c2d_request c2d_request;

    TEST_INFO(( "MQTT App callback with handle : %p \n", mqtt_handle ));

    switch( event.type )
    {
    case CY_MQTT_EVENT_TYPE_DISCONNECT :
        if( event.data.reason == CY_MQTT_DISCONN_TYPE_BROKER_DOWN )
        {
            TEST_INFO(( "CY_MQTT_DISCONN_TYPE_BROKER_DOWN .....\n" ));
        }
        else
        {
            TEST_INFO(( "CY_MQTT_DISCONN_REASON_NETWORK_DISCONNECTION .....\n" ));
        }
        connect_state = false;
        break;

    case CY_MQTT_EVENT_TYPE_PUBLISH_RECEIVE :
        received_msg = &(event.data.pub_msg.received_message);

        /* Case for C2D message */
        if( strstr((char*)received_msg->topic, "/messages/devicebound/") != NULL)
        {
            printf("\r\n##############\r\n Incoming C2D \r\n##############\n");
            parse_c2d_message( (char*)received_msg->topic, received_msg->topic_len, received_msg , &c2d_request );
            TEST_INFO(( "\r\nClient parsed C2D message." ));
        }

        /* Case for Methods message */
        else if(strstr((char*)received_msg->topic, "$iothub/methods/POST/") != NULL)
        {
            printf("\r\n##############\r\n Incoming Methods \r\n##############\n");
            /* Parse the method message and invoke the method */
            az_iot_hub_client_method_request method_request;
            parse_hub_method_message( (char*)received_msg->topic, received_msg->topic_len, received_msg, &method_request );
            TEST_INFO(( "Client parsed method request." ));
            TEST_INFO(( "Pushing to direct method queue...." ));

            if( xQueueSend( hub_direct_method_event_queue, (void *)&method_request, pdMS_TO_TICKS(PUT_QUEUE_TIMEOUT_MSEC) ) != pdPASS )
            {
                TEST_INFO(( "Pushing to hub_direct_method_event_queue failed\n"));
            }
        }

        /* Case for Device Twin message */
        else if(strstr((char*)received_msg->topic, "$iothub/twin/") != NULL )
        {
            printf("\r\n##############\r\n Incoming Device Twin \r\n##############\n");
            received_msg = &(event.data.pub_msg.received_message);
            /* Parse the device twin message */
            az_iot_hub_client_twin_response twin_response;
            parse_device_twin_message( (char*)received_msg->topic, received_msg->topic_len, received_msg, &twin_response);
            TEST_INFO(( "Client parsed device twin message." ));
            handle_device_twin_message( received_msg, &twin_response);
        }

        break;

    default :
        TEST_INFO(( "Unknown Event .....\n" ));
        break;
    }
}

/******************************************************************************
 * Function Name: disconnect_and_delete_mqtt_client
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
static cy_rslt_t disconnect_and_delete_mqtt_client(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    result = cy_mqtt_disconnect( mqtthandle );
    if( result == CY_RSLT_SUCCESS )
    {
        TEST_INFO(( "cy_mqtt_disconnect ----------------------- Pass \n" ));
    }
    else
    {
        TEST_INFO(( "cy_mqtt_disconnect ----------------------- Fail \n" ));
    }
    connect_state = false;

    result = cy_mqtt_delete( mqtthandle );
    if( result == TEST_PASS )
    {
        TEST_INFO(( "cy_mqtt_delete --------------------------- Pass \n" ));
    }
    else
    {
        TEST_INFO(( "cy_mqtt_delete --------------------------- Fail \n" ));
    }

    result = cy_mqtt_deinit();
    if( result == TEST_PASS )
    {
        TEST_INFO(( "cy_mqtt_deinit --------------------------- Pass \n" ));
    }
    else
    {
        TEST_INFO(( "cy_mqtt_deinit --------------------------- Fail \n" ));
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
 * Function Name: send_telemetry_messages_to_iot_hub
 ******************************************************************************
 * Summary:
 *  Function to send device telemetry messages to Azure Hub.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield.
 *
 ******************************************************************************/
static cy_rslt_t send_telemetry_messages_to_iot_hub(void)
{
    int rc;
    cy_rslt_t result = CY_RSLT_SUCCESS;
    size_t topic_len = 0;
    cy_mqtt_publish_info_t pub_msg;
    uint8_t offset = 0;

    /* Get the Telemetry topic to publish telemetry messages. */
    char telemetry_topic_buffer[TELEMETRY_TOPIC_BUFFER_SIZE];

    memset( &telemetry_topic_buffer, 0x00, sizeof( telemetry_topic_buffer ) );
    memset( &pub_msg, 0x00, sizeof( cy_mqtt_publish_info_t ) );

    rc = az_iot_hub_client_telemetry_get_publish_topic( &hub_client, NULL, telemetry_topic_buffer,
            sizeof(telemetry_topic_buffer), &topic_len );
    if (az_result_failed(rc))
    {
        IOT_SAMPLE_LOG_ERROR("Failed to get the Telemetry topic: az_result return code 0x%08x.", rc);
        return TEST_FAIL;
    }

    char const* telemetry_message_payloads[MAX_TELEMETRY_MESSAGE_COUNT] = {
            "{\"message_number\":1}", "{\"message_number\":2}", "{\"message_number\":3}",
            "{\"message_number\":4}", "{\"message_number\":5}",
    };

    /* Publish the number of telemetry messages. */
    for( uint8_t message_count = 0; message_count < MAX_MESSAGE_COUNT; message_count++ )
    {
        offset = ( message_count % MAX_TELEMETRY_MESSAGE_COUNT);
        pub_msg.qos = (cy_mqtt_qos_t)CY_MQTT_QOS0;
        pub_msg.topic = (const char *)&telemetry_topic_buffer;
        pub_msg.topic_len = topic_len;
        pub_msg.payload = (const char *)telemetry_message_payloads[offset];
        pub_msg.payload_len = (size_t)( strlen(telemetry_message_payloads[offset]) );

        /* Publish the reported property update. */
        result = cy_mqtt_publish( mqtthandle, &pub_msg );
        if( result == TEST_PASS )
        {
            TEST_INFO(( "cy_mqtt_publish completed........\n\r" ));
            IOT_SAMPLE_LOG_SUCCESS( "Message #%d: Client published the Telemetry message.", message_count + 1);
            IOT_SAMPLE_LOG( "Payload: %s\n", telemetry_message_payloads[offset] );
        }
        else
        {
            TEST_INFO(( "cy_mqtt_publish failed with Error : [0x%X] ", (unsigned int)result ));
            return TEST_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_SEND_INTERVAL_SEC * 1000));
    }
    return CY_RSLT_SUCCESS;
}

/******************************************************************************
 * Function Name: subscribe_azure_hub_features_topics
 ******************************************************************************
 * Summary:
 *  Function to Simultaneously subscribe to corresponding topics of Azure IoT
 *  Hub features.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield.
 *
 ******************************************************************************/
static cy_rslt_t subscribe_azure_hub_features_topics(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_mqtt_subscribe_info_t sub_msg[4];

    /* Subscription topic and parameters for C2D feature. */
    sub_msg[0].qos = (cy_mqtt_qos_t)CY_MQTT_QOS1;
    sub_msg[0].topic = AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC;
    sub_msg[0].topic_len = ( ( uint16_t ) ( sizeof( AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC ) - 1 ) );

    /* Subscription topic and parameters for Methods feature. */
    sub_msg[1].qos = (cy_mqtt_qos_t)CY_MQTT_QOS1;
    sub_msg[1].topic = AZ_IOT_HUB_CLIENT_METHODS_SUBSCRIBE_TOPIC;
    sub_msg[1].topic_len = ( ( uint16_t ) ( sizeof( AZ_IOT_HUB_CLIENT_METHODS_SUBSCRIBE_TOPIC ) - 1 ) );

    /* Subscription topic and parameters for Device Twin Patch,
     * for retrieving desired twin property. */
    sub_msg[2].qos = (cy_mqtt_qos_t)CY_MQTT_QOS1;
    sub_msg[2].topic = AZ_IOT_HUB_CLIENT_TWIN_PATCH_SUBSCRIBE_TOPIC;
    sub_msg[2].topic_len = sizeof( AZ_IOT_HUB_CLIENT_TWIN_PATCH_SUBSCRIBE_TOPIC ) -1 ;

    /* Subscription topic and parameters for Device Twin Response for device twin response. */
    sub_msg[3].qos = (cy_mqtt_qos_t)CY_MQTT_QOS1;
    sub_msg[3].topic = AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_SUBSCRIBE_TOPIC;
    sub_msg[3].topic_len = sizeof( AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_SUBSCRIBE_TOPIC ) -1 ;

    /* Simultaneously Subscribe to topics of Azure features
     * stored in sub_msg array. */
    result = cy_mqtt_subscribe( mqtthandle, sub_msg, 4 );
    if( result == TEST_PASS )
    {
        TEST_INFO(( "cy_mqtt_subscribe for combo "
                "------------------------ Pass \n" ));
    }
    else
    {
        TEST_INFO(( "cy_mqtt_subscribe for combo "
                "------------------------ Fail \n" ));
    }

    return result;
}

/******************************************************************************
 * Function Name: connect_mqtt_client_to_iot_hub
 ******************************************************************************
 * Summary:
 *  Function to establish MQTT connection to Azure IoT hub.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Provides the result of an operation as a structured bitfield.
 *
 ******************************************************************************/
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
        TEST_INFO(( "Failed to get MQTT client id: az_result return code 0x%08x.", rc ));
        return TEST_FAIL;
    }

    /* Get the MQTT client user name */
    rc = az_iot_hub_client_get_user_name( &hub_client, mqtt_client_username_buffer,
            sizeof(mqtt_client_username_buffer), &username_len );
    if( az_result_failed(rc) )
    {
        TEST_INFO(( "Failed to get MQTT client username: az_result return code 0x%08x.", rc ));
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
        TEST_INFO(( "cy_mqtt_connect -------------------------- Pass \n" ));
        connect_state = true;
    }
    else
    {
        TEST_INFO(( "cy_mqtt_connect -------------------------- Fail \n" ));
        return TEST_FAIL;
    }

    return CY_RSLT_SUCCESS;
}

/******************************************************************************
 * Function Name: create_and_configure_mqtt_client
 ******************************************************************************
 * Summary:
 *  Function to configure MQTT client's credentials and hostname for connection
 *  with Azure Hub.
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
        TEST_INFO(( "Failed to initialize hub client: az_result return code 0x%08x.", (unsigned int)rc ));
        return TEST_FAIL;
    }

    /* Allocate the network buffer */
    buffer = (uint8_t *) malloc( sizeof(uint8_t) * NETWORK_BUFFER_SIZE );
    if( buffer == NULL )
    {
        TEST_INFO(( "No memory is available for network buffer..! \n" ));
        return TEST_FAIL;
    }

    memset( &broker_info, 0x00, sizeof( cy_mqtt_broker_info_t ) );
    memset( &credentials, 0x00, sizeof( cy_awsport_ssl_credentials_t ) );

    result = cy_mqtt_init();
    if( result == TEST_PASS )
    {
        TEST_INFO(( "cy_mqtt_init ----------------------------- Pass \n" ));
    }
    else
    {
        TEST_INFO(( "cy_mqtt_init ----------------------------- Fail \n" ));
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
        TEST_INFO(( "cy_mqtt_create ----------------------------- Pass \n" ));
    }
    else
    {
        TEST_INFO(( "cy_mqtt_create ----------------------------- Fail \n" ));
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
    env_vars.sas_key_duration_minutes = SAS_KEY_DURATION_MINUTES;
    return result;
}

/******************************************************************************
 * Function Name: method_feature_task
 ******************************************************************************
 * Summary:
 *  Task to run Azure IoT hub method feature. Handles method request and it's
 *  response.
 *
 * Parameters:
 *  arg
 *
 * Return:
 *  void
 *
 ******************************************************************************/
void method_feature_task(void *arg)
{
    uint32_t time_ms = 0;
    az_iot_hub_client_method_request method_request;

    /*
     * Initialize the queue for hub method events
     */
    hub_direct_method_event_queue = xQueueCreate( HUB_DIRECT_METHOD_EVENT_QUEUE_LENGTH, sizeof(az_iot_hub_client_method_request) );
    if(hub_direct_method_event_queue != NULL)
    {
        TEST_INFO(( "hub_direct_method_event_queue create for methods feature ----------- Pass\n" ));
    }

    else
    {
        TEST_INFO(( "hub_direct_method_event_queue create for methods feature ----------- Fail\n" ));
    }

    time_ms = ( METHOD_WAIT_LOOP_DURATION_MSEC );
    while( connect_state && (time_ms > 0) )
    {
        if(xQueueReceive(hub_direct_method_event_queue, (void *)&method_request, pdMS_TO_TICKS(GET_QUEUE_TIMEOUT_MSEC) ) == pdPASS)
        {
            handle_method_request( &method_request );
            TEST_INFO(( " " ));
            TEST_INFO(( "Client received messages.\r\n" ));
        }

        time_ms = time_ms - GET_QUEUE_TIMEOUT_MSEC;
    }

    vTaskSuspend(NULL);

}

/******************************************************************************
 * Function Name: device_twin_feature_task
 ******************************************************************************
 * Summary:
 *  Task to run Azure IoT hub device twin feature. Handles device twin property
 *  related message transfers between device and cloud.
 *
 * Parameters:
 *  arg
 *
 * Return:
 *  void
 *
 ******************************************************************************/
void device_twin_feature_task(void *arg)
{
    cy_rslt_t TestRes = TEST_PASS ;
    uint32_t time_ms = 0;

    TestRes = send_and_receive_device_twin_messages();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "send_and_receive_device_twin_messages ----------- Pass\n" ));
    }
    else
    {
        TEST_INFO(( "send_and_receive_device_twin_messages ----------- Fail\n" ));
        goto exit_device_twin;
    }

    time_ms = ( DEVICE_TWIN_WAIT_LOOP_DURATION_MSEC );
    while( connect_state && (time_ms > 0))
    {

        TestRes = xSemaphoreTake( twin_app_sem, portMAX_DELAY );
        if( TestRes == pdTRUE )
        {
            send_reported_property();
            TEST_INFO(( " " ));
            TEST_INFO(( "Client received messages." ));
        }

        time_ms = time_ms - DEVICE_TWIN_TIMEOUT_MSEC;
        vTaskDelay(pdMS_TO_TICKS(TWIN_MESSAGE_WAIT_DELAY_MSEC));
    }

    exit_device_twin:
    vTaskSuspend(NULL);

}

/******************************************************************************
 * Function Name: Azure_Device_Demo_app
 ******************************************************************************
 * Summary:
 *  Task to establish connection to Azure IoT hub and create it's feature tasks.
 *
 * Parameters:
 *  arg
 *
 * Return:
 *  void
 *
 ******************************************************************************/
void Azure_Device_Demo_app(void *arg)
{
    cy_rslt_t TestRes = TEST_PASS ;
    uint8_t Failcount = 0, Passcount = 0, time_sec = 0;

#ifdef CY_TFM_PSA_SUPPORTED
    psa_status_t uxStatus = PSA_SUCCESS;
    size_t read_len = 0;
    (void)uxStatus;
    (void)read_len;
#endif

    /* Initialize the semaphore for hub methods events */
    twin_app_sem = xSemaphoreCreateCounting( 1, 0 );
    if( twin_app_sem != NULL )
    {
        TEST_INFO(( "xSemaphoreCreateCounting for Twin App ----------- Pass\n" ));
    }
    else
    {
        TEST_INFO(( "xSemaphoreCreateCounting for Twin App ----------- Fail\n" ));
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
        TEST_INFO(( "Retrieved Device ID : %s\r\n", device_id_buffer ));
        sas_credentials.device_id = (uint8_t*)device_id_buffer;
        sas_credentials.device_id_len = read_len;
    }
    else
    {
        TEST_INFO(( "psa_ps_get for Device ID failed with %d\n", (int)uxStatus ));
        TEST_INFO(( "Taken Device ID from MQTT_CLIENT_IDENTIFIER_AZURE_SAS macro. \n" ));
        sas_credentials.device_id = (uint8_t*)MQTT_CLIENT_IDENTIFIER_AZURE_SAS;
        sas_credentials.device_id_len = MQTT_CLIENT_IDENTIFIER_AZURE_SAS_LENGTH;
    }

    read_len = 0;

    /* Read the SAS token from the secured memory */
    uxStatus = psa_ps_get( PSA_SAS_TOKEN_UID, 0, sizeof(sas_token_buffer), sas_token_buffer, &read_len );
    if( uxStatus == PSA_SUCCESS )
    {
        sas_token_buffer[read_len] = '\0';
        TEST_INFO(( "Retrieved SAS token : %s\r\n", sas_token_buffer ));
        sas_credentials.sas_token = (uint8_t*)sas_token_buffer;
        sas_credentials.sas_token_len = read_len;
    }
    else
    {
        TEST_INFO(( "psa_ps_get for sas_token failed with %d\n", (int)uxStatus ));
        TEST_INFO(( "Taken SAS Token from IOT_AZURE_PASSWORD macro. \n" ));
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
        TEST_INFO(( "configure_environment_variables ----------- Pass\n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "configure_environment_variables ----------- Fail\n" ));
        Failcount++;
        goto exit;
    }

    TestRes = create_and_configure_mqtt_client();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "create_and_configure_mqtt_client ----------- Pass\n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "create_and_configure_mqtt_client ----------- Fail\n" ));
        Failcount++;
        goto exit;
    }

    TestRes = connect_mqtt_client_to_iot_hub();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "connect_mqtt_client_to_iot_hub ----------- Pass\n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "connect_mqtt_client_to_iot_hub ----------- Fail\n" ));
        Failcount++;
        goto exit;
    }

    TestRes = subscribe_azure_hub_features_topics();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "subscribe_mqtt_client_to_iot_hub_topics for Telemetry "
                "----------- Pass\n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "subscribe_mqtt_client_to_iot_hub_topics for TELEMTRY "
                "----------- Fail\n" ));
        Failcount++;
    }

    /* Method feature task creation */
    xTaskCreate(method_feature_task, "method_feature_task",
            AZURE_TASK_STACK_METHODS, NULL, AZURE_TASK_PRIORITY_METHODS, NULL);

    /* Twin feature task creation */
    xTaskCreate(device_twin_feature_task, "device_twin_feature_task",
            AZURE_TASK_STACK_TWIN, NULL, AZURE_TASK_PRIORITY_TWIN, NULL);

    TestRes = send_telemetry_messages_to_iot_hub();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "send_telemetry_messages_to_iot_hub ----------- Pass\n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "send_telemetry_messages_to_iot_hub ----------- Fail\n" ));
        Failcount++;
    }

    /* Delay Loop for Azure Device Demo app task */
    time_sec = MESSAGE_WAIT_LOOP_DURATION_SEC;
    while( (connect_state) && (time_sec > 0) )
    {
        vTaskDelay(pdMS_TO_TICKS(MESSAGE_WAIT_DELAY_MSEC));
        time_sec = time_sec - DEVICE_DEMO_APP_TIMEOUT_MSEC;
    }

    exit:

    printf("################################\n"
            "Azure Device App demo Ends\n"
            "################################\n");

    TEST_INFO(( "Disconnecting from broker...... \n" ));
    TestRes = disconnect_and_delete_mqtt_client();
    if( TestRes == TEST_PASS )
    {
        TEST_INFO(( "disconnect_and_delete_mqtt_client ----------- Pass\n" ));
        Passcount++;
    }
    else
    {
        TEST_INFO(( "disconnect_and_delete_mqtt_client ----------- Fail\n" ));
        Failcount++;
        goto exit;
    }

    TEST_INFO(( "Completed MQTT Client Test Cases --------------------------\n" ));
    TEST_INFO(( "Total Test Cases   ---------------------- %d\n", ( Failcount + Passcount ) ));
    TEST_INFO(( "Test Cases passed  ---------------------- %d\n", Passcount ));
    TEST_INFO(( "Test Cases failed  ---------------------- %d\n", Failcount ));

    vTaskSuspend(NULL);
}

/* [] END OF FILE */
