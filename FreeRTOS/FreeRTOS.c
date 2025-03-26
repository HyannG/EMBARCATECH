#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stdio.h"

const uint button = 5;
const uint led = 11;
QueueHandle_t buttonState;  
QueueHandle_t ledState;  

void vCheckButtonTask(void *pvParameters) {  
    for (;;) {
        bool status = (gpio_get(button) == 0);  
        xQueueSend(buttonState, &status, portMAX_DELAY);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void vProcessingTask(void *pvParameters) {
    bool receivedStatus;
    for (;;) {
        if (xQueueReceive(buttonState, &receivedStatus, portMAX_DELAY) == pdTRUE) {
            xQueueSend(ledState, &receivedStatus, 0);
        }
        printf(" %s\n", receivedStatus ? "Pressionado" : "Solto");
    }
}

void vControlLEDTask(void *pvParameters) {
    bool ledStatus;
    for (;;) {
        if (xQueueReceive(ledState, &ledStatus, portMAX_DELAY) == pdTRUE) {
            gpio_put(led, ledStatus); 
        }
    }
}

int main() {
    stdio_init_all();

    gpio_init(button);
    gpio_set_dir(button, GPIO_IN);
    gpio_pull_up(button);

    gpio_init(led);
    gpio_set_dir(led, GPIO_OUT);

    buttonState = xQueueCreate(5, sizeof(bool)); 
    ledState = xQueueCreate(5, sizeof(bool));  

    if (buttonState != NULL && ledState != NULL) {
        xTaskCreate(vCheckButtonTask, "Button Check", 256, NULL, 1, NULL);
        xTaskCreate(vProcessingTask, "Processing", 256, NULL, 2, NULL);
        xTaskCreate(vControlLEDTask, "LED Control", 256, NULL, 3, NULL);
        vTaskStartScheduler();
    }

    while(1);
}