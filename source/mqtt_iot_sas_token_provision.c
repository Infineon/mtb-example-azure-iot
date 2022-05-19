/******************************************************************************
 * File Name: mqtt_iot_sas_token_provision.c
 *
 * Description: Stand-alone Application for storing Device ID and SAS token in
 * secured memory for Infineon secured kits.
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

#ifdef CY_TFM_PSA_SUPPORTED

#include "cy_pdl.h"
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

#include "FreeRTOS.h"
#include "task.h"

#include "tfm_multi_core_api.h"
#include "tfm_ns_interface.h"
#include "tfm_ns_mailbox.h"
#include "psa/protected_storage.h"

#include "mqtt_main.h"

/******************************************************************************
 * Macros
 ******************************************************************************/
/* Defines for a simple task */
#define TFM_NAME           ("DATA PROVISIONING")
#define TFM_STACK_SIZE     (4096)
#define TFM_PRIORITY       (5)
#define DEVICE_CERT_UID    (0x100)
#define ROOT_CA_UID        (0x101)

#define TEST_DEBUG( x )                       printf x
#define TEST_INFO( x )                        printf x
#define TEST_ERROR( x )                       printf x

/* Add Azure client device ID which needs to be provisioned to secured memory. */
#define AZURE_CLIENT_DEVICE_ID      "Replace this string by device ID generated from Azure cloud."

/* Add Azure client SAS token which needs to be provisioned to secured memory. */
#define AZURE_CLIENT_SAS_TOKEN      "Replace this string by generated SAS token."

/************************************************************
 * Static Variables
 *************************************************************/
static struct ns_mailbox_queue_t ns_mailbox_queue;

/******************************************************************************
 * Function Name: tfm_ns_multi_core_boot
 ******************************************************************************
 * Summary:
 *  Function to sync with secure core and initialize the TF-M mailbox.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void tfm_ns_multi_core_boot(void)
{
    int32_t ret;

    if (tfm_ns_wait_for_s_cpu_ready())
    {
        /* Error sync'ing with secure core */
        /* Avoid undefined behavior after multi-core sync-up failed. */
        for (;;)
        {
        }
    }

    ret = tfm_ns_mailbox_init(&ns_mailbox_queue);
    if (ret != MAILBOX_SUCCESS)
    {
        /* Non-secure mailbox initialization failed. */
        /* Avoid undefined behavior after NS mailbox initialization failed. */
        for (;;)
        {
        }
    }
}

/******************************************************************************
 * Function Name: tfm_test_task
 ******************************************************************************
 * Summary:
 *  Task to store security credentials like SAS token into the Secure memory
 *  using PSA APIs.
 *
 * Parameters:
 *  arg
 *
 * Return:
 *  void
 ******************************************************************************/
void tfm_test_task(void * arg)
{
    char *set_device_id = (char *)AZURE_CLIENT_DEVICE_ID;
    char *set_sas_token = (char *)AZURE_CLIENT_SAS_TOKEN;
    psa_status_t uxStatus = PSA_SUCCESS;
    char get_data[2048];
    size_t get_len = 0;

    (void)arg;

    /* Check Device certificate is provisioned or not */
    get_len = 0;
    memset( &get_data, 0x00, sizeof(get_data) );
    TEST_DEBUG(("\n\rChecking for Device certificate....\n\r"));
    uxStatus = psa_ps_get(DEVICE_CERT_UID, 0, sizeof(get_data), get_data, &get_len);
    if((uxStatus == PSA_SUCCESS) && (get_len>0))
    {
        TEST_DEBUG(("\r\nDevice certificate is present at UID %d...!!!!!!\r\n",(int)DEVICE_CERT_UID));
    }

    /* Check Root CA is provisioned or not */
    get_len = 0;
    memset( &get_data, 0x00, sizeof(get_data) );
    TEST_DEBUG(("\r\nChecking for Root certificate....\n\r"));
    uxStatus = psa_ps_get(ROOT_CA_UID, 0, sizeof(get_data), get_data, &get_len);
    if((uxStatus == PSA_SUCCESS) && (get_len>0))
    {
        TEST_DEBUG(("\r\nRootCA is present at UID %d...!!!!!!\r\n", (int)ROOT_CA_UID));
    }

    TEST_DEBUG(("\n\n\rProvisioning Device ID...\r\n"));
    uxStatus = psa_ps_set(PSA_DEVICEID_UID, strlen(set_device_id), set_device_id, PSA_STORAGE_FLAG_NONE);
    if(uxStatus == PSA_SUCCESS)
    {
        TEST_DEBUG(("\r\nProvisioning Device ID completed...\r\n"));
        get_len = 0;
        memset( &get_data, 0x00, sizeof(get_data) );
        uxStatus = psa_ps_get(PSA_DEVICEID_UID, 0, sizeof(get_data), get_data, &get_len);
        if(uxStatus == PSA_SUCCESS)
        {
            get_data[get_len] = '\0';
            TEST_DEBUG(("\r\nRetrieved Device ID : %s\r\n", get_data));
        }
        else
        {
            TEST_ERROR(("Reading Device ID Failed with psa error %d...\r\n", (int)uxStatus));
        }
    }
    else
    {
        TEST_ERROR(("Provisioning Device ID Failed with psa error %d...\r\n", (int)uxStatus));
    }

    TEST_DEBUG(("Provisioning SAS token...\r\n"));
    uxStatus = psa_ps_set(PSA_SAS_TOKEN_UID, strlen(set_sas_token), set_sas_token, PSA_STORAGE_FLAG_NONE);
    if(uxStatus == PSA_SUCCESS)
    {
        TEST_DEBUG(("Provisioning SAS token completed...\r\n"));
        get_len = 0;
        memset( &get_data, 0x00, sizeof(get_data) );
        uxStatus = psa_ps_get(PSA_SAS_TOKEN_UID, 0, sizeof(get_data), get_data, &get_len);
        if(uxStatus == PSA_SUCCESS)
        {
            get_data[get_len] = '\0';
            TEST_DEBUG(("Retrieved SAS token : %s\r\n", get_data));
        }
        else
        {
            TEST_ERROR(("Reading SAS token Failed with psa error %d...\r\n", (int)uxStatus));
        }
    }
    else
    {
        TEST_ERROR(("Provisioning SAS token Failed with psa error %d...\r\n", (int)uxStatus));
    }

    TEST_DEBUG(("Completed SAS token provisioning task....\n\r"));

    vTaskSuspend(NULL);
}

/*******************************************************************************
 * Function Name: main
 ********************************************************************************
 * Summary:
 *  Entrance of SAS token Provisioning app.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  int
 *
 *******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    int retval;

    /* Unlock and disable the WDT */
    Cy_WDT_Unlock();
    Cy_WDT_Disable();

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    __enable_irq();

    /* Initialize retarget-io to use the debug UART port */
    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
            CY_RETARGET_IO_BAUDRATE);

    /* retarget-io init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    tfm_ns_multi_core_boot();
    /* Initialize the TFM interface */
    tfm_ns_interface_init();

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    TEST_DEBUG(("\x1b[2J\x1b[;H"));
    TEST_DEBUG(("\n\r****************** PSoC 64 MCU: Device ID and SAS token provisioning Application ****************** \r\n\n"));

    /* Create FreeRTOS task */
    retval = xTaskCreate( tfm_test_task, TFM_NAME, TFM_STACK_SIZE, NULL, TFM_PRIORITY, NULL );

    if( retval == pdPASS )
    {
        vTaskStartScheduler();
    }

    return 0;
}

#endif /* CY_TFM_PSA_SUPPORTED */

/* [] END OF FILE */
