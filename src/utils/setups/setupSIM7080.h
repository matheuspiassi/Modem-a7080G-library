/**
 * @file setupSIM7080.h
 * @brief Declarações das funções e variáveis globais para inicialização do modem SIM7080G, MQTT e GNSS.
 */

#ifndef SIM7080_SETUP_H
#define SIM7080_SETUP_H

#include "include.h"
#include "utils/managers/Logging.h"
#include <functional>

/**
 * @brief Estrutura para armazenar as credenciais de conexão de rede do modem.
 *
 * Campos novos ficam no final: em inicialização por chaves os campos omitidos
 * valem 0, que significa "manter o padrão do módulo".
 */
struct ModemConfig {
    const char* apn;           ///< Access Point Name para conexão de dados.
    const char* user;          ///< Usuário para autenticação (pode ser nullptr).
    const char* password;      ///< Senha para autenticação (pode ser nullptr).
    int rxPin;                 ///< GPIO para RX do modem.
    int txPin;                 ///< GPIO para TX do modem.
    int resetPin;              ///< GPIO para controle de reset/alimentação do modem, ou -1 se não usado.
    LogLevel logLevel;         ///< Nível de log: LOG_LEVEL_SIMPLE ou LOG_LEVEL_DEBUG
    int networkMode;           ///< AT+CNMP: 0 = não altera, 2 auto, 13 GSM, 38 LTE, 51 GSM+LTE
    int lteMode;               ///< AT+CMNB: 0 = não altera, 1 CAT-M, 2 NB-IoT, 3 CAT-M e NB-IoT
    uint32_t baudRate;         ///< Baud rate da UART: 0 = 115200
};

/**
 * @brief Estrutura para armazenar as credenciais e configurações MQTT.
 */
struct MqttConfig {
    const char* clientId;      ///< ID do cliente MQTT.
    const char* server;        ///< Broker: "host", "host:porta" ou "tcp://host:porta".
    int keepalive;             ///< Tempo de keepalive em segundos (manual: 60-180).
    const char* username;      ///< Usuário para autenticação MQTT (pode ser nullptr).
    const char* password;      ///< Senha para autenticação MQTT (pode ser nullptr).
    const char* subscribeTopic; ///< Tópico para inscrição (pode ser nullptr se não quiser inscrever).
    std::function<void(const std::string&, const std::string&)> onMessageCallback; ///< Callback para mensagens recebidas.
    const char* caCertName;    ///< (TLS) Nome do CA no modem, já convertido; nullptr = não valida o servidor.
    const char* clientCertName; ///< (TLS) Nome do certificado do cliente no modem (pode ser nullptr).
};

/**
 * @brief Inicializa o modem SIM7080G e configura a conexão de rede.
 *
 * @param config Estrutura com as configurações de rede e pinos do modem.
 * @param fusoHorario Fuso em quartos de hora para o NTP (-12 = UTC-3).
 * @return true Se o modem foi inicializado e o contexto de dados está ativo.
 * @return false Se ocorreu falha após o número máximo de tentativas.
 */
bool setupSIM7080(const ModemConfig& config, int fusoHorario = -12);

/**
 * @brief Inicializa o cliente MQTT e realiza a conexão com o broker.
 *
 * @param config Estrutura com as configurações e credenciais MQTT.
 * @param criptografia true para MQTT sobre TLS.
 * @return true Se a conexão MQTT foi estabelecida e inscrição no tópico concluída.
 * @return false Se falhar após o número máximo de tentativas.
 */
bool setupMqtt(const MqttConfig& config, bool criptografia = false);

/**
 * @brief Liga o GNSS e, opcionalmente, aguarda o primeiro fix.
 *
 * @param waitFixTimeoutMs 0 = apenas liga; >0 = aguarda fix até esse tempo.
 * @return true Se o GNSS foi ligado (e obteve fix, quando solicitado).
 */
bool setupGNSS(uint32_t waitFixTimeoutMs = 0);

/// Compatibilidade com o nome usado na versão A76XX da biblioteca.
inline bool setupSIMA76XX(const ModemConfig& config, int fusoHorario = -12) {
    return setupSIM7080(config, fusoHorario);
}

extern MQTTClient_AT mqtt;         ///< Cliente MQTT global.
extern ATCommandManager atManager; ///< Gerenciador de comandos AT global.
extern NetworkManager network;     ///< Gerenciador de rede global.
extern GNSSManager gnss;           ///< GNSS global.

extern SemaphoreHandle_t g_modemMutex;

#endif // SIM7080_SETUP_H
