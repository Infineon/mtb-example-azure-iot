/******************************************************************************
 * File Name:   main.c
 *
 * Description: This code example demonstrates Azure IoT features like -
 * Azure (DPS) Device Provisioning Service, C2D (Cloud to Device), Telemetry,
 * Methods, Twin and PnP (Plug and Play). The device establishes a connection
 * with the Azure IoT endpoint and subscribes to topic corresponding to feature
 * being demonstrated. Upon successful connection to Azure, the message transfer
 * takes place between device and Azure.
 *
 * Related Document: See README.md
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

#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cy_log.h"
#include "cyabs_rtos.h"

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>

#ifdef CY_TFM_PSA_SUPPORTED
/* Trusted Firmware header file */
#include "tfm_multi_core_api.h"
#include "tfm_ns_interface.h"
#include "tfm_ns_mailbox.h"
#include "psa/protected_storage.h"

/* Trusted Firmware mailbox struct */
struct                              ns_mailbox_queue_t ns_mailbox_queue;

/******************************************************************************
 * Function Name: tfm_ns_multi_core_boot
 ******************************************************************************
 * Summary:
 *  Function to flag the initialization of secure and non-secure CPU to each
 *  other and to initialize Trusted Firmware mailbox.
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
#endif

/*******************************************************************************
 * Function Name: main
 ********************************************************************************
 * Summary:
 *  System entrance point. This function sets up Menu task and starts
 *  the RTOS scheduler.
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
    cy_rslt_t result = CY_RSLT_SUCCESS;

#ifdef CY_TFM_PSA_SUPPORTED
    cyhal_wdt_t wdt_obj;

    /* Clear watchdog timer so that it doesn't trigger a reset */
    cyhal_wdt_init(&wdt_obj, cyhal_wdt_get_max_timeout_ms());
    cyhal_wdt_free(&wdt_obj);
#endif

    /* Initialize the board support package */
    result = cybsp_init();
    if(result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize retarget-io to use the debug UART port. */
    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
            CY_RETARGET_IO_BAUDRATE);

    CY_ASSERT(result == CY_RSLT_SUCCESS);

#ifdef CY_TFM_PSA_SUPPORTED
    tfm_ns_multi_core_boot();
    /* Initialize the TFM interface */
    tfm_ns_interface_init();
#endif

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");
    printf(AZURE_WELCOME_MESSAGE);

    /* Create the task */
    xTaskCreate(menu_task, "menu_task", MENU_TASK_STACK, NULL, MENU_TASK_PRIORITY, NULL);

    /* Start the FreeRTOS scheduler */
    vTaskStartScheduler() ;

    return 0;
}

/* [] END OF FILE */
