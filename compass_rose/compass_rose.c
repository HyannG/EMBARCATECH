#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include <string.h>
#include <stdio.h>
#include "hardware/adc.h"
#include "hardware/pio.h"
#include "ws2818b.pio.h"
#include "hardware/gpio.h"
#include "inc/ssd1306.h"
#include "hardware/i2c.h"
#include "pico/binary_info.h"
#include "hardware/clocks.h"
#include "direction.h"

const int VRX = 27;          
const int VRY = 26;         
const int ADC_CHANNEL_X = 1;
const int ADC_CHANNEL_Y = 0;
#define WIFI_SSID "wifi nome"     
#define WIFI_PASS "sua senha" 
#define THRESHOLD 10       
#define LED_COUNT 25 
const int LED_PIN = 7;

struct pixel_t {
    uint8_t G, R, B;
};
typedef struct pixel_t pixel_t;

pixel_t leds[LED_COUNT];
PIO np_pio;
uint sm;
uint16_t vrx_value = 0;
uint16_t vry_value = 0;
uint16_t prev_vrx_value = 0;
uint16_t prev_vry_value = 0;
char direction[16] = "Centro"; 
uint8_t ssd[ssd1306_buffer_length];
struct render_area frame_area;

void npInit(uint pin) {
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;
    sm = pio_claim_unused_sm(np_pio, true);
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
    memset(leds, 0, sizeof(leds));
}

void npSetLED(uint index, uint8_t r, uint8_t g, uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
}

void npClear() {
    memset(leds, 0, sizeof(leds));
}

void npWrite() {
    for (uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
}

char http_response[1024];

void create_http_response() {

    snprintf(http_response, sizeof(http_response),
             "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n"
             "<!DOCTYPE html>"
             "<html>"
             "<head>"
             "  <meta charset=\"UTF-8\">"
             "  <title>Monitor do Joystick</title>"
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
             "   a {"
             "     color: white;"
             "     text-decoration: none;"
             "    }"
             "  </style>"
             "</head>"
             "<body>"
             "  <h1>Monitor do Joystick</h1>"
             "  <p class='update'><a href=\"/update\">Atualizar Estado</a></p>"
             "  <h2>Posição do Joystick:</h2>"
             "  <p>Eixo X: %d</p>"
             "  <p>Eixo Y: %d</p>"
             "  <p>Direção: %s</p>" 
             "</body>"
             "</html>\r\n",
             vrx_value, vry_value, direction);
}

static err_t http_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    pbuf_free(p);

    create_http_response();

    tcp_write(tpcb, http_response, strlen(http_response), TCP_WRITE_FLAG_COPY);

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

void joystick_read_axis(uint16_t *x, uint16_t *y) {
    adc_select_input(ADC_CHANNEL_X);
    sleep_us(2);
    *x = adc_read();
    adc_select_input(ADC_CHANNEL_Y);
    sleep_us(2);
    *y = adc_read();
}

void reset_display(uint8_t *ssd, struct render_area *frame_area) {
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, frame_area);
}

void print_direction(const char *point) {
    strcpy(direction, point);
    printf("%s\n", point);  
    reset_display(ssd, &frame_area);
    ssd1306_draw_string(ssd, 32, 16, point);  
    render_on_display(ssd, &frame_area);
}

void update_direction_leds(int *array) {
    npClear();

    for (int num = 0; num <= 24; num++) {
        if(array[num] != 0){
            npSetLED(num, 0, 0, 50);
        } 
    }
    npWrite();
}

void monitor_joystick() {
    uint16_t current_x, current_y;
    joystick_read_axis(&current_x, &current_y);
    
    if (abs(current_x - prev_vrx_value) > THRESHOLD || abs(current_y - prev_vry_value) > THRESHOLD) {
        vrx_value = current_x;
        vry_value = current_y;
        prev_vrx_value = current_x;
        prev_vry_value = current_y;
        printf("X: %d, Y: %d\n", vrx_value, vry_value); 
        // Lógica para detectar os pontos cardeais
        if (vrx_value > 2000 && vry_value > 4000) {
            print_direction("Norte");
            update_direction_leds(norte);
        } else if (vrx_value > 2000 && vrx_value < 3000 && vry_value < 100) {
            print_direction("Sul");
            update_direction_leds(sul);
        } else if (vrx_value < 100 && vry_value > 1000 && vry_value < 3000) {
            print_direction("Leste");
            update_direction_leds(leste);
        } else if (vrx_value > 4000 && vry_value > 2000 && vry_value < 3500) {
            print_direction("Oeste");
            update_direction_leds(oeste);
        } else if (vrx_value > 3000 && vrx_value < 4000 && vry_value > 3000 && vry_value < 4000) {
            print_direction("Nordeste");
            update_direction_leds(nordeste);
        } else if (vrx_value < 2000 && vry_value > 3000) {
            update_direction_leds(noroeste);
            print_direction("Noroeste");           
        } else if (vrx_value < 2000 && vry_value < 1000) {
            print_direction("Sudeste");
            update_direction_leds(sudeste);
        } else if (vrx_value > 3000 && vry_value < 2000) {
            print_direction("Sudoeste");
            update_direction_leds(sudoeste);
        } else if(vrx_value > 1900 && vrx_value < 2100 && vry_value > 1900 && vry_value < 2100){
            update_direction_leds(centro);
            print_direction("Centro");
        }
    }
}

int main() {
    stdio_init_all(); 
    npInit(LED_PIN);
    sleep_ms(10000);
    printf("Iniciando servidor HTTP\n");

    if (cyw43_arch_init()) {
        printf("Erro ao inicializar o Wi-Fi\n");
        return 1;
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
            sleep_ms(5000);  
        }
    }

    adc_init();
    adc_gpio_init(VRX);
    adc_gpio_init(VRY);

    joystick_read_axis(&prev_vrx_value, &prev_vry_value);
    vrx_value = prev_vrx_value;
    vry_value = prev_vry_value;

    start_http_server();

    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(14, GPIO_FUNC_I2C); // SDA
    gpio_set_function(15, GPIO_FUNC_I2C); // SCL
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

    while (true) {
        cyw43_arch_poll();
        monitor_joystick();
        
        sleep_ms(100);
    }

    cyw43_arch_deinit();
    return 0;
}