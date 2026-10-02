/**
 * Exemplo GNSS: liga o GPS do SIM7080G, lê a posição (AT+CGNSINF) e publica via MQTT.
 *
 * No SIM7080G o GNSS divide o RF com o LTE: a leitura é feita com a rede ociosa
 * (nenhuma publicação em andamento). Faça o primeiro fix com antena GNSS ativa e céu aberto.
 */
#include <Arduino.h>
#include <ModemSIM7080G.h>

ModemConfig modemCfg = {
    "zap.vivo.com.br", "vivo", "vivo",
    16, 17, -1,
    LOG_LEVEL_SIMPLE,
    38, 1, 115200};

MqttConfig mqttCfg = {
    "sim7080-tracker",
    "test.mosquitto.org:1883",
    60,
    nullptr, nullptr,
    nullptr, // sem subscribe
    nullptr,
    nullptr, nullptr};

unsigned long lastFix = 0;

void setup()
{
    Serial.begin(115200);
    Serial1.begin(115200);

    if (!setupSIM7080(modemCfg))
        return;

    // Opcional (vale após reboot): GPS + GLONASS
    // gnss.setWorkMode(true);

    setupGNSS(0); // só liga; o fix é aguardado no loop
    setupMqtt(mqttCfg);
}

void loop()
{
    mqtt.loop();

    if (millis() - lastFix < 15000)
        return;
    lastFix = millis();

    GnssInfo info;
    if (!gnss.getInfo(info))
        return;

    if (!info.fixed)
    {
        Serial.printf("Sem fix ainda (satélites GPS em vista: %d)\n", info.gpsSatsInView);
        return;
    }

    char json[200];
    snprintf(json, sizeof(json),
             "{\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,\"vel\":%.1f,\"sats\":%d,\"utc\":\"%s\"}",
             info.latitude, info.longitude, info.altitude, info.speedKmh,
             info.gnssSatsUsed, info.utc);
    Serial.println(json);

    if (mqtt.connected())
        mqtt.publish("sim7080/demo/location", json);
}
