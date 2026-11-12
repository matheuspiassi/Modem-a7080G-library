/**
 * Testes do parser de AT+CPSI? (CellInfo.cpp). Rodam no PC, sem Arduino:
 *
 *   g++ -std=c++11 -Wall -Wextra -Isrc/utils/managers \
 *       test/host/cpsi_parser_test.cpp src/utils/managers/CellInfo.cpp -o cpsi_test
 *   ./cpsi_test
 *
 * Código de saída 0 = todos passaram.
 */
#include "CellInfo.h"
#include <cstdio>
#include <cstring>
#include <string>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                    \
    } while (0)

#define CHECK_EQ_INT(expected, actual)                                       \
    do {                                                                     \
        ++g_checks;                                                          \
        long e_ = (long)(expected), a_ = (long)(actual);                     \
        if (e_ != a_) {                                                      \
            ++g_failures;                                                    \
            printf("  FALHOU %s:%d: %s == %ld, esperado %ld\n",              \
                   __FILE__, __LINE__, #actual, a_, e_);                     \
        }                                                                    \
    } while (0)

#define CHECK_EQ_STR(expected, actual)                                       \
    do {                                                                     \
        ++g_checks;                                                          \
        std::string e_ = (expected), a_ = (actual);                          \
        if (e_ != a_) {                                                      \
            ++g_failures;                                                    \
            printf("  FALHOU %s:%d: %s == \"%s\", esperado \"%s\"\n",        \
                   __FILE__, __LINE__, #actual, a_.c_str(), e_.c_str());     \
        }                                                                    \
    } while (0)

#define RUN(test)                    \
    do {                             \
        int before_ = g_failures;    \
        test();                      \
        printf("%s %s\n", g_failures == before_ ? "[ OK ]" : "[FAIL]", #test); \
    } while (0)

// ------------------------------------------------------------
// Casos pedidos
// ------------------------------------------------------------

static void test_catm_example()
{
    CellInfo c;
    CpsiParseStatus st = parseCpsi(
        "+CPSI: LTE CAT-M1,Online,724-05,0x1A2B,12345678,301,EUTRAN-BAND28,9410,3,3,-10,-95,-65,15", c);
    CHECK_EQ_INT(CPSI_OK, st);
    CHECK(c.registered);
    CHECK_EQ_INT(CELL_MODE_CATM, c.system_mode);
    CHECK_EQ_STR("LTE CAT-M1", c.system_mode_name);
    CHECK_EQ_STR("Online", c.operation_mode);
    CHECK_EQ_STR("724", c.mcc);
    CHECK_EQ_STR("05", c.mnc);
    CHECK_EQ_INT(0x1A2B, c.tac);
    CHECK_EQ_INT(12345678, c.cell_id);
    CHECK_EQ_INT(301, c.pcell_id);
    CHECK_EQ_INT(28, c.band);
    CHECK_EQ_STR("EUTRAN-BAND28", c.band_raw);
    CHECK_EQ_INT(9410, c.earfcn);
    CHECK_EQ_INT(3, c.dlbw);
    CHECK_EQ_INT(3, c.ulbw);
    CHECK_EQ_INT(-10, c.rsrq);
    CHECK_EQ_INT(-95, c.rsrp);
    CHECK_EQ_INT(-65, c.rssi);
    CHECK_EQ_INT(15, c.rssnr);
    CHECK_EQ_INT(10, c.sinr);
    CHECK(c.sinr_valid);
    CHECK_EQ_INT(-1, c.lac);
    CHECK_EQ_STR("LTE CAT-M1 | Banda 28 | EARFCN 9410 | RSRP -95 dBm | RSRQ -10 dB | SINR 10 dB | 724-05",
                 formatCellSummary(c));
}

static void test_nbiot_band3()
{
    CellInfo c;
    CpsiParseStatus st = parseCpsi(
        "+CPSI: LTE NB-IOT,Online,724-05,0x1A2B,12345678,301,EUTRAN-BAND3,1575,0,0,-11,-101,-70,12", c);
    CHECK_EQ_INT(CPSI_OK, st);
    CHECK(c.registered);
    CHECK_EQ_INT(CELL_MODE_NBIOT, c.system_mode);
    CHECK_EQ_INT(3, c.band);
    CHECK_EQ_INT(1575, c.earfcn);
    CHECK_EQ_INT(4, c.sinr);
}

static void test_no_service()
{
    CellInfo c;
    c.band = 99; // deve ser sobrescrito
    CpsiParseStatus st = parseCpsi("+CPSI: NO SERVICE,Online", c);
    CHECK_EQ_INT(CPSI_OK, st);
    CHECK(!c.registered);
    CHECK_EQ_INT(CELL_MODE_NO_SERVICE, c.system_mode);
    CHECK_EQ_STR("Online", c.operation_mode);
    CHECK_EQ_INT(-1, c.band);
    CHECK(c.band_raw.empty());
    CHECK_EQ_STR("NO SERVICE | Online", formatCellSummary(c));
}

static void test_gsm()
{
    CellInfo c;
    CpsiParseStatus st = parseCpsi("+CPSI: GSM,Online,724-05,0x182d,12401,27 EGSM 900,-64,2110,42-42", c);
    CHECK_EQ_INT(CPSI_OK, st);
    CHECK(c.registered);
    CHECK_EQ_INT(CELL_MODE_GSM, c.system_mode);
    CHECK_EQ_INT(-1, c.band);
    CHECK_EQ_STR("724", c.mcc);
    CHECK_EQ_STR("05", c.mnc);
    CHECK_EQ_INT(0x182D, c.lac);
    CHECK_EQ_INT(12401, c.cell_id);
    CHECK_EQ_INT(27, c.arfcn);
    CHECK_EQ_INT(-64, c.rxlev);
    CHECK_EQ_INT(2110, c.track_lo_adjust);
    CHECK_EQ_STR("42-42", c.c1_c2);
    CHECK_EQ_INT(-1, c.tac);
    CHECK_EQ_INT(-1, c.earfcn);
}

static void test_cme_error()
{
    CellInfo c;
    CHECK_EQ_INT(CPSI_ERR_MODEM, parseCpsi("+CME ERROR: 3", c));
    CHECK(!c.registered);
    CHECK_EQ_INT(-1, c.band);
    CHECK_EQ_INT(CPSI_ERR_MODEM, parseCpsi("ERROR", c));
}

static void test_truncated_line()
{
    CellInfo c;
    CHECK_EQ_INT(CPSI_ERR_FIELD_COUNT,
                 parseCpsi("+CPSI: LTE CAT-M1,Online,724-05,0x1A2B,12345678,301,EUTRAN-BAND28,9410", c));
    CHECK(!c.registered);
    CHECK_EQ_INT(-1, c.band);
    CHECK_EQ_INT(CPSI_ERR_FIELD_COUNT, parseCpsi("+CPSI: LTE CAT-M1", c));
    CHECK_EQ_INT(CPSI_ERR_FIELD_COUNT, parseCpsi("+CPSI: GSM,Online,724-05,0x182d", c));
    // campo a mais também é rejeitado
    CHECK_EQ_INT(CPSI_ERR_FIELD_COUNT,
                 parseCpsi("+CPSI: LTE CAT-M1,Online,724-05,0x1A2B,12345678,301,EUTRAN-BAND28,9410,3,3,-10,-95,-65,15,9", c));
}

// ------------------------------------------------------------
// Robustez
// ------------------------------------------------------------

static void test_crlf_and_spaces()
{
    CellInfo c;
    CpsiParseStatus st = parseCpsi(
        "\r\n+CPSI: LTE CAT-M1, Online ,724-05, 0x1A2B,12345678,301, EUTRAN-BAND28 ,9410,3,3,-10,-95,-65,15\r\n", c);
    CHECK_EQ_INT(CPSI_OK, st);
    CHECK_EQ_STR("Online", c.operation_mode);
    CHECK_EQ_INT(28, c.band);
    CHECK_EQ_INT(15, c.rssnr);
}

static void test_without_prefix()
{
    CellInfo c;
    CHECK_EQ_INT(CPSI_OK, parseCpsi(
        "LTE CAT-M1,Online,724-05,0x1A2B,12345678,301,EUTRAN-BAND28,9410,3,3,-10,-95,-65,15", c));
    CHECK_EQ_INT(28, c.band);
}

static void test_bad_fields()
{
    CellInfo c;
    // RSRP não numérico
    CHECK_EQ_INT(CPSI_ERR_BAD_FIELD, parseCpsi(
        "+CPSI: LTE CAT-M1,Online,724-05,0x1A2B,12345678,301,EUTRAN-BAND28,9410,3,3,-10,abc,-65,15", c));
    CHECK(!c.registered);
    // banda sem número
    CHECK_EQ_INT(CPSI_ERR_BAD_FIELD, parseCpsi(
        "+CPSI: LTE CAT-M1,Online,724-05,0x1A2B,12345678,301,EUTRAN-BAND,9410,3,3,-10,-95,-65,15", c));
    // MCC-MNC inválido
    CHECK_EQ_INT(CPSI_ERR_BAD_FIELD, parseCpsi(
        "+CPSI: LTE CAT-M1,Online,72405,0x1A2B,12345678,301,EUTRAN-BAND28,9410,3,3,-10,-95,-65,15", c));
    // campo vazio
    CHECK_EQ_INT(CPSI_ERR_BAD_FIELD, parseCpsi(
        "+CPSI: LTE CAT-M1,Online,724-05,0x1A2B,12345678,301,EUTRAN-BAND28,,3,3,-10,-95,-65,15", c));
}

static void test_other_errors()
{
    CellInfo c;
    CHECK_EQ_INT(CPSI_ERR_EMPTY, parseCpsi("", c));
    CHECK_EQ_INT(CPSI_ERR_EMPTY, parseCpsi("\r\n", c));
    CHECK_EQ_INT(CPSI_ERR_PREFIX, parseCpsi("+CSQ: 20,99", c));
    CHECK_EQ_INT(CPSI_ERR_UNKNOWN_MODE, parseCpsi("+CPSI: WCDMA,Online,724-05", c));
    CHECK_EQ_INT(CPSI_ERR_UNKNOWN_MODE, parseCpsi("+CPSI:", c));
}

static void test_band_number_variants()
{
    CHECK_EQ_INT(28, parseBandNumber("EUTRAN-BAND28"));
    CHECK_EQ_INT(28, parseBandNumber("BAND28"));
    CHECK_EQ_INT(28, parseBandNumber("band28"));
    CHECK_EQ_INT(28, parseBandNumber("BAND 28"));
    CHECK_EQ_INT(28, parseBandNumber("28"));
    CHECK_EQ_INT(3, parseBandNumber(" EUTRAN-BAND3 "));
    CHECK_EQ_INT(85, parseBandNumber("EUTRAN-BAND85"));
    CHECK_EQ_INT(-1, parseBandNumber("EUTRAN-BAND"));
    CHECK_EQ_INT(-1, parseBandNumber(""));
    CHECK_EQ_INT(-1, parseBandNumber("0"));
    CHECK_EQ_INT(-1, parseBandNumber("BAND123456"));
}

static void test_supported_bands()
{
    CHECK(isSupportedBand(28));
    CHECK(isSupportedBand(3));
    CHECK(isSupportedBand(85));
    CHECK(!isSupportedBand(7));
    CHECK(!isSupportedBand(-1));
}

static void test_sinr_range()
{
    CellInfo c;
    // RSSNR 0 -> SINR -20 (limite inferior válido)
    parseCpsi("+CPSI: LTE CAT-M1,Online,724-05,0x1A2B,1,1,EUTRAN-BAND28,9410,3,3,-10,-95,-65,0", c);
    CHECK_EQ_INT(-20, c.sinr);
    CHECK(c.sinr_valid);
    // RSSNR 25 -> SINR 30 (limite superior válido)
    parseCpsi("+CPSI: LTE CAT-M1,Online,724-05,0x1A2B,1,1,EUTRAN-BAND28,9410,3,3,-10,-95,-65,25", c);
    CHECK_EQ_INT(30, c.sinr);
    CHECK(c.sinr_valid);
    // RSSNR 26 -> SINR 32 (fora da faixa)
    parseCpsi("+CPSI: LTE CAT-M1,Online,724-05,0x1A2B,1,1,EUTRAN-BAND28,9410,3,3,-10,-95,-65,26", c);
    CHECK_EQ_INT(32, c.sinr);
    CHECK(!c.sinr_valid);
}

static void test_summary_not_registered()
{
    CellInfo c;
    c.cereg_stat = 2;
    CHECK_EQ_STR("Nao registrado (CEREG stat=2)", formatCellSummary(c));
}

int main()
{
    RUN(test_catm_example);
    RUN(test_nbiot_band3);
    RUN(test_no_service);
    RUN(test_gsm);
    RUN(test_cme_error);
    RUN(test_truncated_line);
    RUN(test_crlf_and_spaces);
    RUN(test_without_prefix);
    RUN(test_bad_fields);
    RUN(test_other_errors);
    RUN(test_band_number_variants);
    RUN(test_supported_bands);
    RUN(test_sinr_range);
    RUN(test_summary_not_registered);

    printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
