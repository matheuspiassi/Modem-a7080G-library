#include "MQTTClient_AT.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ----------------- Classe -----------------
MQTTClient_AT::MQTTClient_AT(ATCommandManager &at)
    : at(at), connected_(false), sslIndex_(1)
{
    queueMutex_ = xSemaphoreCreateMutex();
}

// ============================================================
// Wrappers dos comandos AT
// ============================================================

bool MQTTClient_AT::mqttSetConf(const std::string &tag, const std::string &value, bool quoteValue)
{
    std::string cmd = "AT+SMCONF=\"" + tag + "\",";
    cmd += quoteValue ? ("\"" + value + "\"") : value;
    std::string resp;
    if (!at.sendCommand(cmd, "OK", resp, 5000))
    {
        LOG_ERROR("MQTT", "AT+SMCONF %s failed: %s", tag.c_str(), resp.c_str());
        return false;
    }
    return true;
}

bool MQTTClient_AT::mqttSetUrl(const std::string &host, int port)
{
    // Formato do manual: AT+SMCONF="URL","host","port"
    return mqttSetConf("URL", host + "\",\"" + std::to_string(port));
}

bool MQTTClient_AT::mqttSetClientId(const std::string &clientID)
{
    return mqttSetConf("CLIENTID", clientID);
}

bool MQTTClient_AT::mqttSetKeepAlive(int seconds)
{
    if (mqttSetConf("KEEPTIME", std::to_string(seconds), false))
        return true;

    // Manual: faixa 60-180 s. Algumas versões de firmware rejeitam valores fora dela.
    int clamped = seconds < 60 ? 60 : (seconds > 180 ? 180 : seconds);
    if (clamped == seconds)
        return false;
    LOG_WARN("MQTT", "KEEPTIME %d rejected, using %d", seconds, clamped);
    return mqttSetConf("KEEPTIME", std::to_string(clamped), false);
}

bool MQTTClient_AT::mqttSetCleanSession(bool clean)
{
    return mqttSetConf("CLEANSS", clean ? "1" : "0", false);
}

bool MQTTClient_AT::mqttSetCredentials(const std::string &username, const std::string &password)
{
    if (!mqttSetConf("USERNAME", username))
        return false;
    return mqttSetConf("PASSWORD", password);
}

bool MQTTClient_AT::mqttSetWill(const std::string &topic, const std::string &msg, int qos, bool retain)
{
    return mqttSetConf("TOPIC", topic) &&
           mqttSetConf("MESSAGE", msg) &&
           mqttSetConf("QOS", std::to_string(qos), false) &&
           mqttSetConf("RETAIN", retain ? "1" : "0", false);
}

bool MQTTClient_AT::mqttSetSSL(int sslIndex, const std::string &caName, const std::string &certName)
{
    std::string cmd = "AT+SMSSL=" + std::to_string(sslIndex) +
                      ",\"" + caName + "\",\"" + certName + "\"";
    std::string resp;
    if (!at.sendCommand(cmd, "OK", resp, 5000))
    {
        LOG_ERROR("MQTT", "AT+SMSSL failed: %s", resp.c_str());
        return false;
    }
    return true;
}

bool MQTTClient_AT::mqttSetPublishHex(bool hex)
{
    std::string resp;
    return at.sendCommand(std::string("AT+SMPUBHEX=") + (hex ? "1" : "0"), "OK", resp, 5000);
}

bool MQTTClient_AT::mqttConnect()
{
    LOG_INFO("MQTT", "Connecting (AT+SMCONN)...");
    std::string resp;
    // A conexão (DNS + TCP/TLS + CONNACK) pode demorar em CAT-M/NB-IoT
    if (!at.sendCommand("AT+SMCONN", "OK", resp, 90000))
    {
        LOG_ERROR("MQTT", "AT+SMCONN failed: %s", resp.c_str());
        connected_ = false;
        return false;
    }
    connected_ = true;
    LOG_INFO("MQTT", "Connected");
    return true;
}

bool MQTTClient_AT::mqttDisconnect()
{
    std::string resp;
    bool ok = at.sendCommand("AT+SMDISC", "OK", resp, 15000);
    connected_ = false;
    return ok;
}

bool MQTTClient_AT::mqttPublish(const std::string &topic, const std::string &payload, int qos, bool retain)
{
    if (payload.size() > kMaxPublishLength)
    {
        LOG_ERROR("MQTT", "Payload too large (%u bytes, max %u)",
                  (unsigned)payload.size(), (unsigned)kMaxPublishLength);
        return false;
    }

    // AT+SMPUB="topic",<len>,<qos>,<retain>  ->  ">"  ->  <len bytes>  ->  OK
    std::string cmd = "AT+SMPUB=\"" + topic + "\"," + std::to_string(payload.size()) + "," +
                      std::to_string(qos) + "," + (retain ? "1" : "0");
    std::string resp;
    if (!at.sendCommandWithData(cmd, ">", payload, "OK", resp, 20000))
    {
        LOG_ERROR("MQTT", "Publish to %s failed: %s", topic.c_str(), resp.c_str());
        return false;
    }
    LOG_DEBUG("MQTT", "Published %u bytes to %s", (unsigned)payload.size(), topic.c_str());
    return true;
}

bool MQTTClient_AT::mqttSubscribe(const std::string &topic, int qos)
{
    std::string cmd = "AT+SMSUB=\"" + topic + "\"," + std::to_string(qos);
    std::string resp;
    if (!at.sendCommand(cmd, "OK", resp, 30000))
    {
        LOG_ERROR("MQTT", "Subscribe to %s failed: %s", topic.c_str(), resp.c_str());
        return false;
    }
    LOG_INFO("MQTT", "Subscribed to %s (qos %d)", topic.c_str(), qos);
    return true;
}

bool MQTTClient_AT::mqttUnsubscribe(const std::string &topic)
{
    std::string cmd = "AT+SMUNSUB=\"" + topic + "\"";
    std::string resp;
    if (!at.sendCommand(cmd, "OK", resp, 30000))
    {
        LOG_ERROR("MQTT", "Unsubscribe %s failed: %s", topic.c_str(), resp.c_str());
        return false;
    }
    return true;
}

int MQTTClient_AT::mqttState()
{
    std::string resp;
    int state = -1;
    if (!at.sendCommand("AT+SMSTATE?", "+SMSTATE:", resp, 5000) ||
        sscanf(resp.c_str(), "+SMSTATE: %d", &state) != 1)
    {
        return -1;
    }
    return state;
}

// ============================================================
// API de alto nível
// ============================================================

bool MQTTClient_AT::begin()
{
    at.setUrcHandler([this](const std::string &line) { handleUrc(line); });

    // Encerra sessão anterior (ex.: após reset do ESP32 sem reset do modem)
    if (mqttState() == 1)
    {
        LOG_INFO("MQTT", "Previous session found, disconnecting");
        mqttDisconnect();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    connected_ = false;
    return true;
}

bool MQTTClient_AT::parseServer(const std::string &server, bool tls, std::string &host, int &port)
{
    std::string s = server;

    const char *schemes[] = {"tcp://", "mqtt://", "ssl://", "mqtts://", "tls://", nullptr};
    for (int i = 0; schemes[i]; ++i)
    {
        size_t n = strlen(schemes[i]);
        if (s.compare(0, n, schemes[i]) == 0)
        {
            if (i >= 2)
                tls = true;
            s = s.substr(n);
            break;
        }
    }

    auto slash = s.find('/');
    if (slash != std::string::npos)
        s = s.substr(0, slash);

    port = tls ? 8883 : 1883;
    auto colon = s.rfind(':');
    if (colon != std::string::npos)
    {
        std::string p = s.substr(colon + 1);
        char *end = nullptr;
        long v = strtol(p.c_str(), &end, 10);
        if (!p.empty() && end && *end == '\0' && v > 0 && v <= 65535)
        {
            port = (int)v;
            s = s.substr(0, colon);
        }
    }

    host = s;
    return !host.empty();
}

bool MQTTClient_AT::connect(const std::string &clientID,
                            const std::string &server,
                            int keepAlive,
                            bool cleanSession,
                            const std::string &username,
                            const std::string &password,
                            bool criptografia)
{
    std::string host;
    int port = 0;
    if (!parseServer(server, criptografia, host, port))
    {
        LOG_ERROR("MQTT", "Invalid server: %s", server.c_str());
        return false;
    }

    // SMCONF só pode ser alterado com a sessão desconectada
    if (mqttState() == 1)
        mqttDisconnect();

    LOG_INFO("MQTT", "Broker %s:%d (clientId=%s, tls=%d)", host.c_str(), port, clientID.c_str(), criptografia);

    if (!mqttSetUrl(host, port))
        return false;
    if (!mqttSetClientId(clientID))
        return false;
    if (!mqttSetKeepAlive(keepAlive))
        return false;
    if (!mqttSetCleanSession(cleanSession))
        return false;
    if (!username.empty() && !mqttSetCredentials(username, password))
        return false;

    if (criptografia && !mqttSetSSL(sslIndex_, caName_, certName_))
        return false;

    bool ok = mqttConnect();
    connectedServer = ok ? server : "";
    return ok;
}

bool MQTTClient_AT::publish(const std::string &topic,
                            const std::string &payload,
                            int qos,
                            bool retain)
{
    return mqttPublish(topic, payload, qos, retain);
}

bool MQTTClient_AT::subscribe(const std::string &topic, int qos)
{
    return mqttSubscribe(topic, qos);
}

bool MQTTClient_AT::unsubscribe(const std::string &topic)
{
    return mqttUnsubscribe(topic);
}

bool MQTTClient_AT::disconnect()
{
    connectedServer.clear();
    return mqttDisconnect();
}

void MQTTClient_AT::setSSLCertificates(const std::string &caName,
                                       const std::string &certName,
                                       int sslIndex)
{
    caName_ = caName;
    certName_ = certName;
    sslIndex_ = sslIndex;
}

void MQTTClient_AT::loop()
{
    at.poll();
    while (parseIncoming())
    {
    }
}

bool MQTTClient_AT::connected()
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        int state = mqttState();
        if (state >= 0)
        {
            connected_ = (state == 1);
            return connected_;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    // Sem resposta do modem: considera desconectado
    connected_ = false;
    return false;
}

void MQTTClient_AT::onMessage(MessageCallback callback)
{
    messageCallback = callback;
}

// ============================================================
// URCs
// ============================================================

bool MQTTClient_AT::parseSubUrc(const std::string &line, std::string &topic, std::string &payload)
{
    // +SMSUB: "topic","message"   (a mensagem pode conter aspas, ex.: JSON)
    if (line.compare(0, 7, "+SMSUB:") != 0)
        return false;

    auto q1 = line.find('"');
    if (q1 == std::string::npos)
        return false;
    auto sep = line.find("\",\"", q1 + 1);
    if (sep == std::string::npos)
        return false;

    topic = line.substr(q1 + 1, sep - q1 - 1);

    size_t start = sep + 3;
    size_t end = line.rfind('"');
    if (end == std::string::npos || end < start)
        end = line.size();
    payload = line.substr(start, end - start);
    return true;
}

void MQTTClient_AT::handleUrc(const std::string &line)
{
    // Chamado com o mutex da UART travado: apenas enfileira, sem comandos AT.
    if (line.compare(0, 7, "+SMSUB:") == 0)
    {
        std::string topic, payload;
        if (!parseSubUrc(line, topic, payload))
        {
            LOG_WARN("MQTT", "Could not parse: %s", line.c_str());
            return;
        }
        if (xSemaphoreTake(queueMutex_, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            if (pending_.size() >= kMaxQueue)
            {
                LOG_WARN("MQTT", "RX queue full, dropping oldest message");
                pending_.pop_front();
            }
            pending_.emplace_back(topic, payload);
            xSemaphoreGive(queueMutex_);
        }
        return;
    }

    // Queda da sessão informada pelo modem
    if (line.compare(0, 10, "+SMSTATE: ") == 0)
    {
        int state = -1;
        if (sscanf(line.c_str(), "+SMSTATE: %d", &state) == 1 && state == 0)
        {
            if (connected_)
                LOG_WARN("MQTT", "Connection lost (+SMSTATE: 0)");
            connected_ = false;
        }
    }
}

bool MQTTClient_AT::parseIncoming()
{
    std::pair<std::string, std::string> msg;
    if (xSemaphoreTake(queueMutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;
    bool has = !pending_.empty();
    if (has)
    {
        msg = std::move(pending_.front());
        pending_.pop_front();
    }
    xSemaphoreGive(queueMutex_);

    if (!has)
        return false;

    LOG_DEBUG("MQTT", "RX %s -> %s", msg.first.c_str(), msg.second.c_str());
    if (messageCallback)
        messageCallback(msg.first, msg.second);
    return true;
}
