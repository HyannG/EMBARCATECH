#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "inc/ssd1306.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define LED_PIN 13         
#define BUTTON1_PIN 5     
const int MIC = 28;  
const int ADC_RES = 4095;    
const float ADC_REF = 3.3f;  
const float SOUND_OFFSET = 1.65f; 
const float SOUND_THRESHOLD_LOW = 0.02f;
const float SOUND_THRESHOLD_MEDIUM = 1.0f; 
const float SOUND_THRESHOLD_HIGH = 1.3f;
float MAX_SOUND = 0.0f;
#define WIFI_SSID "REDE WIFI"    
#define WIFI_PASS "SENHA WIFI"

SemaphoreHandle_t xMutex;
char button_message[50] = "Botão sem interação";
float current_sound_level = 0.0f;
char sound_message[50] = "Nenhum som captado!";

char http_response[1024];
uint8_t ssd[ssd1306_buffer_length];
struct render_area frame_area;

void wifi_connection_task(void *pvParameters);
void button_monitor_task(void *pvParameters);
void http_server_task(void *pvParameters);
void display_update_task(void *pvParameters);

void init_display() {
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(14, GPIO_FUNC_I2C); 
    gpio_set_function(15, GPIO_FUNC_I2C); 
    gpio_pull_up(14);
    gpio_pull_up(15);
    ssd1306_init();
    
    frame_area.start_column = 0;
    frame_area.end_column = ssd1306_width - 1;
    frame_area.start_page = 0;
    frame_area.end_page = ssd1306_n_pages - 1;
    
    calculate_render_area_buffer_length(&frame_area);
    
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);
}

void update_display_sound(float level, float max) {
    memset(ssd, 0, ssd1306_buffer_length);
    char sound_str[20];
    char max_sound_str[20];
    
    snprintf(sound_str, sizeof(sound_str), "Som: %.2f V", level);
    ssd1306_draw_string(ssd, 4, 16, sound_str);
    if (max > 0.0f) {
        snprintf(max_sound_str, sizeof(max_sound_str), "Maior som: %.2f V", max); 
        ssd1306_draw_string(ssd, 4, 24, max_sound_str);
    }
    
    render_on_display(ssd, &frame_area);
}

void create_http_response() {
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        snprintf(http_response, sizeof(http_response),
                "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n"
                "<!DOCTYPE html>"
                "<html>"
                "<head>"
                "  <meta charset=\"UTF-8\">"
                "  <title>Microfone</title>"
                "  <meta http-equiv=\"refresh\" content=\"1\">"
                "  <style>"
                "    body {"
                "      font-family: Arial, sans-serif;"
                "      background-color: #0066cc;"
                "      margin: 0;"
                "      padding: 20px;"
                "      display: flex;"
                "      flex-direction: column;"
                "      align-items: center;"
                "      justify-content: center;"
                "      min-height: 100vh;"
                "      color: white;"
                "    }"
                "    a {"
                "      color: white;"
                "      text-decoration: none;"
                "    }"
                "  </style>"
                "</head>"
                "<body>"
                "  <h1>Controle do Microfone</h1>"
                "    <h2>Estado do Botão:</h2>"
                "    <p>%s</p>"
                "    <h2>Nível do Som:</h2>"
                "    <p>%s</p>"
                "    <p>Nível atual: %.2f V</p>"
                "    <p>Máximo captado: %.2f V</p>"
                "  <p><a href=\"/\">Atualizar</a></p>"
                "</body>"
                "</html>\r\n",
                button_message, sound_message, current_sound_level, MAX_SOUND);
        xSemaphoreGive(xMutex);
    }
}

static err_t http_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        tcp_close(tpcb);
        return ERR_OK;
    }
    char *request = (char *)p->payload;
    create_http_response();
    tcp_write(tpcb, http_response, strlen(http_response), TCP_WRITE_FLAG_COPY);
    pbuf_free(p);
    return ERR_OK;
}

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, http_callback); 
    return ERR_OK;
}

static void start_http_server(void) {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        printf("Erro ao criar PCB\n");
        return;
    }

    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }

    pcb = tcp_listen(pcb);  
    tcp_accept(pcb, connection_callback);  

    printf("Servidor HTTP rodando na porta 80...\n");
}

void check_sound_trigger() {
    adc_select_input(2);
    uint16_t raw_adc = adc_read();
    float voltage = (raw_adc * ADC_REF) / ADC_RES;
    
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        current_sound_level = fabs(voltage - SOUND_OFFSET);

        if(current_sound_level > MAX_SOUND){
            MAX_SOUND = current_sound_level;
        }

        if (MAX_SOUND >= SOUND_THRESHOLD_HIGH) {
            snprintf(sound_message, sizeof(sound_message), "Intensidade alta captada!");
        } else if (MAX_SOUND >= SOUND_THRESHOLD_MEDIUM) {
            snprintf(sound_message, sizeof(sound_message), "Intensidade média captada!");
        } else if (MAX_SOUND >= SOUND_THRESHOLD_LOW) {
            snprintf(sound_message, sizeof(sound_message), "Intensidade baixa captada!");
        } else {
            snprintf(sound_message, sizeof(sound_message), "Nenhum som captado!");
        }
        xSemaphoreGive(xMutex);
    }
}


void wifi_connection_task(void *pvParameters) {
    printf("Iniciando servidor HTTP\n");

    if (cyw43_arch_init()) {
        printf("Erro ao inicializar o Wi-Fi\n");
        vTaskDelete(NULL);
    }

    cyw43_arch_enable_sta_mode();

    while (true) {
        printf("Conectando ao Wi-Fi...\n");

        if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000) == 0) {
            printf("Conectado!\n");
            uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
            printf("Endereço IP: %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
            break; 
        } else {
            printf("Falha ao conectar. Tentando novamente em 5 segundos...\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    start_http_server();

    while (true) {
        cyw43_arch_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void button_monitor_task(void *pvParameters) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    gpio_init(BUTTON1_PIN);
    gpio_set_dir(BUTTON1_PIN, GPIO_IN);
    gpio_pull_up(BUTTON1_PIN);
    adc_init();
    adc_gpio_init(MIC);
    adc_select_input(2);

    bool button_last_state = false;
    bool button_pressed = false;

    while (true) {
        bool button_state = !gpio_get(BUTTON1_PIN);

        if (button_state && !button_last_state) {
            if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
                gpio_put(LED_PIN, 1);
                snprintf(button_message, sizeof(button_message), "Botão pressionado!");
                MAX_SOUND = 0.0f;
                xSemaphoreGive(xMutex);
            }
            button_pressed = true;
        } 
        else if (!button_state && button_last_state) {
            if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
                gpio_put(LED_PIN, 0);
                snprintf(button_message, sizeof(button_message), "Botão solto!");
                xSemaphoreGive(xMutex);
            }
            button_pressed = false;
        }

        button_last_state = button_state;

        if (button_pressed) {
            check_sound_trigger();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void display_update_task(void *pvParameters) {
    init_display();
    
    while (true) {
        if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
            if (!gpio_get(BUTTON1_PIN)) { 
                update_display_sound(current_sound_level, MAX_SOUND );
            }
            xSemaphoreGive(xMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int main() {
    stdio_init_all();
    sleep_ms(10000); 

    xMutex = xSemaphoreCreateMutex();
    if (xMutex == NULL) {
        printf("Erro ao criar mutex\n");
        return 1;
    }

    xTaskCreate(wifi_connection_task, "WiFi Task", 1024, NULL, 2, NULL);
    xTaskCreate(button_monitor_task, "Button Task", 1024, NULL, 3, NULL);
    xTaskCreate(display_update_task, "Display Task", 1024, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true) {

    }

    return 0;
}