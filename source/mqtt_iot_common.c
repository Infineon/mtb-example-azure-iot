/******************************************************************************
* File Name: mqtt_iot_common.c
*
* Description: This file contains implementation of Utility functions for Azure
* sample applications on Infineon platforms.
*
********************************************************************************
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

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cyabs_rtos.h"
#include <az_core.h>
#include "mqtt_iot_common.h"

/***********************************************************
* Macros
************************************************************/
#define SECONDS_IN_A_MINUTE                     (60u)
#define MILLISECONDS_IN_A_SECOND                (1000u)

#define IOT_SAMPLE_PRECONDITION_NOT_NULL(arg)   \
  do                                            \
  {                                             \
    if ((arg) == NULL)                          \
    {                                           \
      IOT_SAMPLE_LOG_ERROR("Pointer is NULL."); \
      exit(1);                                  \
    }                                           \
  } while (0)

/***********************************************************
* Constants
************************************************************/
/* MQTT end point */
static az_span const mqtt_url_prefix = AZ_SPAN_LITERAL_FROM_STR("");
static az_span const mqtt_url_suffix = AZ_SPAN_LITERAL_FROM_STR("");
static az_span const provisioning_global_endpoint = AZ_SPAN_LITERAL_FROM_STR("global.azure-devices-provisioning.net");

/************************************************************
* Utility functions
************************************************************/
void build_error_message( char* out_full_message, size_t full_message_buf_size,
                          char const* const error_message, ... )
{
    char const* const append_message = ": az_result return code 0x%08x.";

    size_t message_len = strlen(error_message) + 1;
    strncpy(out_full_message, error_message, full_message_buf_size);
    out_full_message[full_message_buf_size - 1] = 0;
    if (full_message_buf_size > message_len)
    {
        strncat(out_full_message, append_message, full_message_buf_size - message_len);
        out_full_message[full_message_buf_size - 1] = 0;
    }
}

bool get_az_span( az_span* out_span, char const* const error_message, ... )
{
    va_list args;
    va_start(args, error_message);

    *out_span = va_arg(args, az_span);
    va_end(args);

    if (az_span_size(*out_span) == 0)
    {
        return false;
    }

    return true;
}

uint16_t iot_sample_create_mqtt_endpoint( iot_sample_type type, iot_sample_environment_variables const* env_vars,
                                          char* out_endpoint, size_t endpoint_size )
{
    uint16_t required_size = 0;
    IOT_SAMPLE_PRECONDITION_NOT_NULL( env_vars );
    IOT_SAMPLE_PRECONDITION_NOT_NULL( out_endpoint );

    if( type == CY_MQTT_IOT_HUB )
    {
        required_size = az_span_size(mqtt_url_prefix) +
                        az_span_size(env_vars->hub_hostname) +
                        az_span_size(mqtt_url_suffix) +
                        (int32_t)sizeof('\0');
        if ((size_t)required_size > endpoint_size)
        {
            IOT_SAMPLE_LOG_ERROR("Failed to create MQTT endpoint: Buffer is too small.");
            return 0;
        }

        az_span hub_mqtt_endpoint = az_span_create((uint8_t*)out_endpoint, (int32_t)endpoint_size);
        az_span remainder = az_span_copy(hub_mqtt_endpoint, mqtt_url_prefix);
        remainder = az_span_copy(remainder, env_vars->hub_hostname);
        remainder = az_span_copy(remainder, mqtt_url_suffix);
        az_span_copy_u8(remainder, '\0');
    }
    else if( type == CY_MQTT_IOT_PROVISIONING )
    {
        required_size = az_span_size(provisioning_global_endpoint) +
                        (int32_t)sizeof('\0');
        if( (size_t)required_size > endpoint_size )
        {
            IOT_SAMPLE_LOG_ERROR( "Failed to create MQTT endpoint: Buffer is too small." );
            return 0;
        }

        az_span provisioning_mqtt_endpoint = az_span_create( (uint8_t*)out_endpoint, (int32_t)endpoint_size );
        az_span remainder = az_span_copy( provisioning_mqtt_endpoint, provisioning_global_endpoint );
        az_span_copy_u8(remainder, '\0');
    }
    else
    {
        IOT_SAMPLE_LOG_ERROR( "Failed to create MQTT endpoint: Sample type undefined." );
        return 0;
    }

    IOT_SAMPLE_LOG_SUCCESS( "MQTT endpoint created at \"%s\".", out_endpoint );
    return required_size;
}

void iot_sample_sleep_for_seconds( uint32_t seconds )
{
    cy_rtos_delay_milliseconds( seconds * MILLISECONDS_IN_A_SECOND );
}

uint32_t iot_sample_get_epoch_expiration_time_from_minutes( uint32_t minutes )
{
    return (uint32_t)( time(NULL) + minutes * SECONDS_IN_A_MINUTE );
}

/* [] END OF FILE */
