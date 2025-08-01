#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h> // Para funções trigonométricas como atan2f e sqrtf
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"


#include "inc/ssd1306.h" // Biblioteca para o display OLED SSD1306

// --- Definições para o Sensor MPU6050 ---
#define MPU6050_ADDR         0x68 // Endereço I2C do MPU6050 (AD0 para GND)
#define MPU6050_PWR_MGMT_1   0x6B // Registrador Power Management 1
#define MPU6050_ACCEL_XOUT_H 0x3B // Registrador MSB do X do Acelerômetro

// Fator de escala do acelerômetro para ±2g (padrão do MPU6050 ao iniciar)
// 1g (aceleração da gravidade) = 16384 unidades brutas
#define ACCEL_SCALE_FACTOR   2048.0f
#define GYRO_SCALE_FACTOR    16.4f // Fator de escala do giroscópio para ±250°/s

// --- Definições de Pinos e I2C para o MPU6050 (usando I2C0) ---
#define MPU6050_I2C_PORT    i2c0    // Instância I2C0
#define MPU6050_SDA_PIN     0       // Pino GP0 para SDA
#define MPU6050_SCL_PIN     1       // Pino GP1 para SCL
#define MPU6050_I2C_BAUDRATE 100000 // Frequência do I2C para o MPU6050

// --- Definições de Pinos e I2C para o Display OLED (usando I2C1) ---
#define OLED_I2C_PORT        i2c1
#define OLED_SDA_PIN         14
#define OLED_SCL_PIN         15
#define OLED_I2C_BAUDRATE    400000 // Frequência do I2C para o OLED (mais rápida é comum)

// --- Variáveis Globais para o OLED ---
struct render_area frame_area;
uint8_t ssd_buffer[ssd1306_buffer_length];


// --- Protótipos de Funções ---
// Funções do MPU6050
void mpu6050_init();
void mpu6050_read_raw_data(int16_t accel[3], int16_t gyro[3]);


// Funções do Display OLED
void init_oled();
void clear_oled_display();
void display_message_oled(const char *message, int line);
// --- Implementação das Funções do MPU6050 ---

void mpu6050_init() {
    i2c_init(MPU6050_I2C_PORT, MPU6050_I2C_BAUDRATE);
    gpio_set_function(MPU6050_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(MPU6050_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(MPU6050_SDA_PIN);
    gpio_pull_up(MPU6050_SCL_PIN);

    printf("I2C0 para MPU6050 configurado.\n");
    sleep_ms(100);

    // Acorda o MPU6050 e seleciona o clock interno
    uint8_t buf[2];
    buf[0] = MPU6050_PWR_MGMT_1; 
    buf[1] = 0x00; 
    
    int ret = i2c_write_blocking(MPU6050_I2C_PORT, MPU6050_ADDR, buf, 2, false);
    if (ret == PICO_ERROR_GENERIC) {
        printf("Erro ao acordar MPU6050! Verifique conexoes e endereco I2C.\n");
    } else {
        printf("MPU6050 acordado e inicializado com sucesso.\n");
    }
    sleep_ms(100);
}

void mpu6050_read_raw_data(int16_t accel[3], int16_t gyro[3]) {
    uint8_t buffer[14]; // Buffer para armazenar os 14 bytes de dados (Accel X,Y,Z, Temp, Gyro X,Y,Z)

    uint8_t reg_addr = MPU6050_ACCEL_XOUT_H; // Começa a ler do registrador do acelerômetro X
    // Envia o endereço do registrador e mantém a conexão I2C ativa (repeated start = true)
    int ret = i2c_write_blocking(MPU6050_I2C_PORT, MPU6050_ADDR, &reg_addr, 1, true); 
    if (ret == PICO_ERROR_GENERIC) {
        printf("Erro ao solicitar leitura de dados do MPU6050.\n");
        // Em caso de erro, zera os arrays para evitar dados inválidos
        memset(accel, 0, sizeof(int16_t)*3);
        memset(gyro, 0, sizeof(int16_t)*3);
        return;
    }
    
    // Lê os 14 bytes de dados do sensor
    ret = i2c_read_blocking(MPU6050_I2C_PORT, MPU6050_ADDR, buffer, 14, false);
    if (ret == PICO_ERROR_GENERIC) {
        printf("Erro ao ler dados do MPU6050.\n");
        memset(accel, 0, sizeof(int16_t)*3);
        memset(gyro, 0, sizeof(int16_t)*3);
        return;
    }

    // Combina os bytes alto e baixo para formar os valores de 16 bits
    accel[0] = (int16_t)((buffer[0] << 8) | buffer[1]);  // AccelX
    accel[1] = (int16_t)((buffer[2] << 8) | buffer[3]);  // AccelY
    accel[2] = (int16_t)((buffer[4] << 8) | buffer[5]);  // AccelZ

    gyro[0] = (int16_t)((buffer[8] << 8) | buffer[9]);   // GyroX
    gyro[1] = (int16_t)((buffer[10] << 8) | buffer[11]); // GyroY
    gyro[2] = (int16_t)((buffer[12] << 8) | buffer[13]); // GyroZ
}



// --- Implementação das Funções do Display OLED ---

void init_oled() {
    i2c_init(OLED_I2C_PORT, OLED_I2C_BAUDRATE);
    gpio_set_function(OLED_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(OLED_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_SDA_PIN);
    gpio_pull_up(OLED_SCL_PIN);

    ssd1306_init();
    frame_area.start_column = 0;
    frame_area.end_column = ssd1306_width - 1;
    frame_area.start_page = 0;
    frame_area.end_page = ssd1306_n_pages - 1;
    calculate_render_area_buffer_length(&frame_area);
    memset(ssd_buffer, 0, ssd1306_buffer_length);

    sleep_ms(100);
    printf("Display OLED inicializado no I2C1.\n");
}

// Limpa o buffer do display, mas não o renderiza na tela para otimização
void clear_oled_display() {
    memset(ssd_buffer, 0, ssd1306_buffer_length);
}

// Desenha uma mensagem no buffer do OLED em uma linha específica
void display_message_oled(const char *message, int line) {
    // A função ssd1306_draw_string espera um char *, então fazemos um cast para remover o 'const'
    // (idealmente, o protótipo da biblioteca ssd1306.h seria atualizado para const char*)
    ssd1306_draw_string(ssd_buffer, 5, line * 8, (char *)message); 
}




// --- Função Principal ---
int main() {
    stdio_init_all(); // Inicializa a comunicação serial via USB

    printf("Iniciando sistema de monitoramento de inclinacao e controle de servo...\n");

    // --- Inicialização de Periféricos ---
    // É uma boa prática inicializar o display primeiro para que ele possa mostrar mensagens de status/erro
    init_oled();      
    mpu6050_init();   // Inicializa o MPU6050 (acelerômetro/giroscópio)

    // --- Mensagem de Início no OLED ---
    clear_oled_display();
    display_message_oled("MPU6050", 0);
    display_message_oled("Pronto!", 2);
    render_on_display(ssd_buffer, &frame_area); // Exibe a mensagem na tela
    sleep_ms(2000); // Exibe a mensagem por 2 segundos
    clear_oled_display(); // Limpa para a primeira leitura
    char accelX_str[32];
    char accelY_str[32];
    char accelZ_str[32];
    char gyroX_str[32];
    char gyroY_str[32];
    char gyroZ_str[32];
    int16_t accel_data[3]; // Armazena os dados brutos do acelerômetro (X, Y, Z)
    int16_t gyro_data[3];  // Armazena os dados brutos do giroscópio (X, Y, Z)
    // --- Loop Principal do Programa ---
    while (true) {
        mpu6050_read_raw_data(accel_data, gyro_data);
        snprintf(accelX_str, sizeof(accelX_str), "Accel X:%d", accel_data[0]);
        snprintf(accelY_str, sizeof(accelY_str), "Accel Y:%d", accel_data[1]);
        snprintf(accelZ_str, sizeof(accelZ_str), "Accel Z:%d", accel_data[2]);
        display_message_oled(accelX_str, 0);
        display_message_oled(accelY_str, 1);
        display_message_oled(accelZ_str, 2);
        snprintf(gyroX_str, sizeof(gyroX_str), "Gyro  X:%d", gyro_data[0]);
        snprintf(gyroY_str, sizeof(gyroY_str), "Gyro  Y:%d", gyro_data[1]);
        snprintf(gyroZ_str, sizeof(gyroZ_str), "Gyro  Z:%d", gyro_data[2]);
        display_message_oled(gyroX_str, 5);
        display_message_oled(gyroY_str, 6);
        display_message_oled(gyroZ_str, 7);
        render_on_display(ssd_buffer, &frame_area);
        sleep_ms(1000);

    }
    return 0;
}