/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */


//#include "esp_flash.h"
#include "print_hello.h"


void app_main(void)
{
    while (1)
    {
        print_hello();
        vTaskDelay(5000/ portTICK_PERIOD_MS);
    }
    
    

}