# Modem SIM7080G Library

Biblioteca para ESP32 (Arduino / PlatformIO) que controla o modem **SIMCom SIM7080G** (LTE CAT-M / NB-IoT) por comandos AT, com rede, NTP, MQTT/MQTTS e GNSS.

Baseada no *SIM7080 Series AT Command Manual V1.02*. A arquitetura segue a da biblioteca A7608SA: um `ATCommandManager` único, managers por função e funções `setup*` que executam o fluxo completo com novas tentativas.

## Instalação

`platformio.ini`:

```ini
lib_deps =
    https://github.com/RadCom-Comunity/Modem-a7880G-library.git
```

```cpp
#include <ModemSIM7080G.h>
```

## Uso rápido

```cpp
ModemConfig modemCfg = {
    "zap.vivo.com.br", "vivo", "vivo", // APN, usuário, senha
    16, 17,                            // RX, TX do ESP32 (Serial2)
    -1,                                // pino de reset/PWRKEY (-1 = não usa)
    LOG_LEVEL_SIMPLE,
    38,                                // AT+CNMP: 38 = só LTE (0 = não altera)
    1,                                 // AT+CMNB: 1 CAT-M, 2 NB-IoT, 3 ambos (0 = não altera)
    115200};                           // baud rate (0 = 115200)

MqttConfig mqttCfg = {
    "meu-dispositivo",                 // clientId
    "tcp://broker.exemplo.com:1883",   // broker
    60,                                // keepalive (manual: 60-180 s)
    "usuario", "senha",
    "dispositivo/cmd",                 // tópico a assinar (nullptr = nenhum)
    [](const std::string &t, const std::string &p) { Serial.println(p.c_str()); },
    nullptr, nullptr};                 // CA / cert. do cliente (só TLS)

void setup() {
    Serial.begin(115200);
    setupSIM7080(modemCfg, -12);       // -12 quartos de hora = UTC-3 (NTP)
    setupMqtt(mqttCfg);                // setupMqtt(mqttCfg, true) para TLS
    setupGNSS();                       // liga o GNSS
}

void loop() {
    mqtt.loop();                       // entrega as mensagens recebidas ao callback
    mqtt.publish("dispositivo/status", "{\"ok\":1}");

    GnssInfo pos;
    if (gnss.getInfo(pos) && pos.fixed)
        Serial.printf("%.6f, %.6f\n", pos.latitude, pos.longitude);
    delay(30000);
}
```

Exemplos completos em [examples/](examples/).

## O que `setupSIM7080()` faz

1. `AT` (auto-baud), `ATE0`, `AT+CMEE=2`, `AT+CPIN?`
2. `AT+CFUN=0` → `AT+CNMP` / `AT+CMNB` / `AT+CGDCONT` / `AT+CGAUTH` → `AT+CFUN=1`
3. Aguarda registro (`AT+CEREG?`, com fallback para `AT+CREG?`)
4. `AT+CNCFG=0,...` e `AT+CNACT=0,1` (contexto de aplicação usado por MQTT e NTP)
5. `AT+CNTPCID` / `AT+CNTP` e leitura do relógio (`AT+CCLK?`)

## Objetos globais

| Objeto | Classe | Função |
|---|---|---|
| `atManager` | `ATCommandManager` | UART (Serial2), mutex, URCs, envio com prompt (`>` / `DOWNLOAD`) |
| `network` | `NetworkManager` | SIM, modo de rede, registro, APN, contexto de dados, NTP, SSL, certificados |
| `mqtt` | `MQTTClient_AT` | `AT+SMCONF`, `SMCONN`, `SMPUB`, `SMSUB`, `SMUNSUB`, `SMSTATE`, `SMDISC` |
| `gnss` | `GNSSManager` | `AT+CGNSPWR`, `CGNSINF`, `CGNSMOD`, cold/warm/hot start, XTRA |

## MQTT

- O servidor aceita `host`, `host:porta`, `tcp://host:porta` ou `ssl://host:porta`. A porta padrão é 1883, ou 8883 com TLS.
- Mensagens recebidas chegam no URC `+SMSUB: "topic","message"`, entram numa fila e são entregues ao callback em `mqtt.loop()`. Chame `loop()` com frequência.
- Pelo manual, o payload do `AT+SMPUB` tem no máximo **1024 bytes**. Payloads maiores são recusados.
- `mqtt.connected()` consulta `AT+SMSTATE?`. `mqtt.isConnectedCached()` devolve o último estado conhecido sem enviar comando (atualizado pelo URC `+SMSTATE: 0`).
- Mensagens com quebra de linha não são suportadas no URC `+SMSUB`.

### MQTT com TLS

```cpp
// Uma vez: grava e converte o CA no modem
network.uploadFile(3, "ca.crt", CA_PEM);     // 3 = /customer/
network.convertCertificate(2, "ca.crt");     // 2 = CA list

mqttCfg.caCertName = "ca.crt";
setupMqtt(mqttCfg, true);                    // TLS 1.2, SNI e AT+SMSSL
```

Sem `caCertName`, o módulo não valida o certificado do servidor.

## GNSS

```cpp
gnss.powerOn();
GnssInfo info;
if (gnss.waitForFix(info, 180000)) {
    // info.latitude, info.longitude, info.altitude, info.speedKmh,
    // info.course, info.gnssSatsUsed, info.hdop, info.utc ...
}
gnss.setWorkMode(true);   // GPS + GLONASS (só uma constelação extra; vale após reboot)
```

**Importante:** no SIM7080G o GNSS compartilha o RF com o LTE. Com tráfego de dados em andamento o receptor pode não conseguir fix. Faça as leituras com a rede ociosa. Se ainda assim não houver fix, desative o rádio com `network.setFunctionality(0)` durante a aquisição. Use antena GNSS ativa e céu aberto. O primeiro fix (cold start) pode levar alguns minutos.

## Migração da versão A7608SA

| A7608SA | SIM7080G |
|---|---|
| `#include <ModemA7608SA.h>` | `#include <ModemSIM7080G.h>` |
| `setupSIMA76XX(cfg)` | `setupSIM7080(cfg)` (o nome antigo continua como alias) |
| `AT+CGACT` / `AT+CGPADDR` | `AT+CNCFG` / `AT+CNACT` (`activateContext(0)`) |
| `AT+CMQTT*` | `AT+SM*` |
| `AT+CTZU` | `AT+CLTS` (`timeZoneUpdate()`) |
| `+CNTP: 0` = sucesso | `+CNTP: 1` = sucesso |
| — | `gnss` / `setupGNSS()` |

`ModemConfig` e `MqttConfig` ganharam campos no final. Em código que inicializa as structs por chaves, os campos omitidos ficam com 0/nullptr, que significa "usar o padrão".

## Logs

`LOG_LEVEL_DEBUG` mostra todo TX/RX AT e os URCs. `LOG_LEVEL_SIMPLE` mostra apenas INFO, WARN e ERROR.
