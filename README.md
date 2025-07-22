# 🌡️ Estação Meteorológica IoT com BMP280 e AHT20 via Web Server
<p align="center"> Repositório dedicado ao sistema de estação meteorológica completa utilizando a placa BitDogLab com RP2040, que monitora temperatura, umidade, pressão atmosférica e altitude em tempo real, com servidor web integrado para visualização de dados, gráficos históricos e configuração remota de parâmetros.</p>

## 📋 Apresentação da tarefa
Para este trabalho foi necessário implementar uma estação meteorológica IoT completa que coleta dados de múltiplos sensores (AHT20 para temperatura/umidade e BMP280 para pressão/temperatura) e os disponibiliza através de um servidor web HTTP. O sistema oferece visualização em tempo real no display OLED, gráficos históricos via interface web, sistema de alarmes configuráveis com feedback visual (LED RGB e matriz de LEDs) e sonoro (buzzer), além de permitir configuração remota de limites e offsets através da interface web.

## 🎯 Objetivos
- Implementar monitoramento ambiental completo com sensores AHT20 e BMP280
- Estabelecer servidor web HTTP para acesso remoto aos dados
- Criar interface web responsiva com gráficos em tempo real usando Chart.js
- Implementar sistema de alarmes com thresholds configuráveis remotamente
- Exibir informações em tempo real no display OLED SSD1306 com múltiplas páginas
- Visualizar status através de matriz de LEDs 5x5 WS2812B
- Implementar feedback visual com LED RGB e sonoro com buzzer
- Permitir navegação entre páginas do display através do Botão A
- Implementar reset para bootloader através do Botão B
- Armazenar histórico de 50 pontos de dados para visualização de tendências
- Permitir calibração dos sensores através de offsets configuráveis

## 📚 Descrição do Projeto
Utilizou-se a placa BitDogLab com o microcontrolador RP2040 para criar uma estação meteorológica profissional IoT. O sistema coleta dados de temperatura e umidade através do sensor AHT20 conectado via I2C0, e pressão atmosférica/temperatura através do BMP280 também via I2C0. A altitude é calculada automaticamente baseada na pressão atmosférica. Os dados são exibidos em um display OLED SSD1306 conectado via I2C1.

O sistema estabelece conexão WiFi e cria um servidor web HTTP na porta 80. A interface web permite visualização em tempo real dos dados, gráficos históricos dos últimos 50 pontos, e configuração completa do sistema incluindo limites de alarme (mínimo/máximo para cada sensor) e offsets de calibração.

O sistema de alarmes monitora continuamente os valores dos sensores. Quando algum valor excede os limites configurados, o LED RGB pisca em vermelho (mantendo azul fixo), o buzzer emite beeps curtos e a matriz de LEDs exibe o dígito "1". Em operação normal, o LED RGB fica verde+azul e a matriz exibe "0".

O display OLED possui 3 páginas navegáveis: página principal com todos os dados, página de configuração mostrando os limites atuais, e página de status WiFi com IP do servidor. A navegação é feita através do Botão A, com feedback sonoro a cada mudança.

## 🚶 Integrantes do Projeto
Matheus Pereira Alves

## 📑 Funcionamento do Projeto
O sistema opera através de várias funções principais organizadas em um loop principal:

- **Inicialização**: Configuração de I2C (dual), GPIOs, matriz de LEDs, sensores AHT20/BMP280, WiFi e servidor HTTP
- **Leitura de Sensores**: Coleta periódica (1Hz) de temperatura/umidade (AHT20) e pressão/temperatura (BMP280)
- **Cálculo de Altitude**: Função calculate_altitude() baseada na pressão atmosférica e pressão ao nível do mar
- **Sistema de Alarmes**: Função check_alarms() que monitora thresholds e aciona LED RGB/buzzer/matriz
- **Servidor Web**: Callbacks HTTP que servem página HTML com JavaScript e endpoints API JSON
- **Interface Web**: Dashboard responsivo com gráficos Chart.js atualizados via AJAX a cada segundo
- **Histórico de Dados**: Buffer circular armazenando últimas 50 leituras para análise de tendências
- **Display OLED**: Função update_display() com 3 páginas de informação navegáveis
- **Controle por Botões**: Interrupções com debounce para navegação (A) e reset (B)
- **Feedback Visual**: LED RGB com códigos de cor e matriz 5x5 mostrando status numérico

## 👁️ Observações
- O sistema utiliza duas interfaces I2C separadas: I2C0 para sensores e I2C1 para display;
- A conectividade WiFi utiliza o protocolo WPA2/WPA (MIXED) para compatibilidade;
- Implementa debounce de 200ms nos botões através de interrupções GPIO;
- O servidor web serve tanto conteúdo estático (HTML/CSS/JS) quanto API REST JSON;
- A matriz de LEDs WS2812B utiliza PIO para comunicação eficiente;
- Endereço do BMP280 modificado para 0x77;
- Interface web totalmente responsiva, funcionando em desktop e mobile;
- Sistema de calibração permite ajuste fino dos sensores via offsets;
- Histórico de dados persiste apenas durante a execução;

## :camera: GIF mostrando o funcionamento do programa na placa Raspberry Pi Pico
<p align="center">
  <img src="images/trabalhose11.gif" alt="GIF" width="526px" />
</p>

## ▶️ Vídeo no youtube mostrando o funcionamento do programa na placa Raspberry Pi Pico
<p align="center">
    <a href="https://youtu.be/M1Xzc950wdg">Clique aqui para acessar o vídeo</a>
</p>
