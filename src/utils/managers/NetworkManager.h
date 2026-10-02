#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <string>
#include "ATCommandManager.h"

/**
 * Gerenciamento de rede do SIM7080G (LTE CAT-M / NB-IoT / GSM).
 *
 * No SIM7080G os serviços internos (MQTT, HTTP, NTP, TCP) usam o contexto
 * de aplicação configurado com AT+CNCFG e ativado com AT+CNACT (<pdpidx> 0..3),
 * e não o AT+CGACT do A76XX.
 */
class NetworkManager
{
public:
    NetworkManager(ATCommandManager &at);

    // ---------- Estado do módulo ----------
    bool checkSim(std::string &status);                       ///< AT+CPIN? (true se READY)
    bool setFunctionality(int fun, int rst = 0);              ///< AT+CFUN=<fun>[,<rst>]
    bool setNetworkMode(int mode);                            ///< AT+CNMP: 2 auto, 13 GSM, 38 LTE, 51 GSM+LTE
    bool setPreferredLteMode(int mode);                       ///< AT+CMNB: 1 CAT-M, 2 NB-IoT, 3 ambos
    bool setBands(const std::string &mode,                    ///< AT+CBANDCFG="CAT-M"|"NB-IOT",<b1>[,<b2>...]
                  const std::string &bandList);

    // ---------- Sinal / registro ----------
    bool getSignalQuality(int &rssi, int &ber);               ///< AT+CSQ
    bool getRegistrationStatus(int &n, int &stat);            ///< AT+CEREG? (fallback AT+CREG?)
    bool getSystemInfo(std::string &info);                    ///< AT+CPSI?
    bool getNetworkApn(std::string &apn);                     ///< AT+CGNAPN

    // ---------- Contexto de dados ----------
    bool setApnSession(const std::string &apn);               ///< AT+CGDCONT=1,"IP","apn"
    bool definePDPContext(int cid,
                          const std::string &pdpType = "IP",
                          const std::string &apn = "",
                          const std::string &pdpAddr = "",
                          int dComp = -1,
                          int hComp = -1,
                          int ipv4Ctrl = -1,
                          int requestType = -1);              ///< AT+CGDCONT completo (requestType ignorado no SIM7080)
    bool setAuthentication(int cid,
                           int authType = 0,
                           const std::string &password = "",
                           const std::string &username = "");  ///< AT+CGAUTH
    bool attachGPRS(bool attach = true);                      ///< AT+CGATT
    bool configurePDP(int pdpIdx,
                      const std::string &apn,
                      const std::string &user = "",
                      const std::string &password = "",
                      int authType = -1,
                      int ipType = 1);                        ///< AT+CNCFG (authType -1 = automático)
    bool activateContext(int pdpIdx = 0);                     ///< AT+CNACT=<pdpidx>,1
    bool deactivateContext(int pdpIdx = 0);                   ///< AT+CNACT=<pdpidx>,0
    bool isContextActive(int pdpIdx, std::string *address = nullptr); ///< AT+CNACT?
    bool getPDPAddress(int pdpIdx, std::string &address);     ///< IP do contexto (AT+CNACT?)

    /**
     * Sequência completa de dados: CGDCONT + CGAUTH (opcional) + CNCFG + CNACT.
     */
    bool init(const std::string &apn,
              const std::string &user = "",
              const std::string &password = "");

    // ---------- Hora ----------
    /**
     * Sincroniza via NTP (AT+CNTPCID + AT+CNTP).
     * @param tz_quarters fuso em quartos de hora (-12 = UTC-3)
     * @param pdpIdx      contexto usado pelo NTP (o mesmo ativado em activateContext)
     */
    bool syncTimeNtp(const std::string &host, int tz_quarters = 0,
                     int exec_timeout_ms = 120000, int pdpIdx = 0);

    bool timeZoneUpdate();                                    ///< AT+CLTS=1 (hora da rede) + AT&W

    struct CclkResult
    {
        int year = 0, month = 0, day = 0;
        int hour = 0, minute = 0, second = 0;
        int tz_quarters = 0; // -96..+96
        int tz_minutes = 0;  // tz_quarters * 15
        int tz_sign = 0;     // -1,0,+1
        bool valid = false;
        char iso8601[40] = {0};
    };
    CclkResult infoDateAndTime();                             ///< AT+CCLK?

    // ---------- SSL / certificados ----------
    /**
     * Configura o contexto SSL (AT+CSSLCFG): TLS 1.2, ignora RTC e SNI opcional.
     */
    bool configureSSL(bool enableTLS,
                      int sslCtxIndex = 0,
                      bool enableSNI = true,
                      const std::string &sniHost = "");

    /**
     * Grava um arquivo no sistema de arquivos do modem (AT+CFSINIT/CFSWFILE/CFSTERM).
     * @param dirIndex 0 /custapp/, 1 /fota/, 2 /datatx/, 3 /customer/ (certificados: 3)
     * Tamanho máximo: 10240 bytes.
     */
    bool uploadFile(int dirIndex, const std::string &fileName, const std::string &content);

    /**
     * Converte um certificado já gravado para uso SSL (AT+CSSLCFG="CONVERT").
     * @param sslType 1 certificado cliente, 2 CA, 3 tabela PSK
     */
    bool convertCertificate(int sslType,
                            const std::string &certName,
                            const std::string &keyName = "",
                            const std::string &passKey = "");

private:
    ATCommandManager &at;
};


#endif // NETWORKMANAGER_H
