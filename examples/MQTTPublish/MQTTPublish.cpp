/**
 * Exemplo MQTT: conecta ao broker, assina um tópico e publica a cada 30 s.
 * Mensagens recebidas chegam pelo callback chamado dentro de mqtt.loop().
 */
#include <Arduino.h>
#include <ModemSIM7080G.h>

ModemConfig modemCfg = {
    "zap.vivo.com.br", "vivo", "vivo",
    16, 17, -1,
    LOG_LEVEL_SIMPLE,
    38, 1, 115200};

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

    if (!setupSIM7080(modemCfg))
        return;

    setupMqtt(mqttCfg, false); // true = MQTT sobre TLS
}

void loop()
{
    mqtt.loop(); // processa +SMSUB e chama o callback

    if (millis() - lastPublish > 30000)
    {
        lastPublish = millis();

        if (!mqtt.connected())
        {
            Serial.println("MQTT desconectado, reconectando...");
            setupMqtt(mqttCfg, false);
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
