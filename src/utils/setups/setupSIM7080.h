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
 * @brief Configuração do modem.
 *
 * A biblioteca opera em LTE CAT-M e NB-IoT: tenta primeiro `firstMode` e, se não
 * conectar, tenta o outro modo. Em inicialização por chaves os campos omitidos
 * valem 0/nullptr, que significa "automático" / "padrão".
 *
 * Os ponteiros (APN, bandas...) são guardados para as reconexões automáticas:
 * use strings literais ou variáveis globais.
 */
struct ModemConfig {
    const char* apn;            ///< nullptr/"" = automático: usa o APN entregue pela rede (AT+CGNAPN). Preencha só para sobrescrever.
    const char* user;           ///< Usuário para autenticação (pode ser nullptr).
    const char* password;       ///< Senha para autenticação (pode ser nullptr).
    int rxPin;                  ///< GPIO para RX do modem.
    int txPin;                  ///< GPIO para TX do modem.
    int resetPin;               ///< GPIO para controle de reset/alimentação do modem, ou -1 se não usado.
    LogLevel logLevel;          ///< Nível de log: LOG_LEVEL_SIMPLE ou LOG_LEVEL_DEBUG
    RadioMode firstMode;        ///< Modo tentado primeiro: RADIO_CATM (padrão) ou RADIO_NBIOT.
    const char* catmBands;      ///< AT+CBANDCFG="CAT-M",<bandas> (ex.: "3,28"); nullptr = mantém as do módulo.
    const char* nbiotBands;     ///< AT+CBANDCFG="NB-IOT",<bandas> (ex.: "28"); nullptr = mantém as do módulo.
    uint32_t baudRate;          ///< Baud rate da UART: 0 = 115200
    uint32_t registerTimeoutMs; ///< Tempo máximo de registro em cada modo: 0 = 120000 ms.
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
 * @brief Inicializa o modem SIM7080G e conecta à rede.
 *
 * Tenta `config.firstMode` (CAT-M por padrão); se não registrar ou não ativar o
 * contexto de dados, tenta o outro modo (NB-IoT), e vice-versa.
 *
 * @param config Estrutura com as configurações de rede e pinos do modem.
 * @param fusoHorario Fuso em quartos de hora para o NTP (-12 = UTC-3).
 * @return true Se o modem foi inicializado e o contexto de dados está ativo.
 * @return false Se ocorreu falha após o número máximo de tentativas
 *               (o supervisor, se iniciado, continua tentando).
 */
bool setupSIM7080(const ModemConfig& config, int fusoHorario = -12);

/**
 * @brief Inicializa o cliente MQTT e realiza a conexão com o broker.
 *
 * A configuração é guardada: depois de uma troca de modo ou queda, o
 * supervisor reconecta o MQTT e refaz a inscrição sozinho. Se a rede estiver
 * fora do ar, apenas guarda a configuração e retorna false.
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

// ---------------------------------------------------------------------------
// Conexão autônoma (CAT-M <-> NB-IoT)
// ---------------------------------------------------------------------------

/**
 * @brief Um ciclo de supervisão da conexão (bloqueia durante uma reconexão).
 *
 * - Modem sem resposta: reinicializa a comunicação e reconecta.
 * - Registro ou contexto de dados perdidos por mais de 60 s: troca para o
 *   outro modo (CAT-M <-> NB-IoT); se ele também falhar, volta ao modo anterior.
 * - MQTT caído: reconecta; após 3 falhas seguidas com a rede "ok", troca de modo.
 * - 3 trocas de modo sem sucesso: reinicia o modem (AT+CFUN=1,1).
 *
 * Chame periodicamente no loop() ou use startModemSupervisor().
 * @return true se a rede (e o MQTT, quando configurado) está on-line.
 */
bool modemMaintain();

/**
 * @brief Cria uma task FreeRTOS que chama modemMaintain() a cada `intervalMs`.
 */
bool startModemSupervisor(uint32_t intervalMs = 30000,
                          uint32_t stackSize = 8192,
                          UBaseType_t priority = 1);

/// Pede para a task do supervisor encerrar (ela termina o ciclo em andamento).
void stopModemSupervisor();

/// Força a troca para o outro modo agora (CAT-M <-> NB-IoT), com reconexão do MQTT.
bool switchRadioMode();

/// Último estado conhecido do contexto de dados (sem enviar comando AT).
bool modemOnline();

/// Modo em uso na última conexão bem-sucedida (RADIO_UNKNOWN se nunca conectou).
RadioMode modemActiveMode();

/// Compatibilidade com o nome usado na versão A76XX da biblioteca.
inline bool setupSIMA76XX(const ModemConfig& config, int fusoHorario = -12) {
    return setupSIM7080(config, fusoHorario);
}

extern MQTTClient_AT mqtt;         ///< Cliente MQTT global.
extern ATCommandManager atManager; ///< Gerenciador de comandos AT global.
extern NetworkManager network;     ///< Gerenciador de rede global.
extern GNSSManager gnss;           ///< GNSS global.

/// Mutex recursivo que serializa as rotinas de (re)conexão (setup*, modemMaintain).
extern SemaphoreHandle_t g_modemMutex;

#endif // SIM7080_SETUP_H
