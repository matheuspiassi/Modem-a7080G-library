#include "CellInfo.h"
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <vector>

// ============================================================
// Helpers
// ============================================================

namespace
{

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string trim(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b]))
        ++b;
    while (e > b && isSpace(s[e - 1]))
        --e;
    return s.substr(b, e - b);
}

std::string toUpper(std::string s)
{
    for (auto &c : s)
        c = (char)toupper((unsigned char)c);
    return s;
}

bool startsWith(const std::string &s, const char *prefix)
{
    return s.compare(0, std::string(prefix).size(), prefix) == 0;
}

/// Divide por vírgula e remove espaços de cada campo (o <System Mode> tem espaço interno).
std::vector<std::string> splitFields(const std::string &s)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (true)
    {
        size_t comma = s.find(',', start);
        out.push_back(trim(s.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return out;
}

/// Conversão estrita: o campo inteiro precisa ser numérico.
bool parseLong(const std::string &s, int base, long &out)
{
    if (s.empty())
        return false;
    const char *begin = s.c_str();
    char *end = nullptr;
    errno = 0;
    long v = strtol(begin, &end, base);
    if (end == begin || *end != '\0' || errno == ERANGE)
        return false;
    out = v;
    return true;
}

bool parseInt(const std::string &s, int &out)
{
    long v = 0;
    if (!parseLong(s, 10, v) || v < INT_MIN || v > INT_MAX)
        return false;
    out = (int)v;
    return true;
}

/// Hexa com ou sem "0x" (TAC / LAC).
bool parseHex(const std::string &s, long &out)
{
    return parseLong(s, 16, out) && out >= 0;
}

/// Decimal, ou hexa se vier com "0x" (Cell ID varia entre firmwares).
bool parseCellId(const std::string &s, long &out)
{
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return parseHex(s, out);
    return parseLong(s, 10, out) && out >= 0;
}

/// Inteiro no início do campo (GSM: "27 EGSM 900" -> 27).
bool parseLeadingInt(const std::string &s, int &out)
{
    size_t i = 0;
    while (i < s.size() && isdigit((unsigned char)s[i]))
        ++i;
    return i > 0 && i <= 9 && parseInt(s.substr(0, i), out);
}

bool isDigits(const std::string &s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (!isdigit((unsigned char)c))
            return false;
    return true;
}

/// "724-05" -> mcc "724", mnc "05"
bool parseMccMnc(const std::string &s, std::string &mcc, std::string &mnc)
{
    size_t dash = s.find('-');
    if (dash == std::string::npos)
        return false;
    mcc = s.substr(0, dash);
    mnc = s.substr(dash + 1);
    return mcc.size() == 3 && isDigits(mcc) &&
           (mnc.size() == 2 || mnc.size() == 3) && isDigits(mnc);
}

CellSystemMode systemModeFromString(const std::string &field)
{
    const std::string u = toUpper(field);
    if (u == "NO SERVICE")
        return CELL_MODE_NO_SERVICE;
    if (u == "GSM")
        return CELL_MODE_GSM;
    if (u.find("CAT-M") != std::string::npos)
        return CELL_MODE_CATM;
    if (u.find("NB-IOT") != std::string::npos)
        return CELL_MODE_NBIOT;
    return CELL_MODE_UNKNOWN;
}

const size_t kLteFieldCount = 14;
const size_t kGsmFieldCount = 9;

CpsiParseStatus parseLte(const std::vector<std::string> &f, CellInfo &c)
{
    if (f.size() != kLteFieldCount)
        return CPSI_ERR_FIELD_COUNT;

    // <MCC>-<MNC>,<TAC>,<SCellID>,<PCellID>,<Frequency Band>,<earfcn>,<dlbw>,<ulbw>,<RSRQ>,<RSRP>,<RSSI>,<RSSNR>
    c.band_raw = f[6];
    c.band = parseBandNumber(f[6]);

    bool ok = parseMccMnc(f[2], c.mcc, c.mnc) &&
              parseHex(f[3], c.tac) &&
              parseCellId(f[4], c.cell_id) &&
              parseInt(f[5], c.pcell_id) &&
              c.band > 0 &&
              parseInt(f[7], c.earfcn) &&
              parseInt(f[8], c.dlbw) &&
              parseInt(f[9], c.ulbw) &&
              parseInt(f[10], c.rsrq) &&
              parseInt(f[11], c.rsrp) &&
              parseInt(f[12], c.rssi) &&
              parseInt(f[13], c.rssnr);
    if (!ok)
        return CPSI_ERR_BAD_FIELD;

    c.sinr = 2 * c.rssnr - 20;
    c.sinr_valid = c.sinr >= -20 && c.sinr <= 30;
    c.registered = true;
    return CPSI_OK;
}

CpsiParseStatus parseGsm(const std::vector<std::string> &f, CellInfo &c)
{
    if (f.size() != kGsmFieldCount)
        return CPSI_ERR_FIELD_COUNT;

    // <MCC>-<MNC>,<LAC>,<Cell ID>,<Absolute RF Ch Num>,<RxLev>,<Track LO Adjust>,<C1-C2>
    bool ok = parseMccMnc(f[2], c.mcc, c.mnc) &&
              parseHex(f[3], c.lac) &&
              parseCellId(f[4], c.cell_id) &&
              parseLeadingInt(f[5], c.arfcn) &&
              parseInt(f[6], c.rxlev) &&
              parseInt(f[7], c.track_lo_adjust);
    if (!ok)
        return CPSI_ERR_BAD_FIELD;

    c.c1_c2 = f[8];
    c.registered = true;
    return CPSI_OK;
}

} // namespace

// ============================================================
// API
// ============================================================

int parseBandNumber(const std::string &field)
{
    const std::string u = toUpper(trim(field));
    size_t i = u.find("BAND");
    i = (i == std::string::npos) ? 0 : i + 4;
    while (i < u.size() && (u[i] == ' ' || u[i] == '-' || u[i] == '_' || u[i] == ':'))
        ++i;

    int band = 0;
    size_t digits = 0;
    while (i < u.size() && isdigit((unsigned char)u[i]))
    {
        if (++digits > 4)
            return -1;
        band = band * 10 + (u[i] - '0');
        ++i;
    }
    return band > 0 ? band : -1;
}

bool isSupportedBand(int band)
{
    static const int kBands[] = {1, 2, 3, 4, 5, 8, 12, 13, 14, 18, 19, 20, 25, 26, 27, 28, 66, 71, 85};
    for (int b : kBands)
        if (b == band)
            return true;
    return false;
}

const char *cellSystemModeName(CellSystemMode mode)
{
    switch (mode)
    {
    case CELL_MODE_NO_SERVICE: return "NO SERVICE";
    case CELL_MODE_GSM:        return "GSM";
    case CELL_MODE_CATM:       return "LTE CAT-M1";
    case CELL_MODE_NBIOT:      return "LTE NB-IOT";
    default:                   return "UNKNOWN";
    }
}

const char *cpsiParseStatusName(CpsiParseStatus status)
{
    switch (status)
    {
    case CPSI_OK:               return "OK";
    case CPSI_ERR_EMPTY:        return "resposta vazia";
    case CPSI_ERR_MODEM:        return "erro do modem";
    case CPSI_ERR_PREFIX:       return "prefixo inesperado";
    case CPSI_ERR_UNKNOWN_MODE: return "System Mode desconhecido";
    case CPSI_ERR_FIELD_COUNT:  return "quantidade de campos inesperada";
    case CPSI_ERR_BAD_FIELD:    return "campo invalido";
    default:                    return "?";
    }
}

CpsiParseStatus parseCpsi(const std::string &line, CellInfo &out)
{
    out = CellInfo();

    std::string s = trim(line);
    if (s.empty())
        return CPSI_ERR_EMPTY;

    const std::string upper = toUpper(s);
    if (startsWith(upper, "ERROR") || startsWith(upper, "+CME ERROR") || startsWith(upper, "+CMS ERROR"))
        return CPSI_ERR_MODEM;

    if (startsWith(upper, "+CPSI:"))
        s = s.substr(6);
    else if (s[0] == '+')
        return CPSI_ERR_PREFIX;

    std::vector<std::string> f = splitFields(s);
    CellInfo c;
    c.system_mode = systemModeFromString(f[0]);
    if (c.system_mode == CELL_MODE_UNKNOWN)
        return CPSI_ERR_UNKNOWN_MODE;
    c.system_mode_name = f[0];
    if (f.size() > 1)
        c.operation_mode = f[1];

    CpsiParseStatus st = CPSI_OK;
    switch (c.system_mode)
    {
    case CELL_MODE_NO_SERVICE:
        c.registered = false; // "+CPSI: NO SERVICE,Online"
        break;
    case CELL_MODE_GSM:
        st = parseGsm(f, c);
        break;
    default: // CAT-M1 / NB-IoT
        st = parseLte(f, c);
        break;
    }

    if (st == CPSI_OK)
        out = c;
    return st;
}

std::string formatCellSummary(const CellInfo &info)
{
    char buf[160];
    switch (info.system_mode)
    {
    case CELL_MODE_CATM:
    case CELL_MODE_NBIOT:
        snprintf(buf, sizeof(buf),
                 "%s | Banda %d | EARFCN %d | RSRP %d dBm | RSRQ %d dB | SINR %d dB | %s-%s",
                 info.system_mode_name.c_str(), info.band, info.earfcn,
                 info.rsrp, info.rsrq, info.sinr, info.mcc.c_str(), info.mnc.c_str());
        break;
    case CELL_MODE_GSM:
        snprintf(buf, sizeof(buf), "%s | ARFCN %d | RxLev %d | LAC 0x%lX | Cell %ld | %s-%s",
                 info.system_mode_name.c_str(), info.arfcn, info.rxlev,
                 info.lac, info.cell_id, info.mcc.c_str(), info.mnc.c_str());
        break;
    case CELL_MODE_NO_SERVICE:
        snprintf(buf, sizeof(buf), "%s | %s", info.system_mode_name.c_str(), info.operation_mode.c_str());
        break;
    default:
        if (info.cereg_stat >= 0)
            snprintf(buf, sizeof(buf), "Nao registrado (CEREG stat=%d)", info.cereg_stat);
        else
            snprintf(buf, sizeof(buf), "Sem informacao de celula");
        break;
    }
    return buf;
}
