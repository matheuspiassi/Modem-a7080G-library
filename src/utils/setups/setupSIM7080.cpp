/**
 * @file setupSIM7080.cpp
 * @brief Inicialização do modem SIM7080G (CAT-M / NB-IoT com troca automática),
 *        supervisão da conexão, cliente MQTT e GNSS.
 */

#include "utils/setups/setupSIM7080.h"
#include "utils/managers/ATCommandManager.h"
#include "utils/managers/NetworkManager.h"
#include "utils/managers/MQTTClient_AT.h"
#include "utils/managers/GNSSManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// Variável global para controlar o nível de log
LogLevel g_logLevel = LOG_LEVEL_SIMPLE;

String lastCommandID; ///< Armazena o ID do último comando recebido.
char lastCommandTime; ///< Armazena o timestamp do último comando recebido.

ATCommandManager atManager(Serial2, 20000); ///< Gerenciador de comandos AT (timeout 20s).
NetworkManager network(atManager);          ///< Gerenciador de rede do modem.
MQTTClient_AT mqtt(atManager);              ///< Cliente MQTT utilizando comandos AT.
GNSSManager gnss(atManager);                ///< GNSS do modem.

SemaphoreHandle_t g_modemMutex = nullptr;

static const int kAppPdpIndex = 0;                 ///< Contexto de aplicação usado por MQTT/NTP (AT+CNACT)
static const uint32_t kDefaultRegisterTimeoutMs = 120000;
static const uint32_t kRegisterPollMs = 5000;
static const uint32_t kLossGraceMs = 60000;        ///< Perda tolerada antes de trocar de modo
static const int kMqttFailuresBeforeSwitch = 3;
static const int kFailoversBeforeReboot = 3;

// ---------------- Estado da conexão ----------------
static ModemConfig s_cfg = {};
static bool s_cfgValid = false;
static MqttConfig s_mqttCfg;
static bool s_mqttValid = false;
static bool s_mqttTls = false;

static volatile RadioMode s_activeMode = RADIO_UNKNOWN;
static volatile bool s_online = false;
static uint32_t s_lossSince = 0;
static int s_mqttFailures = 0;
static int s_failoverFailures = 0;

static TaskHandle_t s_supervisorTask = nullptr;
static volatile bool s_supervisorStop = false;
static uint32_t s_supervisorIntervalMs = 30000;

// ---------------- Lock das rotinas de conexão ----------------
namespace
{
    class ModemLock
    {
    public:
        ModemLock()
        {
            if (!g_modemMutex)
                g_modemMutex = xSemaphoreCreateRecursiveMutex();
            locked_ = g_modemMutex && xSemaphoreTakeRecursive(g_modemMutex, portMAX_DELAY) == pdTRUE;
        }
        ~ModemLock()
        {
            if (locked_)
                xSemaphoreGiveRecursive(g_modemMutex);
        }

    private:
        bool locked_ = false;
    };
}

//---------------------------------------------------------//
// Etapas internas
//---------------------------------------------------------//

/**
 * Acorda o modem (auto-baud), drena URCs de boot e configura ATE0 / CMEE / SIM.
 */
static bool bootModem()
{
    Serial.println("Acordando modem e drenando URCs de boot...");
    std::string resp;
    bool alive = false;
    for (int i = 0; i < 10 && !alive; ++i)
    {
        alive = atManager.sendCommand("AT", "OK", resp, 2000);
        if (!alive)
            vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!alive)
    {
        Serial.println("ERRO: modem não responde a AT");
        return false;
    }

    unsigned long drainStart = millis();
    int drainedCount = 0;
    while (millis() - drainStart < 3000)
    {
        while (atManager.serial().available())
        {
            atManager.serial().read();
            drainedCount++;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (drainedCount > 0)
        Serial.printf("✓ %d bytes de URCs drenados\n", drainedCount);

    if (!atManager.sendCommand("ATE0", "OK", resp, 10000))
    {
        Serial.println("ERRO: ATE0 falhou");
        return false;
    }
    atManager.sendCommand("AT+CMEE=2", "OK", resp, 5000);

    std::string simStatus;
    for (int i = 0; i < 5; ++i)
    {
        if (network.checkSim(simStatus))
            return true;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    Serial.printf("ERRO: SIM não está pronto (%s)\n", simStatus.c_str());
    return false;
}

/**
 * Conecta em um modo específico:
 * CFUN=0 → CNMP=38 / CMNB / CBANDCFG / CGDCONT / CGAUTH → CFUN=1 → COPS=0 →
 * registro (CEREG) → APN (config ou CGNAPN) → CNCFG → CNACT.
 */
static bool connectRadio(RadioMode mode)
{
    Serial.printf("=== Conectando em %s ===\n", radioModeName(mode));
    s_online = false;

    // A sessão MQTT não sobrevive ao CFUN=0
    if (s_mqttValid)
        mqtt.disconnect();

    network.setFunctionality(0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (!network.setRadioMode(mode))
    {
        Serial.printf("ERRO: falha ao travar o rádio em %s\n", radioModeName(mode));
        network.setFunctionality(1);
        return false;
    }

    const char *bands = (mode == RADIO_CATM) ? s_cfg.catmBands : s_cfg.nbiotBands;
    if (bands && bands[0] != '\0' && !network.setBands(mode, bands))
        Serial.println("AVISO: falha no AT+CBANDCFG, seguindo com as bandas atuais do módulo");

    // APN vazio = a rede entrega o APN padrão do chip durante o attach
    const bool hasApn = s_cfg.apn && s_cfg.apn[0] != '\0';
    const bool hasAuth = s_cfg.user && s_cfg.user[0] != '\0';
    if (!network.setApnSession(hasApn ? s_cfg.apn : ""))
    {
        Serial.println("ERRO: falha no AT+CGDCONT");
        network.setFunctionality(1);
        return false;
    }
    if (hasAuth && !network.setAuthentication(1, 3, s_cfg.password ? s_cfg.password : "", s_cfg.user))
        Serial.println("AVISO: falha no AT+CGAUTH, seguindo com credenciais do AT+CNCFG");

    if (!network.setFunctionality(1))
    {
        Serial.println("ERRO: AT+CFUN=1 falhou");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    network.setOperatorAutomatic();

    // Registro
    const uint32_t timeout = s_cfg.registerTimeoutMs ? s_cfg.registerTimeoutMs : kDefaultRegisterTimeoutMs;
    const uint32_t start = millis();
    bool registered = false;
    while (millis() - start < timeout)
    {
        int n = 0, stat = 0;
        if (network.getRegistrationStatus(n, stat))
        {
            if (stat == 1 || stat == 5)
            {
                registered = true;
                break;
            }
            if (stat == 3)
            {
                Serial.printf("AVISO: registro negado em %s (stat=3)\n", radioModeName(mode));
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kRegisterPollMs));
    }
    if (!registered)
    {
        Serial.printf("ERRO: não registrou em %s\n", radioModeName(mode));
        return false;
    }

    RadioMode active = network.getActiveMode();
    if (active != RADIO_UNKNOWN && active != mode)
        Serial.printf("AVISO: AT+CPSI? indica %s\n", radioModeName(active));

    int rssi = 0, ber = 0;
    network.getSignalQuality(rssi, ber);

    // Contexto de aplicação (usado por MQTT, NTP, HTTP...)
    std::string apn = hasApn ? s_cfg.apn : "";
    if (!hasApn && !network.getNetworkApn(apn))
    {
        Serial.println("AVISO: rede não informou o APN (AT+CGNAPN), usando APN vazio");
        apn.clear();
    }
    Serial.printf("APN do contexto de dados: %s\n", apn.empty() ? "(vazio)" : apn.c_str());

    if (!network.configurePDP(kAppPdpIndex, apn,
                              hasAuth ? s_cfg.user : "",
                              (hasAuth && s_cfg.password) ? s_cfg.password : ""))
    {
        Serial.println("ERRO: falha no AT+CNCFG");
        return false;
    }
    if (!network.activateContext(kAppPdpIndex))
    {
        Serial.println("ERRO: falha ao ativar contexto de dados (AT+CNACT)");
        return false;
    }

    std::string ip;
    network.getPDPAddress(kAppPdpIndex, ip);
    Serial.printf("✓ %s conectado | IP %s | RSSI %d\n", radioModeName(mode), ip.c_str(), rssi);
    Serial1.printf("SIN,%d\n", rssi);

    s_activeMode = mode;
    s_online = true;
    s_lossSince = 0;
    return true;
}

/** Tenta `first`; se falhar, tenta o outro modo. */
static bool connectWithFallback(RadioMode first)
{
    if (connectRadio(first))
        return true;
    RadioMode second = otherRadioMode(first);
    Serial.printf("AVISO: %s falhou, tentando %s\n", radioModeName(first), radioModeName(second));
    return connectRadio(second);
}

static RadioMode configuredFirstMode()
{
    return s_cfg.firstMode == RADIO_NBIOT ? RADIO_NBIOT : RADIO_CATM;
}

/**
 * Reconecta após uma queda: começa pelo OUTRO modo (o atual acabou de cair)
 * e volta ao atual se o outro também falhar. Reconecta o MQTT em seguida.
 */
static bool recoverConnection()
{
    RadioMode first = (s_activeMode == RADIO_UNKNOWN) ? configuredFirstMode()
                                                      : otherRadioMode(s_activeMode);
    Serial.printf("=== Reconectando (começando por %s) ===\n", radioModeName(first));

    if (connectWithFallback(first))
    {
        s_failoverFailures = 0;
        s_mqttFailures = 0;
        if (s_mqttValid && !setupMqtt(s_mqttCfg, s_mqttTls))
            s_mqttFailures++;
        return true;
    }

    if (++s_failoverFailures >= kFailoversBeforeReboot)
    {
        Serial.println("AVISO: nenhum modo conectou após várias tentativas → reiniciando modem (AT+CFUN=1,1)");
        s_failoverFailures = 0;
        network.setFunctionality(1, 1);
        vTaskDelay(pdMS_TO_TICKS(10000));
        bootModem();
    }
    return false;
}

//---------------------------------------------------------//
// API pública
//---------------------------------------------------------//

/**
 * @brief Inicializa o modem SIM7080G e conecta em CAT-M ou NB-IoT.
 *
 * Fluxo:
 * - AT / ATE0 / AT+CMEE=2 / AT+CPIN?
 * - Modo preferido (config.firstMode): CFUN=0 → CNMP=38 / CMNB / CBANDCFG / CGDCONT → CFUN=1
 *   → COPS=0 → registro (CEREG) → APN (config ou CGNAPN) → CNCFG → CNACT
 * - Se falhar, o mesmo no outro modo
 * - NTP (AT+CNTP) e leitura do relógio (AT+CCLK?)
 */
bool setupSIM7080(const ModemConfig &config, int fusoHorario)
{
    ModemLock lock;

    g_logLevel = config.logLevel;
    s_cfg = config;
    s_cfgValid = true;

    const int maxRetries = 2;
    const uint32_t baud = config.baudRate ? config.baudRate : 115200;

    // Configurar pino de reset/alimentação do modem quando ele foi informado.
    // Alguns projetos fazem o controle do PWRKEY fora desta biblioteca e usam -1.
    if (config.resetPin >= 0)
    {
        pinMode(config.resetPin, OUTPUT);
        digitalWrite(config.resetPin, HIGH);
    }
    vTaskDelay(pdMS_TO_TICKS(10000));

    for (int retry = 1; retry <= maxRetries; ++retry)
    {
        Serial.printf("=== Iniciando Modem SIM7080G (tentativa %d de %d) ===\n", retry, maxRetries);
        atManager.begin(baud, config.rxPin, config.txPin);

        if (!bootModem())
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        Serial.printf("APN: %s | Usuário: %s | Primeiro modo: %s\n",
                      (config.apn && config.apn[0]) ? config.apn : "automático (rede)",
                      config.user ? config.user : "N/A",
                      radioModeName(configuredFirstMode()));

        if (!connectWithFallback(configuredFirstMode()))
        {
            Serial.println("ERRO: nem CAT-M nem NB-IoT conectaram → reiniciando...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        network.syncTimeNtp("a.st1.ntp.br", fusoHorario, 120000, kAppPdpIndex);
        network.infoDateAndTime();

        Serial.printf("✓ REDE Modem SIM7080G pronto (%s)\n", radioModeName(s_activeMode));
        return true;
    }

    Serial.println("ERRO: excedeu número máximo de tentativas de inicialização do modem");
    return false;
}

/**
 * @brief Inicializa o cliente MQTT, conecta ao broker e assina o tópico.
 *
 * Fluxo:
 * - Registra o handler de URC e encerra sessão anterior (AT+SMDISC).
 * - (TLS) Configura contexto SSL (AT+CSSLCFG) e AT+SMSSL.
 * - Configura AT+SMCONF (URL, CLIENTID, KEEPTIME, CLEANSS, USERNAME, PASSWORD) e conecta (AT+SMCONN).
 * - Inscreve no tópico (AT+SMSUB), se fornecido.
 * - Registra callback para processamento de mensagens (se fornecido).
 */
bool setupMqtt(const MqttConfig &config, bool criptografia)
{
    ModemLock lock;

    // Guarda a configuração para as reconexões automáticas
    if (&config != &s_mqttCfg)
        s_mqttCfg = config;
    s_mqttTls = criptografia;
    s_mqttValid = true;

    // Rede gerenciada por setupSIM7080() e fora do ar: só guarda a config,
    // o supervisor (modemMaintain) conecta quando a rede voltar.
    if (s_cfgValid && !s_online)
    {
        Serial.println("AVISO: rede fora do ar, MQTT será conectado pelo supervisor");
        return false;
    }

    const int maxRetries = 2;
    int retryCount = 0;

    const std::string server = config.server ? config.server : "";

    if (criptografia)
    {
        std::string host;
        int port = 0;
        MQTTClient_AT::parseServer(server, true, host, port);

        // O índice do AT+SMSSL segue o exemplo do manual (1). Configura os contextos 0 e 1
        // com TLS 1.2 para funcionar em qualquer interpretação do firmware.
        const int sslIndex = 1;
        network.configureSSL(true, 0, true, host);
        network.configureSSL(true, sslIndex, true, host);
        mqtt.setSSLCertificates(config.caCertName ? config.caCertName : "",
                                config.clientCertName ? config.clientCertName : "",
                                sslIndex);
    }

    while (retryCount < maxRetries)
    {
        retryCount++;
        Serial.printf("=== Iniciando MQTT (tentativa %d de %d) ===\n", retryCount, maxRetries);

        if (!mqtt.begin())
        {
            Serial.println("ERRO: início do MQTT falhou → reiniciando MQTT...");
            vTaskDelay(pdMS_TO_TICKS(1500));
            continue;
        }

        if (!mqtt.connect(config.clientId ? config.clientId : "",
                          server,
                          config.keepalive,
                          true,
                          config.username ? config.username : "",
                          config.password ? config.password : "",
                          criptografia))
        {
            Serial.println("ERRO: AT+SMCONN falhou → reiniciando MQTT...");
            // Contexto de dados pode ter caído: tenta reativar antes da nova tentativa
            if (!network.isContextActive(kAppPdpIndex))
                network.activateContext(kAppPdpIndex);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Subscribe apenas se o tópico foi fornecido
        if (config.subscribeTopic != nullptr && config.subscribeTopic[0] != '\0')
        {
            if (!mqtt.subscribe(config.subscribeTopic, 0))
            {
                Serial.println("ERRO: subscribe falhou → reiniciando MQTT...");
                mqtt.disconnect();
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }

        // Registra callback apenas se fornecido
        if (config.onMessageCallback)
        {
            mqtt.onMessage(config.onMessageCallback);
        }

        Serial1.print("COMM,CO\n");
        Serial.println("✓ MQTT conectado e inscrito");
        return true;
    }

    Serial.println("ERRO: excedeu número máximo de tentativas de MQTT");
    Serial1.print("COMM,NCO\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    return false;
}

bool modemMaintain()
{
    if (!s_cfgValid)
        return false;
    ModemLock lock;

    // 1) Modem responde?
    std::string resp;
    if (!atManager.sendCommand("AT", "OK", resp, 2000) &&
        !atManager.sendCommand("AT", "OK", resp, 2000))
    {
        Serial.println("AVISO: modem não responde, reinicializando comunicação...");
        s_online = false;
        if (!bootModem())
            return false;
        return recoverConnection();
    }

    // 2) Registro e contexto de dados no modo atual
    bool registered = network.isRegistered();
    bool dataOk = registered && network.isContextActive(kAppPdpIndex);
    if (registered && !dataOk)
    {
        Serial.println("AVISO: contexto de dados inativo, reativando...");
        dataOk = network.activateContext(kAppPdpIndex);
    }

    if (registered && dataOk)
    {
        s_online = true;
        s_lossSince = 0;

        // 3) MQTT
        if (s_mqttValid && !mqtt.connected())
        {
            Serial.println("AVISO: MQTT desconectado, reconectando...");
            if (setupMqtt(s_mqttCfg, s_mqttTls))
            {
                s_mqttFailures = 0;
            }
            else if (++s_mqttFailures >= kMqttFailuresBeforeSwitch)
            {
                Serial.printf("AVISO: MQTT não conecta em %s, trocando de modo\n", radioModeName(s_activeMode));
                s_mqttFailures = 0;
                return recoverConnection();
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    // 4) Conexão perdida: espera um pouco (o modem costuma voltar sozinho) e então troca de modo
    s_online = false;
    if (s_lossSince == 0)
    {
        s_lossSince = millis() ? millis() : 1;
        Serial.printf("AVISO: conexão %s perdida, aguardando %lu s antes de trocar de modo\n",
                      radioModeName(s_activeMode), (unsigned long)(kLossGraceMs / 1000));
        return false;
    }
    if (millis() - s_lossSince < kLossGraceMs)
        return false;

    s_lossSince = 0;
    return recoverConnection();
}

static void supervisorTask(void *)
{
    while (!s_supervisorStop)
    {
        vTaskDelay(pdMS_TO_TICKS(s_supervisorIntervalMs));
        if (s_supervisorStop)
            break;
        modemMaintain();
    }
    s_supervisorTask = nullptr;
    vTaskDelete(nullptr);
}

bool startModemSupervisor(uint32_t intervalMs, uint32_t stackSize, UBaseType_t priority)
{
    if (s_supervisorTask)
        return true;
    s_supervisorIntervalMs = intervalMs ? intervalMs : 30000;
    s_supervisorStop = false;
    BaseType_t ok = xTaskCreate(supervisorTask, "modemSupervisor", stackSize, nullptr,
                                priority, &s_supervisorTask);
    if (ok != pdPASS)
    {
        s_supervisorTask = nullptr;
        Serial.println("ERRO: não foi possível criar a task do supervisor do modem");
        return false;
    }
    return true;
}

void stopModemSupervisor()
{
    s_supervisorStop = true;
}

bool switchRadioMode()
{
    if (!s_cfgValid)
        return false;
    ModemLock lock;
    return recoverConnection();
}

bool modemOnline()
{
    return s_online;
}

RadioMode modemActiveMode()
{
    return s_activeMode;
}

/**
 * @brief Liga o GNSS e, opcionalmente, aguarda o primeiro fix.
 *
 * Observação: no SIM7080G o GNSS divide o RF com o LTE; o fix é mais rápido com a rede
 * ociosa. A primeira aquisição (cold start) pode levar alguns minutos com céu aberto.
 */
bool setupGNSS(uint32_t waitFixTimeoutMs)
{
    Serial.println("=== Iniciando GNSS ===");
    if (!gnss.powerOn())
    {
        Serial.println("ERRO: AT+CGNSPWR=1 falhou");
        return false;
    }

    if (waitFixTimeoutMs == 0)
    {
        Serial.println("✓ GNSS ligado");
        return true;
    }

    GnssInfo info;
    if (!gnss.waitForFix(info, waitFixTimeoutMs))
    {
        Serial.println("AVISO: GNSS ligado, mas sem fix dentro do tempo");
        return false;
    }

    Serial.printf("✓ GNSS fix: %.6f, %.6f (alt %.1f m, %d satélites)\n",
                  info.latitude, info.longitude, info.altitude, info.gnssSatsUsed);
    return true;
}
