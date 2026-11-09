/**
 * Exemplo MQTT: conecta ao broker, assina um tópico e publica a cada 30 s.
 * Mensagens recebidas chegam pelo callback chamado dentro de mqtt.loop().
 *
 * O supervisor reconecta a rede (trocando CAT-M <-> NB-IoT) e o MQTT sozinho.
 */
#include <Arduino.h>
#include <ModemSIM7080G.h>

ModemConfig modemCfg = {
    nullptr, nullptr, nullptr, // APN automático, sem usuário/senha
    16, 17, -1,
    LOG_LEVEL_SIMPLE,
    RADIO_CATM,                // CAT-M primeiro, NB-IoT como reserva
    nullptr, "28",             // bandas CAT-M / NB-IoT
    115200, 0};

void onMqttMessage(const std::string &topic, const std::string &payload)
{
    Serial.printf("Mensagem em %s: %s\n", topic.c_str(), payload.c_str());
}

MqttConfig mqttCfg = {
    "sim7080-demo",              // clientId
    "tcp://test.mosquitto.org:1883", // broker
    60,                          // keepalive (manual: 60-180 s)
    nullptr,                     // usuário
    nullptr,                     // senha
    "sim7080/demo/cmd",          // tópico para assinar
    onMqttMessage,               // callback
    nullptr,                     // CA (somente TLS)
    nullptr};                    // certificado do cliente (somente TLS)

unsigned long lastPublish = 0;

void setup()
{
    Serial.begin(115200);
    Serial1.begin(115200);

    setupSIM7080(modemCfg);
    setupMqtt(mqttCfg, false);   // true = MQTT sobre TLS. Sem rede, só guarda a config.
    startModemSupervisor(30000); // rede + MQTT autônomos
}

void loop()
{
    mqtt.loop(); // processa +SMSUB e chama o callback

    if (millis() - lastPublish > 30000)
    {
        lastPublish = millis();

        // Durante uma reconexão a publicação é pulada (o supervisor está trabalhando)
        if (!modemOnline() || !mqtt.isConnectedCached())
        {
            Serial.println("Sem conexão no momento, publicação pulada");
            return;
        }

        int rssi = 0, ber = 0;
        network.getSignalQuality(rssi, ber);
        std::string payload = "{\"rssi\":" + std::to_string(rssi) +
                              ",\"uptime\":" + std::to_string(millis() / 1000) + "}";
        if (mqtt.publish("sim7080/demo/status", payload, 1))
            Serial.println("Publicado");
    }
    delay(50);
}
