#ifndef GNSSMANAGER_H
#define GNSSMANAGER_H

#include <string>
#include "ATCommandManager.h"

/**
 * Dados retornados por AT+CGNSINF (tabela 8-1 do manual SIM7080).
 */
struct GnssInfo
{
    bool running = false;          ///< GNSS ligado
    bool fixed = false;            ///< Posição fixada
    int year = 0, month = 0, day = 0;
    int hour = 0, minute = 0;
    float second = 0;              ///< UTC
    char utc[20] = {0};            ///< yyyyMMddhhmmss.sss (texto original)
    double latitude = 0;           ///< graus (±dd.dddddd)
    double longitude = 0;          ///< graus (±ddd.dddddd)
    float altitude = 0;            ///< MSL, metros
    float speedKmh = 0;            ///< velocidade sobre o solo, km/h
    float course = 0;              ///< rumo, graus
    int fixMode = 0;               ///< 0,1,2
    float hdop = 0, pdop = 0, vdop = 0;
    int gpsSatsInView = 0;
    int gnssSatsUsed = 0;
    int glonassSatsInView = 0;
    int cn0Max = 0;                ///< dBHz
    float hpa = 0, vpa = 0;        ///< precisão horizontal/vertical, metros
};

/**
 * GNSS do SIM7080G.
 *
 * Atenção: no SIM7080G o GNSS compartilha o RF com o LTE. Com tráfego de dados
 * ativo o receptor pode não conseguir fix; faça a leitura com a rede ociosa
 * (sem publicações em andamento) ou, se necessário, com AT+CFUN=0.
 */
class GNSSManager
{
public:
    GNSSManager(ATCommandManager &at);

    bool powerOn();                                   ///< AT+CGNSPWR=1
    bool powerOff();                                  ///< AT+CGNSPWR=0
    int  powerState();                                ///< AT+CGNSPWR? (1 ligado, 0 desligado, -1 erro)

    /**
     * AT+CGNSMOD=1,<glo>,<bd>,<gal>,<qzss> (GPS sempre ligado).
     * Somente UMA constelação adicional pode ser habilitada. Vale após reboot.
     */
    bool setWorkMode(bool glonass, bool beidou = false, bool galileo = false, bool qzss = false);

    bool coldStart();                                 ///< AT+CGNSCOLD
    bool warmStart();                                 ///< AT+CGNSWARM
    bool hotStart();                                  ///< AT+CGNSHOT

    bool setXtra(bool enable);                        ///< AT+CGNSXTRA=<0|1>
    bool copyXtra();                                  ///< AT+CGNSCPY

    /** Lê AT+CGNSINF. Retorna true se o comando respondeu (verifique info.fixed). */
    bool getInfo(GnssInfo &info);

    /** Aguarda fix consultando AT+CGNSINF a cada intervalMs. */
    bool waitForFix(GnssInfo &info, uint32_t timeoutMs = 120000, uint32_t intervalMs = 2000);

    /** Atalho: retorna latitude/longitude se houver fix. */
    bool getLocation(double &latitude, double &longitude);

    /** Converte a linha "+CGNSINF: ..." em GnssInfo. */
    static bool parseCgnsinf(const std::string &line, GnssInfo &info);

private:
    ATCommandManager &at;
    bool simpleCommand(const char *cmd, uint32_t timeout = 10000);
};

#endif // GNSSMANAGER_H
