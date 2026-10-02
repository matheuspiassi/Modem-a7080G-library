/**
 * @file setupSIM7080.cpp
 * @brief Implementação da inicialização do modem SIM7080G, do cliente MQTT e do GNSS.
 */

#include "utils/setups/setupSIM7080.h"
#include "utils/managers/ATCommandManager.h"
#include "utils/managers/NetworkManager.h"
#include "utils/managers/MQTTClient_AT.h"
#include "utils/managers/GNSSManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Variável global para controlar o nível de log
LogLevel g_logLevel = LOG_LEVEL_SIMPLE;

String lastCommandID; ///< Armazena o ID do último comando recebido.
char lastCommandTime; ///< Armazena o timestamp do último comando recebido.

ATCommandManager atManager(Serial2, 20000); ///< Gerenciador de comandos AT (timeout 20s).
NetworkManager network(atManager);          ///< Gerenciador de rede do modem.
MQTTClient_AT mqtt(atManager);              ///< Cliente MQTT utilizando comandos AT.
GNSSManager gnss(atManager);                ///< GNSS do modem.

static const int kAppPdpIndex = 0; ///< Contexto de aplicação usado por MQTT/NTP (AT+CNACT)

//---------------------------------------------------------//

/**
 * @brief Inicializa o modem SIM7080G, configura rede e APN, ativa o contexto de dados e obtém IP.
 *
 * Fluxo:
 * - AT / ATE0 / AT+CMEE=2 / AT+CPIN?
 * - AT+CFUN=0 → AT+CNMP / AT+CMNB / AT+CGDCONT → AT+CFUN=1
 * - Aguarda registro (AT+CEREG?)
 * - AT+CNCFG + AT+CNACT (contexto de aplicação 0)
 * - NTP (AT+CNTP) e leitura do relógio (AT+CCLK?)
 *
 * @param config Estrutura com as configurações de rede e pinos do modem.
 * @return true Se inicializou e conectou à rede com sucesso.
 * @return false Se excedeu número máximo de tentativas.
 */
bool setupSIM7080(const ModemConfig &config, int fusoHorario)
{
    // Configurar nível de log
    g_logLevel = config.logLevel;

    const int maxRetries = 2;
    int retryCount = 0;
    const uint32_t baud = config.baudRate ? config.baudRate : 115200;

    // Configurar pino de reset/alimentação do modem quando ele foi informado.
    // Alguns projetos fazem o controle do PWRKEY fora desta biblioteca e usam -1.
    if (config.resetPin >= 0)
    {
        pinMode(config.resetPin, OUTPUT);
        digitalWrite(config.resetPin, HIGH);
    }
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (retryCount < maxRetries)
    {
        retryCount++;
        Serial.printf("=== Iniciando Modem SIM7080G (tentativa %d de %d) ===\n", retryCount, maxRetries);
        atManager.begin(baud, config.rxPin, config.txPin);
        Serial.println("UART do modem iniciada");

        // 0) Acordar o modem (auto-baud) e drenar URCs de boot (RDY, +CFUN, +CPIN, SMS Ready...)
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
                Serial.println("ERRO: modem não responde a AT → reiniciando...");
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
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
            {
                Serial.printf("✓ %d bytes de URCs drenados\n", drainedCount);
            }
        }

        // 1) Echo off + erros detalhados
        {
            std::string resp;
            if (!atManager.sendCommand("ATE0", "OK", resp, 10000))
            {
                Serial.println("ERRO: ATE0 falhou → reiniciando...");
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            atManager.sendCommand("AT+CMEE=2", "OK", resp, 5000);
        }

        // 2) SIM
        {
            std::string simStatus;
            bool simOk = false;
            for (int i = 0; i < 5 && !simOk; ++i)
            {
                simOk = network.checkSim(simStatus);
                if (!simOk)
                    vTaskDelay(pdMS_TO_TICKS(2000));
            }
            if (!simOk)
            {
                Serial.printf("ERRO: SIM não está pronto (%s) → reiniciando...\n", simStatus.c_str());
                continue;
            }
        }

        // 3) Modo de rede e APN com o rádio desligado (aplica no próximo attach)
        network.setFunctionality(0);
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (config.networkMode > 0)
            network.setNetworkMode(config.networkMode);
        if (config.lteMode > 0)
            network.setPreferredLteMode(config.lteMode);

        Serial.printf("APN: %s | Usuário: %s | Senha: %s\n",
                      config.apn ? config.apn : "N/A",
                      config.user ? config.user : "N/A",
                      config.password ? "***" : "N/A");

        if (config.apn && !network.setApnSession(config.apn))
        {
            Serial.println("ERRO: falha ao configurar APN");
            network.setFunctionality(1);
            continue;
        }

        const bool hasAuth = config.user && config.user[0] != '\0';
        if (hasAuth)
        {
            // AT+CGAUTH=<cid>,<auth>,<passwd>,<user>
            if (!network.setAuthentication(1, 3, config.password ? config.password : "", config.user))
            {
                Serial.println("AVISO: falha no AT+CGAUTH, seguindo com credenciais do AT+CNCFG");
            }
        }

        if (!network.setFunctionality(1))
        {
            Serial.println("ERRO: AT+CFUN=1 falhou → reiniciando...");
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(3000));

        int rssi = 0, ber = 0;
        network.getSignalQuality(rssi, ber);

        // 4) Registro de rede (CAT-M/NB-IoT podem demorar alguns minutos no primeiro attach)
        {
            int n = 0, stat = 0;
            bool ok = false;
            for (int i = 0; i < 15; ++i)
            {
                if (network.getRegistrationStatus(n, stat) && (stat == 1 || stat == 5))
                {
                    ok = true;
                    break;
                }
                if (stat == 3)
                {
                    Serial.println("AVISO: registro negado pela rede (stat=3)");
                }
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (!ok)
            {
                Serial.println("ERRO: não registrou na rede → reiniciando...");
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
        }
        network.getSignalQuality(rssi, ber);

        {
            std::string info;
            network.getSystemInfo(info);
        }

        // 5) Contexto de aplicação (usado por MQTT, NTP, HTTP...)
        if (!network.configurePDP(kAppPdpIndex,
                                  config.apn ? config.apn : "",
                                  hasAuth ? config.user : "",
                                  (hasAuth && config.password) ? config.password : ""))
        {
            Serial.println("ERRO: falha no AT+CNCFG");
            continue;
        }

        if (!network.activateContext(kAppPdpIndex))
        {
            Serial.println("ERRO: falha ao ativar contexto de dados (AT+CNACT)");
            continue;
        }

        // 6) Obtém IP
        {
            std::string ip;
            network.getPDPAddress(kAppPdpIndex, ip);
            Serial.printf("IP Obtido: %s\n", ip.c_str());
        }

        network.syncTimeNtp("a.st1.ntp.br", fusoHorario, 120000, kAppPdpIndex);
        // network.timeZoneUpdate();

        network.infoDateAndTime();
        Serial1.printf("SIN,%d\n", rssi);

        Serial.println("✓ REDE Modem SIM7080G pronto");
        return true;
    }

    Serial.println("ERRO: excedeu número máximo de tentativas de inicialização do modem");
    Serial.println("ERRO: Resetando modem");
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
 *
 * @param config Estrutura com as configurações e credenciais MQTT.
 * @return true Se a conexão e inscrição foram bem-sucedidas.
 * @return false Se falhar após o número máximo de tentativas.
 */
bool setupMqtt(const MqttConfig &config, bool criptografia)
{
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
    Serial.println("ERRO: Reiniciando Modem");
    Serial1.print("COMM,NCO\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    return false;
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
