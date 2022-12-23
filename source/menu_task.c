/******************************************************************************
 * File Name: menu_task.c
 *
 * Description: This file contains tasks and functions related to Azure feature
 * task creation and Wi-Fi initialization.
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

/* Wi-Fi connection manager header files. */
#include "cy_wcm.h"
#include "cy_wcm_error.h"

/* IP address related header files (part of the lwIP TCP/IP stack). */
#include "ip_addr.h"


/* IoT SDK, Secure Sockets, and MQTT initialization */
#include "cy_tcpip_port_secure_sockets.h"
#include "mqtt_main.h"

/*******************************************************************************
 * Macros
 ********************************************************************************/
/* MAX connection retries to join WI-FI AP */
#define MAX_CONNECTION_RETRIES              (10u)

/* Wait between connection retries */
#define WIFI_CONN_RETRY_DELAY_MS            (50)

#define ASCII_INTEGER_DIFFERENCE            (48)

/*******************************************************************************
 * Forward declaration
 ********************************************************************************/
cy_rslt_t connect_to_wifi_ap(void);

/*******************************************************************************
 * Function Name: connect_to_wifi_ap
 *******************************************************************************
 * Summary:
 *  Connects to Wi-Fi AP using the user-configured credentials, retries up to a
 *  configured number of times until the connection succeeds.
 *
 *******************************************************************************/
cy_rslt_t connect_to_wifi_ap(void)
{
    cy_wcm_config_t wifi_config = { .interface = CY_WCM_INTERFACE_TYPE_STA};
    cy_wcm_connect_params_t wifi_conn_param;
    cy_wcm_ip_address_t ip_address;
    cy_rslt_t result;

    /* Variable to track the number of connection retries to the Wi-Fi AP
     * specified by WIFI_SSID macro. */
    uint32_t conn_retries = 0;

    printf("\r\nConnecting to Wi-Fi AP...\r\n\n");

    /* Initialize Wi-Fi connection manager. */
    cy_wcm_init(&wifi_config);

    /* Set the Wi-Fi SSID, password and security type. */
    memset(&wifi_conn_param, 0, sizeof(cy_wcm_connect_params_t));
    memcpy(wifi_conn_param.ap_credentials.SSID, WIFI_SSID, sizeof(WIFI_SSID));
    memcpy(wifi_conn_param.ap_credentials.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
    wifi_conn_param.ap_credentials.security = WIFI_SECURITY;

    /* Connect to the Wi-Fi AP */
    for(conn_retries = 0; conn_retries < MAX_CONNECTION_RETRIES; conn_retries++)
    {
        result = cy_wcm_connect_ap( &wifi_conn_param, &ip_address );

        if(result == CY_RSLT_SUCCESS)
        {
            printf("Successfully connected to Wi-Fi network '%s'.\n",
                                wifi_conn_param.ap_credentials.SSID);

            #if(USE_IPV6_ADDRESS)
            /* Get the IPv6 address.*/
                result = cy_wcm_get_ipv6_addr(CY_WCM_INTERFACE_TYPE_STA,
                                              CY_WCM_IPV6_LINK_LOCAL, &ip_address);
                if(result == CY_RSLT_SUCCESS)
                {
                    printf("IPv6 address (link-local) assigned: %s\n",
                            ip6addr_ntoa((const ip6_addr_t*)&ip_address.ip.v6));
                }
            #else
                printf("IPv4 address assigned: %s\n",
                        ip4addr_ntoa((const ip4_addr_t*)&ip_address.ip.v4));

            #endif /* USE_IPV6_ADDRESS */

            return result;
        }


        printf( "\r\nConnection to Wi-Fi network failed with error code %d."
                "Retrying in %d ms...\n", (int) result, WIFI_CONN_RETRY_DELAY_MS );
        vTaskDelay(pdMS_TO_TICKS(WIFI_CONN_RETRY_DELAY_MS));
    }

    printf( "\r\nExceeded maximum Wi-Fi connection attempts\n" );
    return result;
}

/*******************************************************************************
 * Function Name: menu_task
 *******************************************************************************
 * Summary:
 *  Task to create Azure feature task based on user input.
 *
 * Parameters:
 *  args
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void menu_task(void *arg)
{
    /* Status variable */
    uint8_t uart_read_value;
    uint8_t uart_read_integer;
    bool valid_option = false;

    cy_rslt_t result = CY_RSLT_SUCCESS;

    result = connect_to_wifi_ap();
    if(result != CY_RSLT_SUCCESS)
    {
        printf("Unable to connect to Access Point\n");
        printf("Terminating Menu Task\n");
        CY_ASSERT(0);
    }

    while(!valid_option)
    {
        printf("\n===============================================================\n");
        printf(MENU_AZURE_IOT_HUB);
        printf("\n===============================================================\n");

        /* Reading option number from console */
        while (cyhal_uart_getc(&cy_retarget_io_uart_obj, &uart_read_value, 1) != CY_RSLT_SUCCESS);
        /* Converting ASCII character to Integer value */
        uart_read_integer = uart_read_value - ASCII_INTEGER_DIFFERENCE;

        switch(uart_read_integer)
        {
        case AZURE_DEVICE_PROVISIONING_SERVICE:
        {
            printf("Device Provisioning feature demo begins\n");
            xTaskCreate(Azure_dps_app_task, "Azure_dps_app_task",
                    AZURE_TASK_STACK_AZURE_DPS, NULL, AZURE_TASK_PRIORITY_AZURE_DPS, NULL);
            valid_option = true;
            break;
        }

        case DEVICE_DEMO:
        {
            printf("Azure Device demo begins\n");
            xTaskCreate(Azure_Device_Demo_app, "Azure_Device_Demo_app",
                    AZURE_TASK_STACK_DEVICE_DEMO_APP, NULL, AZURE_TASK_PRIORITY_DEVICE_DEMO_APP, NULL);
            valid_option = true;
            break;
        }

        case PLUG_N_PLAY:
        {
            printf("\nPlug and Play feature demo begins\n");
            xTaskCreate(Azure_hub_pnp_app, "Azure_hub_pnp_app",
                    AZURE_TASK_STACK_PNP, NULL, AZURE_TASK_PRIORITY_PNP, NULL);
            valid_option = true;
            break;
        }

        default:
        {
            printf("\x1b[2J\x1b[;H");
            printf("\r\nPlease select from the given valid options\r\n");
        }
        }
    }

    vTaskDelete(NULL);
}

/* [] END OF FILE */
