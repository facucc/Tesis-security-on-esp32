#include "print_hello.h"

void print_hello(void)
{   
    while (1)
    {
        printf("Hello world\n");
        vTaskDelay(5000/ portTICK_PERIOD_MS);
    }
    return;
}
