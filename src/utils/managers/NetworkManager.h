#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <string>
#include "ATCommandManager.h"

/// Modo de acesso LTE do SIM7080G.
enum RadioMode : int
{
    RADIO_CATM = 0,    ///< LTE CAT-M1  (AT+CNMP=38, AT+CMNB=1)
    RADIO_NBIOT = 1,   ///< LTE NB-IoT  (AT+CNMP=38, AT+CMNB=2)
    RADIO_UNKNOWN = -1
};

/// O outro modo (CAT-M <-> NB-IoT).
inline RadioMode otherRadioMode(RadioMode m)
{
    return m == RADIO_NBIOT ? RADIO_CATM : RADIO_NBIOT;
}

/// "CAT-M" / "NB-IoT"
inline const char *radioModeName(RadioMode m)
{
    return m == RADIO_CATM ? "CAT-M" : (m == RADIO_NBIOT ? "NB-IoT" : "desconhecido");
}

/**
 * Gerenciamento de rede do SIM7080G (LTE CAT-M / NB-IoT).
 *
 * O rádio é travado em um modo por vez (AT+CNMP=38 + AT+CMNB=1 ou 2). A troca
 * automática entre CAT-M e NB-IoT é feita por setupSIM7080() / modemMaintain().
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

    /**
     * Trava o rádio em um modo: AT+CNMP=38 (só LTE) + AT+CMNB=1 (CAT-M) ou 2 (NB-IoT).
     * Envie com AT+CFUN=0 para valer no próximo attach.
     */
    bool setRadioMode(RadioMode mode);

    bool setBands(const std::string &mode,                    ///< AT+CBANDCFG="CAT-M"|"NB-IOT",<b1>[,<b2>...]
                  const std::string &bandList);
    bool setBands(RadioMode mode, const std::string &bandList); ///< Idem, pelo RadioMode

    bool setOperatorAutomatic();                              ///< AT+COPS=0

    // ---------- Sinal / registro ----------
    bool getSignalQuality(int &rssi, int &ber);               ///< AT+CSQ
    bool getRegistrationStatus(int &n, int &stat);            ///< AT+CEREG?
    bool isRegistered();                                      ///< AT+CEREG? com stat 1 (casa) ou 5 (roaming)
    bool getSystemInfo(std::string &info);                    ///< AT+CPSI?
    RadioMode getActiveMode();                                ///< AT+CPSI?: "LTE CAT-M1" / "LTE NB-IOT"

    /**
     * APN entregue pela rede no attach (AT+CGNAPN), sem o sufixo
     * ".mncXXX.mccYYY.gprs" quando a rede o inclui.
     */
    bool getNetworkApn(std::string &apn);

    // ---------- Contexto de dados ----------
    bool setApnSession(const std::string &apn = "");          ///< AT+CGDCONT=1,"IP","apn" ("" = APN da rede)
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
     * Contexto de aplicação com o rádio já registrado: CNCFG + CNACT.
     * @param apn APN a usar; "" = APN entregue pela rede (AT+CGNAPN).
     */
    bool init(const std::string &apn = "",
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
