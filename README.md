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
    nullptr, nullptr, nullptr,         // APN (nullptr = automático), usuário, senha
    16, 17,                            // RX, TX do ESP32 (Serial2)
    -1,                                // pino de reset/PWRKEY (-1 = não usa)
    LOG_LEVEL_SIMPLE,
    RADIO_CATM,                        // modo tentado primeiro (RADIO_CATM ou RADIO_NBIOT)
    nullptr,                           // bandas CAT-M  (nullptr = mantém as do módulo)
    "28",                              // bandas NB-IoT (nullptr = mantém as do módulo)
    115200,                            // baud rate (0 = 115200)
    0};                                // tempo máx. de registro por modo (0 = 120 s)

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
    startModemSupervisor(30000);       // reconexão autônoma CAT-M <-> NB-IoT
}

void loop() {
    mqtt.loop();                       // entrega as mensagens recebidas ao callback
    if (modemOnline() && mqtt.isConnectedCached())
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
2. Conecta no modo `firstMode` (CAT-M por padrão):
   `AT+CFUN=0` → `AT+CNMP=38` / `AT+CMNB=1` (CAT-M) ou `2` (NB-IoT) / `AT+CBANDCFG` / `AT+CGDCONT` / `AT+CGAUTH` → `AT+CFUN=1` → `AT+COPS=0`
3. Aguarda registro (`AT+CEREG?`) por até `registerTimeoutMs`
4. APN: o de `ModemConfig.apn` ou, se `nullptr`, o entregue pela rede (`AT+CGNAPN`)
5. `AT+CNCFG=0,...` e `AT+CNACT=0,1` (contexto de aplicação usado por MQTT e NTP)
6. Se os passos 2–5 falharem, repete tudo no **outro modo** (CAT-M → NB-IoT ou NB-IoT → CAT-M)
7. `AT+CNTPCID` / `AT+CNTP` e leitura do relógio (`AT+CCLK?`)

## Conexão autônoma (CAT-M ↔ NB-IoT)

`startModemSupervisor(intervalMs)` cria uma task que chama `modemMaintain()` periodicamente. Se preferir, chame `modemMaintain()` no seu `loop()`. A cada ciclo:

| Situação | Ação |
|---|---|
| Modem não responde a `AT` | Reinicializa a comunicação e reconecta |
| Registrado, mas contexto de dados inativo | `AT+CNACT=0,1` no mesmo modo |
| Registro ou dados perdidos por mais de 60 s | Troca para o **outro** modo. Se falhar, volta ao modo anterior |
| MQTT caído | Reconecta (`setupMqtt` com a config guardada) e refaz o subscribe |
| MQTT falha 3 vezes seguidas com a rede "ok" | Troca de modo |
| 3 trocas seguidas sem sucesso | Reinicia o modem (`AT+CFUN=1,1`) |

- `modemOnline()` / `modemActiveMode()` devolvem o último estado sem enviar comando AT.
- `switchRadioMode()` força a troca de modo na hora.
- Se `setupSIM7080()` falhar no boot, o supervisor continua tentando. `setupMqtt()` sem rede só guarda a configuração, e o supervisor conecta quando a rede subir.
- Durante uma reconexão o modem fica ocupado e publicações falham. Confira `modemOnline() && mqtt.isConnectedCached()` antes de publicar.

### APN automático

A biblioteca não tem APN fixo. Com `apn = nullptr`, o attach é feito com `AT+CGDCONT=1,"IP",""` e a rede entrega o APN padrão do chip. Esse APN é lido com `AT+CGNAPN` e usado no `AT+CNCFG`. Se a operadora exigir um APN específico no attach, preencha `ModemConfig.apn`.

### Banda ativa e sinal da célula

`network.getActiveBand(info)` lê `AT+CPSI?` e preenche um `CellInfo` com a banda **em uso** (`info.band`, ex.: `28`), EARFCN, RSRP/RSRQ/RSSI, `sinr` (`2 * RSSNR - 20`), MCC-MNC, TAC e Cell ID. Não confundir com `AT+CBANDCFG?`, que lista as bandas habilitadas para busca.

```cpp
CellInfo info;
if (network.getActiveBand(info) && info.registered)
    Serial.printf("Banda %d, RSRP %d dBm\n", info.band, info.rsrp);
// Log: [MODEM] I: LTE CAT-M1 | Banda 28 | EARFCN 9410 | RSRP -95 dBm | RSRQ -10 dB | SINR 10 dB | 724-05
```

- Antes do `AT+CPSI?` consulta `AT+CEREG?`. Sem registro (`stat` diferente de 1 ou 5) retorna `true` com `info.registered = false`. Para pular essa consulta, use `getActiveBand(info, false)`.
- `+CPSI: NO SERVICE` também retorna `true` com `registered = false`. Timeout, `+CME ERROR` ou resposta fora do formato retornam `false` e logam a linha crua.
- Em GSM, `band = -1` e os campos `lac`, `arfcn` e `rxlev` são preenchidos.
- Testes do parser (rodam no PC): veja o cabeçalho de [test/host/cpsi_parser_test.cpp](test/host/cpsi_parser_test.cpp).

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

`MqttConfig` ganhou campos no final. Em código que inicializa as structs por chaves, os campos omitidos ficam com 0/nullptr, que significa "usar o padrão".

Em `ModemConfig`, `networkMode` (`AT+CNMP`) e `lteMode` (`AT+CMNB`) foram trocados por `firstMode`, `catmBands`, `nbiotBands` e `registerTimeoutMs`. Código antigo terminado em `38, 1, 115200` não compila mais. Troque por `RADIO_CATM, nullptr, nullptr, 115200, 0`.

## Logs

`LOG_LEVEL_DEBUG` mostra todo TX/RX AT e os URCs. `LOG_LEVEL_SIMPLE` mostra apenas INFO, WARN e ERROR.
