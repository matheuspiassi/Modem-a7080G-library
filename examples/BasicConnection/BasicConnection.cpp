/**
 * Exemplo básico: inicializa o SIM7080G em CAT-M (ou NB-IoT se CAT-M falhar),
 * ativa o contexto de dados, sincroniza o relógio via NTP e deixa o supervisor
 * cuidando da conexão (troca de modo automática quando a rede cai).
 */
#include <Arduino.h>
#include <ModemSIM7080G.h>

// ModemConfig: apn, user, password, rxPin, txPin, resetPin, logLevel,
//              firstMode, catmBands, nbiotBands, baudRate, registerTimeoutMs
ModemConfig modemCfg = {
    nullptr,           // APN: nullptr = automático (APN entregue pela rede)
    nullptr,           // usuário (nullptr se não houver)
    nullptr,           // senha   (nullptr se não houver)
    16,                // RX do ESP32 (TX do modem)
    17,                // TX do ESP32 (RX do modem)
    -1,                // pino de reset/PWRKEY (-1 = não controla)
    LOG_LEVEL_DEBUG,
    RADIO_CATM,        // tenta CAT-M primeiro; se não conectar, NB-IoT
    nullptr,           // bandas CAT-M  (nullptr = mantém as do módulo)
    "28",              // bandas NB-IoT (nullptr = mantém as do módulo)
    115200,
    0};                // tempo máximo de registro por modo (0 = 120 s)

void setup()
{
    Serial.begin(115200);
    Serial1.begin(115200); // a biblioteca reporta "SIN,<rssi>" e "COMM,..." na Serial1

    if (setupSIM7080(modemCfg, -12)) // -12 quartos de hora = UTC-3
    {
        NetworkManager::CclkResult now = network.infoDateAndTime();
        if (now.valid)
            Serial.printf("Hora do modem: %s\n", now.iso8601);
    }
    else
    {
        Serial.println("Falha ao iniciar o modem; o supervisor continuará tentando");
    }

    // Verifica a conexão a cada 30 s e troca CAT-M <-> NB-IoT quando ela cai
    startModemSupervisor(30000);
}

void loop()
{
    Serial.printf("Rede: %s | modo: %s\n",
                  modemOnline() ? "on-line" : "off-line",
                  radioModeName(modemActiveMode()));
    delay(30000);
}
