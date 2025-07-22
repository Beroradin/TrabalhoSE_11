#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"
#include "pico/binary_info.h"
#include "hardware/pio.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "ws2818b.pio.h"
#include "lwip/tcp.h"
#include "lib/aht20.h"
#include "lib/bmp280.h"
#include "lib/ssd1306.h"
#include "lib/font.h"

// ==================== CONFIGURAÇÕES E DEFINIÇÕES ====================

// Configurações WiFi
#define WIFI_SSID "SSID"
#define WIFI_PASS "SENHA"

// I2C dos sensores
#define I2C_PORT i2c0
#define I2C_SDA 0
#define I2C_SCL 1

// I2C do Display
#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define DISPLAY_ADDR 0x3C
// Foi necessário alterar o endereço do bmp280.c para 0x77

// GPIOs
#define BOTAO_A 5
#define BOTAO_B 6
#define LED_PIN 7
#define BUZZER_PIN 21
#define RED_PIN    13
#define BLUE_PIN   12
#define GREEN_PIN  11

// Constantes
#define SEA_LEVEL_PRESSURE 101325.0
#define UPDATE_INTERVAL_MS 1000
#define MAX_DATA_POINTS 50
#define DEBOUNCE_DELAY_MS 200
#define SQUARE_SIZE 8
#define LED_COUNT 25

// ==================== ESTRUTURAS DE DADOS ====================

typedef struct {
    float temp_min;
    float temp_max;
    float humid_min;
    float humid_max;
    float press_min;
    float press_max;
    float temp_offset;
    float humid_offset;
    float press_offset;
} Config;

typedef struct {
    float temperature[MAX_DATA_POINTS];
    float humidity[MAX_DATA_POINTS];
    float pressure[MAX_DATA_POINTS];
    int index;
    int count;
} HistoricalData;

struct pixel_t {
    uint8_t G, R, B;
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t;

struct http_state {
    char response[8192];
    size_t len;
    size_t sent;
};

// ==================== VARIÁVEIS GLOBAIS ====================

Config config = {
    .temp_min = 10.0, .temp_max = 35.0,
    .humid_min = 20.0, .humid_max = 80.0,
    .press_min = 900.0, .press_max = 1100.0,
    .temp_offset = 0.0, .humid_offset = 0.0, .press_offset = 0.0
};

HistoricalData history = {0};
AHT20_Data sensor_data;
float bmp_temperature = 0;
float bmp_pressure = 0;
float altitude = 0;
int current_page = 0;
bool alarm_active = false;
ssd1306_t ssd;
int digit = 2;

// Variáveis para controle dos botões
volatile bool button_a_pressed = false;
volatile bool button_b_pressed = false;
volatile uint32_t last_button_time = 0;

// Variáveis da matriz de LEDs
npLED_t leds[LED_COUNT];
PIO np_pio;
uint sm;

// ==================== PROTÓTIPOS DE FUNÇÕES ====================

// Funções de inicialização
void init_gpio(void);
void init_i2c_sensors(void);
void init_i2c_display(void);
void init_sensors(void);
void init_wifi(void);
void init_led_matrix(void);

// Funções de controle da matriz de LEDs
void npSetLED(uint index, uint8_t r, uint8_t g, uint8_t b);
void npWrite(void);
int getIndex(int x, int y);
void npDisplayDigit(int digit);
void npClear(void);
void npInit(uint pin);

// Funções de controle de hardware
void set_rgb_led(uint8_t r, uint8_t g, uint8_t b);
void buzzer_beep(int duration_ms);

// Funções de processamento de dados
float calculate_altitude(float pressure);
void add_to_history(float temp, float humid, float press);
void check_alarms(void);

// Funções de interface
void update_display(void);
void handle_buttons(void);
void gpio_callback(uint gpio, uint32_t events);

// Funções do servidor HTTP
void start_http_server(void);
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);

// ==================== DADOS ESTÁTICOS ====================

// Matrizes para cada dígito 
const uint8_t digits[3][5][5][3] = {
    // Dígito 0
    {
        {{0, 0, 0}, {0, 0, 110}, {0, 0, 110}, {0, 0, 110}, {0, 0, 0}}, 
        {{0, 0, 110}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 110}},    
        {{0, 0, 110}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 110}},    
        {{0, 0, 110}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 110}},    
        {{0, 0, 0}, {0, 0, 110}, {0, 0, 110}, {0, 0, 110}, {0, 0, 0}}  
    },
    // Dígito 1
    {
        {{110, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {110, 0, 0}},
        {{0, 0, 0}, {110, 0, 0}, {0, 0, 0}, {110, 0, 0}, {0, 0, 0}},
        {{0, 0, 0}, {0, 0, 0}, {110, 0, 0}, {0, 0, 0}, {0, 0, 0}},
        {{0, 0, 0}, {110, 0, 0}, {0, 0, 0}, {110, 0, 0}, {0, 0, 0}},
        {{110, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {110, 0, 0}}
    },
    // Dígito 2
    {
        {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
        {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
        {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
        {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
        {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}} 
    }
};

// HTML da página web
const char HTML_HEADER[] = 
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>Estação Meteorológica</title>"
    "<style>"
    "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }"
    ".container { max-width: 1200px; margin: 0 auto; }"
    ".card { background: white; padding: 20px; margin: 10px 0; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }"
    ".sensor-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; }"
    ".sensor-card { text-align: center; padding: 20px; }"
    ".sensor-value { font-size: 36px; font-weight: bold; margin: 10px 0; }"
    ".sensor-label { color: #666; }"
    ".temp { color: #ff6b6b; }"
    ".humid { color: #4ecdc4; }"
    ".press { color: #45b7d1; }"
    ".charts { display: grid; grid-template-columns: 1fr; gap: 20px; margin-top: 20px; }"
    ".chart-container { height: 200px; position: relative; }"
    ".config-form { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }"
    ".form-group { display: flex; flex-direction: column; }"
    ".form-group label { margin-bottom: 5px; color: #333; }"
    ".form-group input { padding: 8px; border: 1px solid #ddd; border-radius: 4px; }"
    ".btn { background: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; }"
    ".btn:hover { background: #45a049; }"
    ".alert { background: #ff6b6b; color: white; padding: 10px; border-radius: 4px; display: none; }"
    ".alert.active { display: block; }"
    "@media (max-width: 768px) { .sensor-value { font-size: 28px; } }"
    "</style>"
    "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
    "</head><body>";

const char HTML_BODY[] = 
    "<div class='container'>"
    "<h1>🌤️ Estação Meteorológica BitDogLab - Trabalho SE 11 - MPA</h1>"
    "<div id='alert' class='alert'></div>"
    
    "<div class='card'>"
    "<h2>Dados Atuais</h2>"
    "<div class='sensor-grid'>"
    "<div class='sensor-card'>"
    "<div class='sensor-label'>Temperatura</div>"
    "<div class='sensor-value temp' id='temp'>--</div>"
    "<div class='sensor-label'>°C</div>"
    "</div>"
    "<div class='sensor-card'>"
    "<div class='sensor-label'>Umidade</div>"
    "<div class='sensor-value humid' id='humid'>--</div>"
    "<div class='sensor-label'>%</div>"
    "</div>"
    "<div class='sensor-card'>"
    "<div class='sensor-label'>Pressão</div>"
    "<div class='sensor-value press' id='press'>--</div>"
    "<div class='sensor-label'>hPa</div>"
    "</div>"
    "<div class='sensor-card'>"
    "<div class='sensor-label'>Altitude</div>"
    "<div class='sensor-value' id='alt'>--</div>"
    "<div class='sensor-label'>m</div>"
    "</div>"
    "</div></div>"
    
    "<div class='card'>"
    "<h2>Gráficos</h2>"
    "<div class='charts'>"
    "<div class='chart-container'><canvas id='tempChart'></canvas></div>"
    "<div class='chart-container'><canvas id='humidChart'></canvas></div>"
    "<div class='chart-container'><canvas id='pressChart'></canvas></div>"
    "</div></div>"
    
    "<div class='card'>"
    "<h2>Configurações</h2>"
    "<form id='configForm' class='config-form'>"
    "<div class='form-group'>"
    "<label>Temp. Mínima (°C)</label>"
    "<input type='number' id='temp_min' step='0.1'>"
    "</div>"
    "<div class='form-group'>"
    "<label>Temp. Máxima (°C)</label>"
    "<input type='number' id='temp_max' step='0.1'>"
    "</div>"
    "<div class='form-group'>"
    "<label>Umidade Mínima (%)</label>"
    "<input type='number' id='humid_min' step='0.1'>"
    "</div>"
    "<div class='form-group'>"
    "<label>Umidade Máxima (%)</label>"
    "<input type='number' id='humid_max' step='0.1'>"
    "</div>"
    "<div class='form-group'>"
    "<label>Pressão Mínima (hPa)</label>"
    "<input type='number' id='press_min' step='0.1'>"
    "</div>"
    "<div class='form-group'>"
    "<label>Pressão Máxima (hPa)</label>"
    "<input type='number' id='press_max' step='0.1'>"
    "</div>"
    "<div class='form-group'>"
    "<label>Offset Temp. (°C)</label>"
    "<input type='number' id='temp_offset' step='0.1'>"
    "</div>"
    "<div class='form-group'>"
    "<label>Offset Umidade (%)</label>"
    "<input type='number' id='humid_offset' step='0.1'>"
    "</div>"
    "<div class='form-group'>"
    "<label>Offset Pressão (hPa)</label>"
    "<input type='number' id='press_offset' step='0.1'>"
    "</div>"
    "</form>"
    "<button class='btn' onclick='saveConfig()'>Salvar Configurações</button>"
    "</div></div>";

const char HTML_SCRIPT[] = 
    "<script>"
    "let tempChart, humidChart, pressChart;"
    "let tempData = [], humidData = [], pressData = [];"
    "let labels = [];"
    
    "function initCharts() {"
    "  const chartOptions = {"
    "    responsive: true,"
    "    maintainAspectRatio: false,"
    "    scales: {"
    "      x: { display: false },"
    "      y: { beginAtZero: false }"
    "    },"
    "    animation: { duration: 0 }"
    "  };"
    
    "  tempChart = new Chart(document.getElementById('tempChart'), {"
    "    type: 'line',"
    "    data: {"
    "      labels: labels,"
    "      datasets: [{"
    "        label: 'Temperatura (°C)',"
    "        data: tempData,"
    "        borderColor: '#ff6b6b',"
    "        tension: 0.1"
    "      }]"
    "    },"
    "    options: chartOptions"
    "  });"
    
    "  humidChart = new Chart(document.getElementById('humidChart'), {"
    "    type: 'line',"
    "    data: {"
    "      labels: labels,"
    "      datasets: [{"
    "        label: 'Umidade (%)',"
    "        data: humidData,"
    "        borderColor: '#4ecdc4',"
    "        tension: 0.1"
    "      }]"
    "    },"
    "    options: chartOptions"
    "  });"
    
    "  pressChart = new Chart(document.getElementById('pressChart'), {"
    "    type: 'line',"
    "    data: {"
    "      labels: labels,"
    "      datasets: [{"
    "        label: 'Pressão (hPa)',"
    "        data: pressData,"
    "        borderColor: '#45b7d1',"
    "        tension: 0.1"
    "      }]"
    "    },"
    "    options: chartOptions"
    "  });"
    "}"
    
    "function updateData() {"
    "  fetch('/api/data').then(r => r.json()).then(data => {"
    "    document.getElementById('temp').textContent = data.temperature.toFixed(1);"
    "    document.getElementById('humid').textContent = data.humidity.toFixed(1);"
    "    document.getElementById('press').textContent = data.pressure.toFixed(1);"
    "    document.getElementById('alt').textContent = data.altitude.toFixed(1);"
    
    "    if (data.history) {"
    "      tempData = data.history.temperature;"
    "      humidData = data.history.humidity;"
    "      pressData = data.history.pressure;"
    "      labels = Array(tempData.length).fill('');"
    
    "      tempChart.data.labels = labels;"
    "      tempChart.data.datasets[0].data = tempData;"
    "      tempChart.update();"
    
    "      humidChart.data.labels = labels;"
    "      humidChart.data.datasets[0].data = humidData;"
    "      humidChart.update();"
    
    "      pressChart.data.labels = labels;"
    "      pressChart.data.datasets[0].data = pressData;"
    "      pressChart.update();"
    "    }"
    
    "    if (data.alert) {"
    "      document.getElementById('alert').textContent = data.alert;"
    "      document.getElementById('alert').classList.add('active');"
    "    } else {"
    "      document.getElementById('alert').classList.remove('active');"
    "    }"
    "  });"
    "}"
    
    "function loadConfig() {"
    "  fetch('/api/config').then(r => r.json()).then(data => {"
    "    Object.keys(data).forEach(key => {"
    "      const el = document.getElementById(key);"
    "      if (el) el.value = data[key];"
    "    });"
    "  });"
    "}"
    
    "function saveConfig() {"
    "  const config = {};"
    "  ['temp_min', 'temp_max', 'humid_min', 'humid_max', 'press_min', 'press_max',"
    "   'temp_offset', 'humid_offset', 'press_offset'].forEach(id => {"
    "    config[id] = parseFloat(document.getElementById(id).value);"
    "  });"
    
    "  fetch('/api/config', {"
    "    method: 'POST',"
    "    headers: { 'Content-Type': 'application/json' },"
    "    body: JSON.stringify(config)"
    "  }).then(() => alert('Configurações salvas!'));"
    "}"
    
    "window.onload = () => {"
    "  initCharts();"
    "  loadConfig();"
    "  updateData();"
    "  setInterval(updateData, 1000);"
    "};"
    "</script></body></html>";

// ==================== FUNÇÃO PRINCIPAL ====================

int main() {
    stdio_init_all();
    sleep_ms(2000);
    
    // Inicializações em ordem
    init_led_matrix();
    init_gpio();
    init_i2c_display();
    init_i2c_sensors();
    init_sensors();
    init_wifi();
    
    // Calibração do BMP280
    struct bmp280_calib_param bmp_params;
    bmp280_get_calib_params(I2C_PORT, &bmp_params);
    
    // Feedback de inicialização
    buzzer_beep(200);
    set_rgb_led(0, 0, 1);  // LED azul durante inicialização
    sleep_ms(500);
    
    // Loop principal
    uint32_t last_update = 0;
    int32_t raw_temp_bmp, raw_pressure;
    
    while (1) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        handle_buttons();
        
        if (now - last_update >= UPDATE_INTERVAL_MS) {
            last_update = now;
            
            // Lê sensores
            if (aht20_read(I2C_PORT, &sensor_data)) {
                bmp280_read_raw(I2C_PORT, &raw_temp_bmp, &raw_pressure);
                bmp_temperature = bmp280_convert_temp(raw_temp_bmp, &bmp_params) / 100.0;
                bmp_pressure = bmp280_convert_pressure(raw_pressure, raw_temp_bmp, &bmp_params) / 100.0;
                altitude = calculate_altitude(bmp_pressure * 100);
                
                // Processa dados
                add_to_history(
                    sensor_data.temperature + config.temp_offset,
                    sensor_data.humidity + config.humid_offset,
                    bmp_pressure + config.press_offset
                );
                
                check_alarms();
                update_display();
                npDisplayDigit(digit);
                
                // Debug
                printf("T=%.1f°C U=%.1f%% P=%.1fhPa A=%.1fm\n",
                    sensor_data.temperature, sensor_data.humidity, 
                    bmp_pressure, altitude);
            }
        }
        
        // Processa rede
        cyw43_arch_poll();
        sleep_ms(10);
    }
    
    cyw43_arch_deinit();
    return 0;
}

// ==================== IMPLEMENTAÇÃO DAS FUNÇÕES ====================

// ---------- Funções de Inicialização ----------

void init_gpio(void) {
    // Inicializa botões
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);

    gpio_set_irq_enabled(BOTAO_A, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BOTAO_B, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_callback(gpio_callback);
    irq_set_enabled(IO_IRQ_BANK0, true);
    
    // Inicializa buzzer
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    
    // Inicializa LED RGB
    gpio_init(RED_PIN);
    gpio_set_dir(RED_PIN, GPIO_OUT);
    gpio_init(GREEN_PIN);
    gpio_set_dir(GREEN_PIN, GPIO_OUT);
    gpio_init(BLUE_PIN);
    gpio_set_dir(BLUE_PIN, GPIO_OUT);
}

void init_i2c_display(void) {
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);
    
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, DISPLAY_ADDR, I2C_PORT_DISP);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Iniciando...", 25, 25);
    ssd1306_send_data(&ssd);
}

void init_i2c_sensors(void) {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
}

void init_sensors(void) {
    bmp280_init(I2C_PORT);
    aht20_reset(I2C_PORT);
    aht20_init(I2C_PORT);
}

void init_wifi(void) {
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Conectando WiFi", 0, 20);
    ssd1306_send_data(&ssd);
    
    if (cyw43_arch_init()) {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "ERRO INIT", 15, 25);
        ssd1306_send_data(&ssd);
        return;
    }
    
    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_MIXED_PSK, 10000)) {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "SEM REDE", 15, 25);
        ssd1306_send_data(&ssd);
        sleep_ms(2000);
    } else {
        start_http_server();
        
        uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
        char ip_str[24];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi OK!", 35, 20);
        ssd1306_draw_string(&ssd, ip_str, 20, 35);
        ssd1306_send_data(&ssd);
        printf("Conectado: %s\n", ip_str);
        sleep_ms(2000);
    }
}

void init_led_matrix(void) {
    npInit(LED_PIN);
}

// ---------- Funções da Matriz de LEDs ----------

void npSetLED(uint index, uint8_t r, uint8_t g, uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
}

void npWrite(void) {
    for (uint i = 0; i < LED_COUNT; i++) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100);
}

int getIndex(int x, int y) {
    if (y % 2 == 0) {
        return 24 - (y * 5 + x);
    } else {
        return 24 - (y * 5 + (4 - x));
    }
}

void npDisplayDigit(int digit) {
    for (int coluna = 0; coluna < 5; coluna++) {
        for (int linha = 0; linha < 5; linha++) {
            int posicao = getIndex(linha, coluna);
            npSetLED(
                posicao,
                digits[digit][coluna][linha][0],  // R
                digits[digit][coluna][linha][1],  // G
                digits[digit][coluna][linha][2]   // B
            );
        }
    }
    npWrite();
}

void npClear(void) {
   digit = 2;  // Vai teia
   npDisplayDigit(digit);
}

void npInit(uint pin) {
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;
    sm = pio_claim_unused_sm(np_pio, true);
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
    npClear();
}

// ---------- Funções de Controle de Hardware ----------

void set_rgb_led(uint8_t r, uint8_t g, uint8_t b) {
    gpio_put(RED_PIN, r);
    gpio_put(GREEN_PIN, g);
    gpio_put(BLUE_PIN, b);
}

void buzzer_beep(int duration_ms) {
    if (duration_ms > 0 && duration_ms < 1000) {
        gpio_put(BUZZER_PIN, 1);
        sleep_ms(duration_ms);
        gpio_put(BUZZER_PIN, 0);
    }
}

// ---------- Funções de Processamento de Dados ----------

float calculate_altitude(float pressure) {
    return 44330.0 * (1.0 - pow(pressure / SEA_LEVEL_PRESSURE, 0.1903));
}

void add_to_history(float temp, float humid, float press) {
    history.temperature[history.index] = temp;
    history.humidity[history.index] = humid;
    history.pressure[history.index] = press;
    
    history.index = (history.index + 1) % MAX_DATA_POINTS;
    if (history.count < MAX_DATA_POINTS) {
        history.count++;
    }
}

void check_alarms(void) {
    bool temp_alarm = (sensor_data.temperature < config.temp_min || 
                      sensor_data.temperature > config.temp_max);
    bool humid_alarm = (sensor_data.humidity < config.humid_min || 
                       sensor_data.humidity > config.humid_max);
    bool press_alarm = (bmp_pressure < config.press_min || 
                       bmp_pressure > config.press_max);
    
    alarm_active = temp_alarm || humid_alarm || press_alarm;
    
    if (alarm_active) {
        static bool led_state = false;
        led_state = !led_state;
        
        if (led_state) {
            set_rgb_led(1, 0, 1);  // Vermelho + azul ligados
            digit = 1;
        } else {
            set_rgb_led(0, 0, 1);  // Só azul ligado (vermelho pisca)
        }
        
        if (led_state) {
            buzzer_beep(100);
        }
    } else {
        set_rgb_led(0, 1, 1);  // Verde + azul fixo (operação normal)
        digit = 0;
    }
}

// ---------- Funções de Interface ----------

void update_display(void) {
    char str[32];
    
    ssd1306_fill(&ssd, false);
    
    switch (current_page) {
        case 0:  // Página principal
            ssd1306_draw_string(&ssd, "ESTACAO", 20, 0);
            ssd1306_line(&ssd, 0, 10, 127, 10, true);
            
            sprintf(str, "Temp: %.1fC", sensor_data.temperature + config.temp_offset);
            ssd1306_draw_string(&ssd, str, 0, 15);
            
            sprintf(str, "Umid: %.1f%%", sensor_data.humidity + config.humid_offset);
            ssd1306_draw_string(&ssd, str, 0, 25);
            
            sprintf(str, "Pres: %.0fhPa", bmp_pressure + config.press_offset);
            ssd1306_draw_string(&ssd, str, 0, 35);
            
            sprintf(str, "Alt: %.0fm", altitude);
            ssd1306_draw_string(&ssd, str, 0, 45);
            
            if (alarm_active) {
                ssd1306_draw_string(&ssd, "! ALARME !", 30, 55);
            }
            break;
            
        case 1:  // Página de limites
            ssd1306_draw_string(&ssd, "LIMITES CONFIG", 15, 0);
            ssd1306_line(&ssd, 0, 10, 127, 10, true);
            
            sprintf(str, "T: %.0f-%.0fC", config.temp_min, config.temp_max);
            ssd1306_draw_string(&ssd, str, 0, 15);
            
            sprintf(str, "U: %.0f-%.0f%%", config.humid_min, config.humid_max);
            ssd1306_draw_string(&ssd, str, 0, 25);
            
            sprintf(str, "P: %.0f-%.0f", config.press_min, config.press_max);
            ssd1306_draw_string(&ssd, str, 0, 35);
            
            ssd1306_draw_string(&ssd, "Botao A: Voltar", 0, 55);
            break;
            
        case 2:  // Página de status WiFi
            ssd1306_draw_string(&ssd, "STATUS WIFI", 25, 0);
            ssd1306_line(&ssd, 0, 10, 127, 10, true);
            
            if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP) {
                ssd1306_draw_string(&ssd, "Conectado", 30, 20);
                
                uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
                sprintf(str, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
                ssd1306_draw_string(&ssd, str, 15, 35);
            } else {
                ssd1306_draw_string(&ssd, "Desconectado", 20, 25);
            }
            break;
    }
    
    ssd1306_send_data(&ssd);
}

void gpio_callback(uint gpio, uint32_t events) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    if (now - last_button_time < DEBOUNCE_DELAY_MS) {
        return;
    }
    last_button_time = now;

    if (gpio == BOTAO_A) {
        button_a_pressed = true;
    } else if (gpio == BOTAO_B) {
        button_b_pressed = true;
    }
}

void handle_buttons(void) {
    if (button_a_pressed) {
        button_a_pressed = false;
        
        current_page = (current_page + 1) % 3;
        buzzer_beep(50);
        
        printf("Página alterada para: %d\n", current_page);
    }
    
    if (button_b_pressed) {
        button_b_pressed = false;
        
        buzzer_beep(100);
        set_rgb_led(1, 1, 0);
        sleep_ms(500);
        
        reset_usb_boot(0, 0);
    }
}

// ---------- Funções do Servidor HTTP ----------

static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len) {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs) {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;

    if (strstr(req, "GET /api/data")) {
        // Prepara dados JSON
        char json[2048];
        char history_temp[512] = "[";
        char history_humid[512] = "[";
        char history_press[512] = "[";
        
        // Monta arrays de histórico
        for (int i = 0; i < history.count; i++) {
            int idx = (history.index - history.count + i + MAX_DATA_POINTS) % MAX_DATA_POINTS;
            char val[16];
            
            sprintf(val, "%.1f%s", history.temperature[idx], (i < history.count-1) ? "," : "");
            strcat(history_temp, val);
            
            sprintf(val, "%.1f%s", history.humidity[idx], (i < history.count-1) ? "," : "");
            strcat(history_humid, val);
            
            sprintf(val, "%.1f%s", history.pressure[idx], (i < history.count-1) ? "," : "");
            strcat(history_press, val);
        }
        strcat(history_temp, "]");
        strcat(history_humid, "]");
        strcat(history_press, "]");
        
        // Monta resposta JSON completa
        snprintf(json, sizeof(json),
            "{"
            "\"temperature\":%.2f,"
            "\"humidity\":%.2f,"
            "\"pressure\":%.2f,"
            "\"altitude\":%.2f,"
            "\"history\":{"
            "\"temperature\":%s,"
            "\"humidity\":%s,"
            "\"pressure\":%s"
            "}%s"
            "}",
            sensor_data.temperature + config.temp_offset,
            sensor_data.humidity + config.humid_offset,
            bmp_pressure + config.press_offset,
            altitude,
            history_temp,
            history_humid,
            history_press,
            alarm_active ? ",\"alert\":\"Valores fora dos limites!\"" : ""
        );
        
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            (int)strlen(json), json);
            
    } else if (strstr(req, "GET /api/config")) {
        // Retorna configurações atuais
        char json[512];
        snprintf(json, sizeof(json),
            "{"
            "\"temp_min\":%.1f,\"temp_max\":%.1f,"
            "\"humid_min\":%.1f,\"humid_max\":%.1f,"
            "\"press_min\":%.1f,\"press_max\":%.1f,"
            "\"temp_offset\":%.1f,\"humid_offset\":%.1f,\"press_offset\":%.1f"
            "}",
            config.temp_min, config.temp_max,
            config.humid_min, config.humid_max,
            config.press_min, config.press_max,
            config.temp_offset, config.humid_offset, config.press_offset
        );
        
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            (int)strlen(json), json);
            
    } else if (strstr(req, "POST /api/config")) {
        // Processa nova configuração
        char *body = strstr(req, "\r\n\r\n");
        if (body) {
            body += 4;
            
            // Parse JSON
            sscanf(body, 
                "{\"temp_min\":%f,\"temp_max\":%f,"
                "\"humid_min\":%f,\"humid_max\":%f,"
                "\"press_min\":%f,\"press_max\":%f,"
                "\"temp_offset\":%f,\"humid_offset\":%f,\"press_offset\":%f",
                &config.temp_min, &config.temp_max,
                &config.humid_min, &config.humid_max,
                &config.press_min, &config.press_max,
                &config.temp_offset, &config.humid_offset, &config.press_offset
            );
            
            buzzer_beep(50);  // Feedback sonoro
        }
        
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n"
            "\r\n"
            "OK");
            
    } else {
        // Página principal HTML
        int html_len = strlen(HTML_HEADER) + strlen(HTML_BODY) + strlen(HTML_SCRIPT);
        
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s%s%s",
            html_len, HTML_HEADER, HTML_BODY, HTML_SCRIPT);
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);
    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    pbuf_free(p);
    return ERR_OK;
}

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

void start_http_server(void) {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        printf("Erro ao criar PCB TCP\n");
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP iniciado na porta 80\n");
}