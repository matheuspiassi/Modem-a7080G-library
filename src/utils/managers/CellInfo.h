#ifndef CELLINFO_H
#define CELLINFO_H

#include <string>

/**
 * Informações da célula servidora reportadas por AT+CPSI?
 * (SIM7080 AT Command Manual V1.02, 5.2.19 "Inquiring UE System Information").
 *
 * Este módulo é C++ puro (sem Arduino/FreeRTOS) para poder ser testado no PC.
 * A consulta ao modem fica em NetworkManager::getActiveBand().
 */

/// <System Mode> do AT+CPSI?
enum CellSystemMode : int
{
    CELL_MODE_UNKNOWN = -1,
    CELL_MODE_NO_SERVICE = 0, ///< "NO SERVICE"
    CELL_MODE_GSM = 1,        ///< "GSM"
    CELL_MODE_CATM = 2,       ///< "LTE CAT-M1"
    CELL_MODE_NBIOT = 3       ///< "LTE NB-IOT"
};

/// Resultado do parser de +CPSI
enum CpsiParseStatus : int
{
    CPSI_OK = 0,
    CPSI_ERR_EMPTY,        ///< Linha vazia (ex.: timeout)
    CPSI_ERR_MODEM,        ///< "ERROR" / "+CME ERROR: <err>"
    CPSI_ERR_PREFIX,       ///< Linha não começa com "+CPSI:"
    CPSI_ERR_UNKNOWN_MODE, ///< <System Mode> desconhecido
    CPSI_ERR_FIELD_COUNT,  ///< Quantidade de campos diferente da esperada (14 LTE, 9 GSM)
    CPSI_ERR_BAD_FIELD     ///< Campo numérico/MCC-MNC inválido
};

struct CellInfo
{
    bool registered = false;                      ///< false em "NO SERVICE" ou se CEREG não indicar registro
    int cereg_stat = -1;                          ///< <stat> do AT+CEREG? (-1 = não consultado)
    CellSystemMode system_mode = CELL_MODE_UNKNOWN;
    std::string system_mode_name;                 ///< Texto original: "LTE CAT-M1", "GSM"...
    std::string operation_mode;                   ///< "Online", "Offline", "Low Power Mode"...
    std::string mcc;                              ///< Ex.: "724" (texto, preserva zeros à esquerda)
    std::string mnc;                              ///< Ex.: "05"

    // ---- LTE (CAT-M1 / NB-IoT) ----
    long tac = -1;                                ///< Tracking Area Code (recebido em hexa, ex.: 0x1A2B)
    long cell_id = -1;                            ///< SCellID (LTE) ou Cell ID (GSM)
    int pcell_id = -1;                            ///< Physical Cell ID
    int band = -1;                                ///< Banda E-UTRAN em uso (ex.: 28); -1 em GSM/sem serviço
    std::string band_raw;                         ///< Campo original (ex.: "EUTRAN-BAND28")
    int earfcn = -1;
    int dlbw = -1, ulbw = -1;                     ///< Largura de banda DL/UL (código do modem)
    int rsrq = 0;                                 ///< dB
    int rsrp = 0;                                 ///< dBm
    int rssi = 0;                                 ///< dBm
    int rssnr = 0;                                ///< Valor bruto do modem
    int sinr = 0;                                 ///< dB = 2 * rssnr - 20
    bool sinr_valid = false;                      ///< sinr dentro da faixa -20..30 dB

    // ---- GSM ----
    long lac = -1;                                ///< Location Area Code (recebido em hexa)
    int arfcn = -1;                               ///< Absolute RF Channel Number
    int rxlev = 0;
    int track_lo_adjust = 0;
    std::string c1_c2;                            ///< Ex.: "42-42"
};

/**
 * Faz o parse de uma resposta de AT+CPSI?.
 *
 * Aceita a linha com ou sem o prefixo "+CPSI:" e com CR/LF/espaços nas pontas.
 * Os 3 formatos são diferenciados pelo <System Mode>:
 *   - "LTE CAT-M1" / "LTE NB-IOT": exatamente 14 campos
 *   - "GSM": exatamente 9 campos
 *   - "NO SERVICE": >= 2 campos (registered = false)
 *
 * Em caso de erro `out` volta aos valores padrão (registered = false, band = -1).
 */
CpsiParseStatus parseCpsi(const std::string &line, CellInfo &out);

/**
 * Extrai o número da banda do campo <Frequency Band>.
 * Tolera "EUTRAN-BAND28", "BAND28", "BAND 28", "band28", "28".
 * @return número da banda ou -1 se não houver número
 */
int parseBandNumber(const std::string &field);

/** true se a banda é suportada pelo SIM7080G (1,2,3,4,5,8,12,13,14,18,19,20,25,26,27,28,66,71,85). */
bool isSupportedBand(int band);

/** Nome curto do enum (para log). */
const char *cellSystemModeName(CellSystemMode mode);
const char *cpsiParseStatusName(CpsiParseStatus status);

/**
 * Linha resumo para log, ex.:
 * "LTE CAT-M1 | Banda 28 | EARFCN 9410 | RSRP -95 dBm | RSRQ -10 dB | SINR 10 dB | 724-05"
 */
std::string formatCellSummary(const CellInfo &info);

#endif // CELLINFO_H
