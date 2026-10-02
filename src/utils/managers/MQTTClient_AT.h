#ifndef MQTTCLIENT_AT_H
#define MQTTCLIENT_AT_H

#include <string>
#include <deque>
#include <utility>
#include <functional>
#include "ATCommandManager.h"

/**
 * Cliente MQTT do SIM7080G (comandos AT+SMCONF / SMCONN / SMPUB / SMSUB).
 *
 * O SIM7080G mantém uma única sessão MQTT. As mensagens recebidas chegam pelo
 * URC +SMSUB: "topic","message", são enfileiradas pelo handler de URC e entregues
 * ao callback em loop().
 */
class MQTTClient_AT {
public:
    using MessageCallback = std::function<void(const std::string&, const std::string&)>;

    static const size_t kMaxPublishLength = 1024; ///< <content length> 0-1024 (manual, AT+SMPUB)

    MQTTClient_AT(ATCommandManager &at);

    // ---------- Wrappers dos comandos AT ----------
    bool mqttSetConf(const std::string &tag, const std::string &value, bool quoteValue = true); ///< AT+SMCONF
    bool mqttSetUrl(const std::string &host, int port);                        ///< SMCONF "URL"
    bool mqttSetClientId(const std::string &clientID);                         ///< SMCONF "CLIENTID"
    bool mqttSetKeepAlive(int seconds);                                        ///< SMCONF "KEEPTIME"
    bool mqttSetCleanSession(bool clean);                                      ///< SMCONF "CLEANSS"
    bool mqttSetCredentials(const std::string &username, const std::string &password); ///< USERNAME/PASSWORD
    bool mqttSetWill(const std::string &topic, const std::string &msg,
                     int qos = 0, bool retain = false);                        ///< TOPIC/MESSAGE/QOS/RETAIN
    bool mqttSetSSL(int sslIndex,
                    const std::string &caName = "",
                    const std::string &certName = "");                         ///< AT+SMSSL
    bool mqttSetPublishHex(bool hex);                                          ///< AT+SMPUBHEX
    bool mqttConnect();                                                        ///< AT+SMCONN
    bool mqttDisconnect();                                                     ///< AT+SMDISC
    bool mqttPublish(const std::string &topic, const std::string &payload,
                     int qos = 0, bool retain = false);                        ///< AT+SMPUB
    bool mqttSubscribe(const std::string &topic, int qos = 0);                 ///< AT+SMSUB
    bool mqttUnsubscribe(const std::string &topic);                            ///< AT+SMUNSUB
    int  mqttState();                                                          ///< AT+SMSTATE? (1 on-line, 0 off-line, -1 erro)

    // ---------- API de alto nível ----------
    /** Registra o handler de URC e encerra sessão MQTT anterior, se houver. */
    bool begin();

    /**
     * Configura e conecta ao broker.
     * @param server "host", "host:porta", "tcp://host:porta" ou "ssl://host:porta"
     *               (porta padrão 1883, ou 8883 com criptografia / ssl://)
     * @param criptografia habilita MQTT sobre TLS (AT+SMSSL)
     */
    bool connect(const std::string &clientID,
                 const std::string &server,
                 int keepAlive = 60,
                 bool cleanSession = true,
                 const std::string &username = "",
                 const std::string &password = "",
                 bool criptografia = false);
    bool publish(const std::string &topic,
                 const std::string &payload,
                 int qos = 0,
                 bool retain = false);
    bool subscribe(const std::string &topic, int qos = 0);
    bool unsubscribe(const std::string &topic);
    bool disconnect();
    bool mqttStop() { return disconnect(); } ///< compatibilidade com a versão A76XX

    /** Define certificados (nomes no sistema de arquivos do modem) usados quando criptografia=true. */
    void setSSLCertificates(const std::string &caName,
                            const std::string &certName = "",
                            int sslIndex = 1);

    /** Processa URCs pendentes e entrega mensagens recebidas ao callback. Chame com frequência. */
    void loop();
    /** Consulta o estado real da sessão (AT+SMSTATE?). */
    bool connected();
    /** Último estado conhecido, sem enviar comando AT. */
    bool isConnectedCached() const { return connected_; }
    void onMessage(MessageCallback callback);

    /** Separa host e porta de uma URL de broker. */
    static bool parseServer(const std::string &server, bool tls,
                            std::string &host, int &port);
    /** Separa tópico e mensagem do URC +SMSUB: "topic","message". */
    static bool parseSubUrc(const std::string &line,
                            std::string &topic, std::string &payload);

private:
    ATCommandManager &at;
    bool connected_;
    std::string connectedServer;
    std::string caName_;
    std::string certName_;
    int sslIndex_;
    MessageCallback messageCallback;

    static const size_t kMaxQueue = 16;
    std::deque<std::pair<std::string, std::string>> pending_;
    SemaphoreHandle_t queueMutex_;

    void handleUrc(const std::string &line);
    bool parseIncoming();
};

#endif  // MQTTCLIENT_AT_H
