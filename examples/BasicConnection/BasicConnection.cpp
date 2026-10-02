/**
 * Exemplo básico: inicializa o SIM7080G, registra na rede (CAT-M),
 * ativa o contexto de dados e sincroniza o relógio via NTP.
 */
#include <Arduino.h>
#include <ModemSIM7080G.h>

// ModemConfig: apn, user, password, rxPin, txPin, resetPin, logLevel,
//              networkMode (AT+CNMP), lteMode (AT+CMNB), baudRate
ModemConfig modemCfg = {
    "zap.vivo.com.br", // APN
    "vivo",            // usuário (nullptr se não houver)
    "vivo",            // senha   (nullptr se não houver)
    16,                // RX do ESP32 (TX do modem)
    17,                // TX do ESP32 (RX do modem)
    -1,                // pino de reset/PWRKEY (-1 = não controla)
    LOG_LEVEL_DEBUG,
    38,                // AT+CNMP=38 -> somente LTE
    1,                 // AT+CMNB=1  -> CAT-M
    115200};

void setup()
{
    Serial.begin(115200);
    Serial1.begin(115200); // a biblioteca reporta "SIN,<rssi>" e "COMM,..." na Serial1

    if (!setupSIM7080(modemCfg, -12)) // -12 quartos de hora = UTC-3
    {
        Serial.println("Falha ao iniciar o modem");
        return;
    }

    std::string info;
    network.getSystemInfo(info);

    NetworkManager::CclkResult now = network.infoDateAndTime();
    if (now.valid)
        Serial.printf("Hora do modem: %s\n", now.iso8601);
}

void loop()
{
    int rssi = 0, ber = 0;
    if (network.getSignalQuality(rssi, ber))
        Serial.printf("RSSI=%d BER=%d\n", rssi, ber);

    if (!network.isContextActive(0))
    {
        Serial.println("Contexto de dados caiu, reativando...");
        network.activateContext(0);
    }
    delay(30000);
}
