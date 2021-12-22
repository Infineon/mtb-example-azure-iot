/******************************************************************************
* File Name: mqtt_iot_common.h
*
* Description: This file contains header file for Azure sample applications
* utility functions on Infineon platforms.
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

#ifndef IOT_SAMPLE_COMMON_H
#define IOT_SAMPLE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <az_core.h>

/*******************************************************************************
* Macros
********************************************************************************/
#define IOT_SAMPLE_APP_BUFFER_SIZE_IN_BYTES       (256)
#define IOT_SAMPLE_SAS_KEY_DURATION_TIME_DIGITS   (4)
#define IOT_SAMPLE_MQTT_PUBLISH_QOS               (0)

/* Logging */
#define IOT_SAMPLE_LOG_ERROR(...)                                                  \
  do                                                                               \
  {                                                                                \
    (void)fprintf(stderr, "\n\rERROR :%s:%s():%d: ", __FILE__, __func__, __LINE__); \
    (void)fprintf(stderr, __VA_ARGS__);                                            \
    (void)fprintf(stderr, "\n\r");                                                   \
    fflush(stdout);                                                                \
    fflush(stderr);                                                                \
  } while (0)

#define IOT_SAMPLE_LOG_SUCCESS(...) \
  do                                \
  {                                 \
    (void)printf("\n\rSUCCESS: ");  \
    (void)printf(__VA_ARGS__);      \
    (void)printf("\n\r");           \
  } while (0)

#define IOT_SAMPLE_LOG(...)    \
  do                           \
  {                            \
    (void)printf("\n\r");      \
    (void)printf(__VA_ARGS__); \
    (void)printf("\n\r");      \
  } while (0)

#define IOT_SAMPLE_LOG_AZ_SPAN(span_description, span)                                           \
  do                                                                                             \
  {                                                                                              \
    (void)printf("\n\r%s ", span_description);                                                   \
    (void)fwrite((char*)az_span_ptr(span), sizeof(uint8_t), (size_t)az_span_size(span), stdout); \
    (void)printf("\n\r");                                                                          \
  } while (0)

/* Error handling */
/* Note: Only handles a single variadic parameter of type char const*, or two
 * variadic parameters of type char const* and az_span. */

void build_error_message(char* out_full_message, size_t full_message_buf_size, char const* const error_message, ...);
bool get_az_span(az_span* out_span, char const* const error_message, ...);

#define IOT_SAMPLE_EXIT_IF_AZ_FAILED(azfn, ...)                                            \
  do                                                                                       \
  {                                                                                        \
    az_result const result = (azfn);                                                       \
                                                                                           \
    if (az_result_failed(result))                                                          \
    {                                                                                      \
      char full_message[256];                                                              \
      build_error_message(full_message, sizeof(full_message), __VA_ARGS__);                \
                                                                                           \
      az_span span;                                                                        \
      bool has_az_span = get_az_span(&span, __VA_ARGS__, AZ_SPAN_EMPTY);                   \
      if (has_az_span)                                                                     \
      {                                                                                    \
        IOT_SAMPLE_LOG_ERROR(full_message, az_span_size(span), az_span_ptr(span), result); \
      }                                                                                    \
      else                                                                                 \
      {                                                                                    \
        IOT_SAMPLE_LOG_ERROR(full_message, result);                                        \
      }                                                                                    \
      exit(1);                                                                             \
    }                                                                                      \
  } while (0)

/***********************************************************
* Global Variables
************************************************************/
typedef struct
{
  az_span hub_device_id;
  az_span hub_hostname;
  az_span hub_sas_key;
  az_span provisioning_id_scope;
  az_span provisioning_registration_id;
  az_span provisioning_sas_key;
  az_span x509_cert_pem_file_path;
  az_span x509_trust_pem_file_path;
  uint32_t sas_key_duration_minutes;
} iot_sample_environment_variables;

typedef struct
{
  uint8_t* device_id;
  uint16_t device_id_len;
  uint8_t* sas_token;
  uint16_t sas_token_len;
} iot_sample_credentials;

typedef enum
{
  CY_MQTT_IOT_HUB,
  CY_MQTT_IOT_PROVISIONING
} iot_sample_type;

typedef enum
{
  CY_MQTT_IOT_HUB_C2D_SAMPLE,
  CY_MQTT_IOT_HUB_METHODS_SAMPLE,
  CY_MQTT_IOT_HUB_PNP_COMPONENT_SAMPLE,
  CY_MQTT_IOT_HUB_PNP_SAMPLE,
  CY_MQTT_IOT_HUB_SAS_TELEMETRY_SAMPLE,
  CY_MQTT_IOT_HUB_TELEMETRY_SAMPLE,
  CY_MQTT_IOT_HUB_TWIN_SAMPLE,
  CY_MQTT_IOT_PROVISIONING_SAMPLE,
  CY_MQTT_IOT_PROVISIONING_SAS_SAMPLE
} iot_sample_name;

extern bool is_device_operational;

/*
 * @brief Builds an MQTT endpoint C string for an Azure IoT Hub or provisioning
 * service
 *
 * @param[in] type Enumerated type of the sample
 * @param[in] env_vars Pointer to environment variable struct
 * @param[out] endpoint Buffer with sufficient capacity to hold the built
 * endpoint. If
 * successful, contains a null-terminated string of the endpoint.
 * @param[in] endpoint_size Size of \p out_endpoint in bytes
 */
uint16_t iot_sample_create_mqtt_endpoint(
    iot_sample_type type,
    iot_sample_environment_variables const* env_vars,
    char* endpoint,
    size_t endpoint_size);

/*
 * @brief Sleep for given seconds
 *
 * @param[in] seconds Number of seconds to sleep
 */
void iot_sample_sleep_for_seconds(uint32_t seconds);

/*
 * @brief Returns total seconds passed including supplied minutes
 *
 * @param[in] minutes Number of minutes to include in total seconds returned
 *
 * @return Total time in seconds
 */
uint32_t iot_sample_get_epoch_expiration_time_from_minutes(uint32_t minutes);

#endif /* IOT_SAMPLE_COMMON_H */

/* [] END OF FILE */
