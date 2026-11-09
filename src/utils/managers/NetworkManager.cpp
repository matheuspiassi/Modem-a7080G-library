#include "NetworkManager.h"
#include "Logging.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

NetworkManager::NetworkManager(ATCommandManager &at)
    : at(at) {}

// ============================================================
// Estado do módulo
// ============================================================

bool NetworkManager::checkSim(std::string &status) {
    std::string resp;
    if (!at.sendCommand("AT+CPIN?", "+CPIN:", resp, 5000)) {
        LOG_ERROR("NET", "AT+CPIN? failed: %s", resp.c_str());
        status = resp;
        return false;
    }
    auto pos = resp.find(':');
    status = (pos != std::string::npos) ? resp.substr(pos + 1) : resp;
    while (!status.empty() && status[0] == ' ')
        status.erase(0, 1);
    LOG_INFO("NET", "SIM status: %s", status.c_str());
    return status == "READY";
}

bool NetworkManager::setFunctionality(int fun, int rst) {
    std::string cmd = "AT+CFUN=" + std::to_string(fun);
    if (rst)
        cmd += "," + std::to_string(rst);
    LOG_INFO("NET", "Setting functionality: %s", cmd.c_str());
    std::string resp;
    if (!at.sendCommand(cmd, "OK", resp, 15000)) {
        LOG_ERROR("NET", "AT+CFUN failed: %s", resp.c_str());
        return false;
    }
    return true;
}

bool NetworkManager::setNetworkMode(int mode) {
    std::string cmd = "AT+CNMP=" + std::to_string(mode);
    LOG_INFO("NET", "Setting network mode: %s", cmd.c_str());
    std::string resp;
    if (!at.sendCommand(cmd, "OK", resp, 10000)) {
        LOG_ERROR("NET", "AT+CNMP failed: %s", resp.c_str());
        return false;
    }
    return true;
}

bool NetworkManager::setPreferredLteMode(int mode) {
    std::string cmd = "AT+CMNB=" + std::to_string(mode);
    LOG_INFO("NET", "Setting CAT-M/NB-IoT preference: %s", cmd.c_str());
    std::string resp;
    if (!at.sendCommand(cmd, "OK", resp, 10000)) {
        LOG_ERROR("NET", "AT+CMNB failed: %s", resp.c_str());
        return false;
    }
    return true;
}

bool NetworkManager::setBands(const std::string &mode, const std::string &bandList) {
    std::string cmd = "AT+CBANDCFG=\"" + mode + "\"," + bandList;
    LOG_INFO("NET", "Setting bands: %s", cmd.c_str());
    std::string resp;
    if (!at.sendCommand(cmd, "OK", resp, 10000)) {
        LOG_ERROR("NET", "AT+CBANDCFG failed: %s", resp.c_str());
        return false;
    }
    return true;
}

bool NetworkManager::setBands(RadioMode mode, const std::string &bandList) {
    return setBands(mode == RADIO_CATM ? "CAT-M" : "NB-IOT", bandList);
}

bool NetworkManager::setRadioMode(RadioMode mode) {
    if (mode != RADIO_CATM && mode != RADIO_NBIOT) {
        LOG_ERROR("NET", "Invalid radio mode %d", (int)mode);
        return false;
    }
    LOG_INFO("NET", "Locking radio to %s", radioModeName(mode));
    return setNetworkMode(38) &&                                  // somente LTE
           setPreferredLteMode(mode == RADIO_CATM ? 1 : 2);       // 1 CAT-M, 2 NB-IoT
}

bool NetworkManager::setOperatorAutomatic() {
    LOG_INFO("NET", "Automatic operator selection (AT+COPS=0)");
    std::string resp;
    if (!at.sendCommand("AT+COPS=0", "OK", resp, 60000)) {
        LOG_WARN("NET", "AT+COPS=0 failed: %s", resp.c_str());
        return false;
    }
    return true;
}

// ============================================================
// Sinal / registro
// ============================================================

bool NetworkManager::getSignalQuality(int &rssi, int &ber) {
    LOG_INFO("NET", "getSignalQuality() called");
    std::string resp;
    if (!at.sendCommand("AT+CSQ", "+CSQ:", resp, 5000)) {
        LOG_ERROR("NET", "AT+CSQ failed: %s", resp.c_str());
        return false;
    }
    if (sscanf(resp.c_str(), "+CSQ: %d,%d", &rssi, &ber) == 2) {
        LOG_INFO("NET", "Signal OK: RSSI=%d, BER=%d", rssi, ber);
        return true;
    }
    LOG_ERROR("NET", "Failed to parse CSQ");
    return false;
}

bool NetworkManager::getRegistrationStatus(int &n, int &stat) {
    LOG_INFO("NET", "getRegistrationStatus() called");
    std::string resp;

    // CAT-M e NB-IoT registram em EPS (CEREG)
    if (!at.sendCommand("AT+CEREG?", "+CEREG:", resp, 5000)) {
        LOG_ERROR("NET", "AT+CEREG? failed: %s", resp.c_str());
        return false;
    }

    // Resposta: +CEREG: <n>,<stat>[,...]
    // URC (com AT+CEREG=1/2 ativo) que pode chegar no lugar: +CEREG: <stat>[,"tac",...]
    int a = -1, b = -1;
    int matched = sscanf(resp.c_str(), "+CEREG: %d,%d", &a, &b);
    if (matched == 2) {
        n = a;
        stat = b;
    } else if (matched == 1) {
        n = -1;
        stat = a;
    } else {
        LOG_ERROR("NET", "Failed to parse CEREG: %s", resp.c_str());
        return false;
    }
    LOG_INFO("NET", "EPS registration: n=%d, stat=%d", n, stat);
    return true;
}

bool NetworkManager::isRegistered() {
    int n = 0, stat = 0;
    return getRegistrationStatus(n, stat) && (stat == 1 || stat == 5);
}

RadioMode NetworkManager::getActiveMode() {
    std::string info;
    // Ex.: +CPSI: LTE CAT-M1,Online,...  /  +CPSI: LTE NB-IOT,Online,...
    if (!getSystemInfo(info))
        return RADIO_UNKNOWN;
    if (info.find("NB-IOT") != std::string::npos)
        return RADIO_NBIOT;
    if (info.find("CAT-M") != std::string::npos)
        return RADIO_CATM;
    return RADIO_UNKNOWN;
}

bool NetworkManager::getSystemInfo(std::string &info) {
    std::string resp;
    if (!at.sendCommand("AT+CPSI?", "+CPSI:", resp, 5000)) {
        LOG_ERROR("NET", "AT+CPSI? failed: %s", resp.c_str());
        return false;
    }
    info = resp.substr(strlen("+CPSI:"));
    while (!info.empty() && info[0] == ' ')
        info.erase(0, 1);
    LOG_INFO("NET", "System info: %s", info.c_str());
    return true;
}

bool NetworkManager::getNetworkApn(std::string &apn) {
    std::string resp;
    if (!at.sendCommand("AT+CGNAPN", "+CGNAPN:", resp, 5000)) {
        LOG_ERROR("NET", "AT+CGNAPN failed: %s", resp.c_str());
        return false;
    }
    int valid = 0;
    char buf[101] = {0};
    int n = sscanf(resp.c_str(), "+CGNAPN: %d,\"%100[^\"]\"", &valid, buf);
    if (n < 1 || valid != 1) {
        LOG_WARN("NET", "Network did not provide an APN");
        apn.clear();
        return false;
    }
    apn = buf;

    // Algumas redes devolvem o APN completo (APN-NI + APN-OI):
    // "apn.operadora.mnc010.mcc724.gprs" -> "apn.operadora"
    std::string lower = apn;
    for (auto &c : lower)
        c = (char)tolower((unsigned char)c);
    size_t mnc = lower.find(".mnc");
    if (mnc != std::string::npos && lower.find(".mcc", mnc) != std::string::npos)
        apn.erase(mnc);

    if (apn.empty()) {
        LOG_WARN("NET", "Network APN is empty");
        return false;
    }
    LOG_INFO("NET", "Network APN: %s", apn.c_str());
    return true;
}

// ============================================================
// Contexto de dados
// ============================================================

bool NetworkManager::setApnSession(const std::string &apn) {
    return definePDPContext(1, "IP", apn);
}

bool NetworkManager::definePDPContext(int cid,
                                      const std::string &pdpType,
                                      const std::string &apn,
                                      const std::string &pdpAddr,
                                      int dComp,
                                      int hComp,
                                      int ipv4Ctrl,
                                      int /*requestType*/) {
    std::ostringstream cmd;
    cmd << "AT+CGDCONT=" << cid << ",\"" << pdpType << "\",\"" << apn << "\"";
    if (!pdpAddr.empty() || dComp >= 0 || hComp >= 0 || ipv4Ctrl >= 0) {
        cmd << ",\"" << pdpAddr << "\"";
        cmd << "," << (dComp >= 0 ? dComp : 0);
        cmd << "," << (hComp >= 0 ? hComp : 0);
        if (ipv4Ctrl >= 0)
            cmd << "," << ipv4Ctrl;
    }
    LOG_INFO("NET", "Setting APN session: %s", cmd.str().c_str());
    std::string resp;
    if (!at.sendCommand(cmd.str(), "OK", resp, 5000)) {
        LOG_ERROR("NET", "AT+CGDCONT failed: %s", resp.c_str());
        return false;
    }
    return true;
}

bool NetworkManager::setAuthentication(int cid,
                                       int authType,
                                       const std::string &password,
                                       const std::string &username) {
    std::ostringstream cmd;
    cmd << "AT+CGAUTH=" << cid << "," << authType;
    if (!password.empty() || !username.empty()) cmd << ",\"" << password << "\"";
    if (!username.empty()) cmd << ",\"" << username << "\"";
    LOG_INFO("NET", "Setting authentication (cid=%d, type=%d)", cid, authType);
    std::string resp;
    if (!at.sendCommand(cmd.str(), "OK", resp, 10000)) {
        LOG_ERROR("NET", "AT+CGAUTH failed: %s", resp.c_str());
        return false;
    }
    return true;
}

bool NetworkManager::attachGPRS(bool attach) {
    std::string cmd = "AT+CGATT=" + std::to_string(attach ? 1 : 0);
    LOG_INFO("NET", "Attaching packet service: %s", cmd.c_str());

    std::string resp;
    // Max response time do manual: 75 s
    if (!at.sendCommand(cmd, "OK", resp, 75000)) {
        LOG_ERROR("NET", "AT+CGATT=set failed: %s", resp.c_str());
        return false;
    }

    for (int attempt = 1; attempt <= 5; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        LOG_INFO("NET", "Checking attach status (attempt %d)", attempt);

        int status = -1;
        if (at.sendCommand("AT+CGATT?", "+CGATT:", resp, 5000) &&
            sscanf(resp.c_str(), "+CGATT: %d", &status) == 1) {
            if (status == (attach ? 1 : 0)) {
                LOG_INFO("NET", attach ? "Packet service attached" : "Packet service detached");
                return true;
            }
            LOG_WARN("NET", "CGATT status = %d", status);
        } else {
            LOG_WARN("NET", "AT+CGATT? failed: %s", resp.c_str());
        }
    }

    LOG_ERROR("NET", "Attach failed after retries");
    return false;
}

bool NetworkManager::configurePDP(int pdpIdx,
                                  const std::string &apn,
                                  const std::string &user,
                                  const std::string &password,
                                  int authType,
                                  int ipType) {
    if (authType < 0)
        authType = user.empty() ? 0 : 3; // 3 = PAP ou CHAP

    std::ostringstream cmd;
    cmd << "AT+CNCFG=" << pdpIdx << "," << ipType << ",\"" << apn << "\"";
    if (!user.empty() || !password.empty() || authType != 0)
        cmd << ",\"" << user << "\",\"" << password << "\"," << authType;

    LOG_INFO("NET", "Configuring app PDP %d (APN=%s, auth=%d)", pdpIdx, apn.c_str(), authType);
    std::string resp;
    if (!at.sendCommand(cmd.str(), "OK", resp, 10000)) {
        LOG_ERROR("NET", "AT+CNCFG failed: %s", resp.c_str());
        return false;
    }
    return true;
}

bool NetworkManager::isContextActive(int pdpIdx, std::string *address) {
    // AT+CNACT? lista todos os contextos: +CNACT: <pdpidx>,<status>,"<ip>"
    std::string prefix = "+CNACT: " + std::to_string(pdpIdx) + ",";
    std::string resp;
    if (!at.sendCommand("AT+CNACT?", prefix.c_str(), resp, 5000)) {
        LOG_WARN("NET", "AT+CNACT? failed: %s", resp.c_str());
        return false;
    }
    int idx = -1, status = 0;
    char ip[64] = {0};
    int n = sscanf(resp.c_str(), "+CNACT: %d,%d,\"%63[^\"]\"", &idx, &status, ip);
    if (n < 2) {
        LOG_WARN("NET", "Failed to parse CNACT: %s", resp.c_str());
        return false;
    }
    if (address)
        *address = ip;
    return status == 1;
}

bool NetworkManager::activateContext(int pdpIdx) {
    std::string ip;
    if (isContextActive(pdpIdx, &ip)) {
        LOG_INFO("NET", "App PDP %d already active (IP %s)", pdpIdx, ip.c_str());
        return true;
    }

    std::string cmd = "AT+CNACT=" + std::to_string(pdpIdx) + ",1";
    LOG_INFO("NET", "Activating app PDP: %s", cmd.c_str());
    std::string resp;
    // Após o OK o modem envia o URC "+APP PDP: <pdpidx>,ACTIVE"
    if (!at.sendCommand(cmd, "+APP PDP:", resp, 30000)) {
        LOG_WARN("NET", "No +APP PDP URC (%s), checking state...", resp.c_str());
    } else if (resp.find("DEACTIVE") != std::string::npos) {
        LOG_WARN("NET", "App PDP reported DEACTIVE: %s", resp.c_str());
    }

    for (int attempt = 1; attempt <= 5; ++attempt) {
        if (isContextActive(pdpIdx, &ip)) {
            LOG_INFO("NET", "App PDP %d activated (IP %s)", pdpIdx, ip.c_str());
            return true;
        }
        LOG_INFO("NET", "Checking PDP status (attempt %d)", attempt);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    LOG_ERROR("NET", "App PDP activation failed");
    return false;
}

bool NetworkManager::deactivateContext(int pdpIdx) {
    std::string cmd = "AT+CNACT=" + std::to_string(pdpIdx) + ",0";
    LOG_INFO("NET", "Deactivating app PDP: %s", cmd.c_str());
    std::string resp;
    if (!at.sendCommand(cmd, "+APP PDP:", resp, 15000)) {
        // Pode já estar desativado
        return !isContextActive(pdpIdx);
    }
    return true;
}

bool NetworkManager::getPDPAddress(int pdpIdx, std::string &address) {
    LOG_INFO("NET", "Requesting PDP address");
    if (!isContextActive(pdpIdx, &address) || address.empty()) {
        LOG_ERROR("NET", "No IP for app PDP %d", pdpIdx);
        return false;
    }
    LOG_INFO("NET", "Obtained IP address: %s", address.c_str());
    return true;
}

bool NetworkManager::init(const std::string &apn,
                          const std::string &user,
                          const std::string &password) {
    std::string useApn = apn;
    if (useApn.empty() && !getNetworkApn(useApn)) {
        LOG_WARN("NET", "No APN from network, using empty APN (default bearer)");
        useApn.clear();
    }
    if (!configurePDP(0, useApn, user, password))
        return false;
    return activateContext(0);
}

// ============================================================
// Hora
// ============================================================

bool NetworkManager::syncTimeNtp(const std::string &host, int tz_quarters, int exec_timeout_ms, int pdpIdx) {
    std::string resp;

    // Contexto usado pelo NTP (<pdpidx> do AT+CNACT)
    std::string cidCmd = "AT+CNTPCID=" + std::to_string(pdpIdx);
    if (!at.sendCommand(cidCmd, "OK", resp, 5000)) {
        LOG_WARN("NET", "AT+CNTPCID failed: %s", resp.c_str());
    }

    // Servidor, fuso (quartos de hora, ex.: -12 = UTC-3), cid e modo 2 (ajusta hora local e informa UTC)
    std::ostringstream setCmd;
    setCmd << "AT+CNTP=\"" << host << "\"," << tz_quarters << "," << pdpIdx << ",2";
    if (!at.sendCommand(setCmd.str(), "OK", resp, 10000)) {
        LOG_ERROR("NET", "AT+CNTP=<host,tz> failed: %s", resp.c_str());
        return false;
    }

    // Executa sincronização: OK e depois +CNTP: <code>[,<time>]
    if (!at.sendCommand("AT+CNTP", "+CNTP:", resp, exec_timeout_ms)) {
        LOG_ERROR("NET", "AT+CNTP (exec) failed/timeout: %s", resp.c_str());
        return false;
    }

    int code = 0;
    if (sscanf(resp.c_str(), "+CNTP: %d", &code) != 1) {
        LOG_ERROR("NET", "Failed to parse +CNTP:<code>");
        return false;
    }

    // No SIM7080G o código de sucesso é 1 (61..65 = erro)
    if (code == 1) {
        LOG_INFO("NET", "NTP sync OK");
        return true;
    }
    LOG_ERROR("NET", "NTP sync error: %d", code);
    return false;
}

bool NetworkManager::timeZoneUpdate() {
    std::string resp;

    // Habilita atualização de hora/fuso pela rede (URCs *PSUTTZ, +CTZV, DST)
    if (!at.sendCommand("AT+CLTS=1", "OK", resp, 10000)) {
        LOG_ERROR("NET", "AT+CLTS=1 failed: %s", resp.c_str());
        return false;
    }

    // Persiste em NVRAM
    at.sendCommand("AT&W", "OK", resp, 5000);
    return true;
}

NetworkManager::CclkResult NetworkManager::infoDateAndTime() {
    CclkResult out; // começa inválido por padrão
    std::string resp;

    // Pede o relógio do modem
    if (!at.sendCommand("AT+CCLK?", "+CCLK:", resp, 15000)) {
        LOG_ERROR("NET", "AT+CCLK? failed: %s", resp.c_str());
        return out;
    }

    // Localiza +CCLK: "..."
    auto q1 = resp.find('"');
    auto q2 = (q1 == std::string::npos) ? std::string::npos : resp.find('"', q1 + 1);
    if (q1 == std::string::npos || q2 == std::string::npos || q2 <= q1 + 1) {
        LOG_ERROR("NET", "CCLK quotes not found");
        return out;
    }

    std::string ts = resp.substr(q1 + 1, q2 - q1 - 1);
    // Formato: "yy/MM/dd,hh:mm:ss±zz"   (±zz em quartos de hora)
    int yy = 0, MM = 0, dd = 0, hh = 0, mm = 0, ss = 0;
    char tzsign = '+';
    int tzz = 0;  // valor absoluto; sinal em tzsign

    int matched = std::sscanf(ts.c_str(), "%2d/%2d/%2d,%2d:%2d:%2d%c%d",
                              &yy, &MM, &dd, &hh, &mm, &ss, &tzsign, &tzz);

    if (matched < 6) {
        LOG_ERROR("NET", "Failed to parse CCLK payload: %s", ts.c_str());
        return out;
    }
    if (matched < 8) tzz = 0; // sem fuso informado

    // Verifica faixas básicas
    if (MM < 1 || MM > 12 || dd < 1 || dd > 31 || hh < 0 || hh > 23 ||
        mm < 0 || mm > 59 || ss < 0 || ss > 59) {
        LOG_ERROR("NET", "CCLK fields out of range: %s", ts.c_str());
        return out;
    }

    out.year   = 2000 + yy;
    out.month  = MM;
    out.day    = dd;
    out.hour   = hh;
    out.minute = mm;
    out.second = ss;

    if (tzz < 0) tzz = -tzz; // força absoluto; sinal está em tzsign
    int quarters = (tzsign == '-') ? -tzz : tzz;
    if (quarters < -96) quarters = -96;
    if (quarters >  96) quarters =  96;

    out.tz_quarters = quarters;
    out.tz_minutes  = quarters * 15;
    out.tz_sign     = (quarters == 0) ? 0 : (quarters > 0 ? 1 : -1);

    // ISO-8601 com offset
    int offMinAbs = (out.tz_minutes >= 0) ? out.tz_minutes : -out.tz_minutes;
    int offH = offMinAbs / 60;
    int offM = offMinAbs % 60;

    std::snprintf(out.iso8601, sizeof(out.iso8601),
                  "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                  out.year, out.month, out.day,
                  out.hour, out.minute, out.second,
                  (out.tz_minutes < 0 ? '-' : '+'),
                  offH, offM);

    out.valid = true;

    LOG_INFO("NET", "CCLK parsed -> %s (tz_quarters=%d)", out.iso8601, out.tz_quarters);

    return out;
}

// ============================================================
// SSL / certificados
// ============================================================

bool NetworkManager::configureSSL(bool enableTLS,
                                  int sslCtxIndex,
                                  bool enableSNI,
                                  const std::string &sniHost) {
    if (!enableTLS) {
        LOG_INFO("NET", "TLS desabilitado. Nenhuma configuração SSL aplicada.");
        return true;
    }
    std::string resp;
    const std::string ctx = std::to_string(sslCtxIndex);

    // TLS 1.2 (o padrão do módulo é TLS 1.0, recusado pela maioria dos brokers)
    if (!at.sendCommand("AT+CSSLCFG=\"SSLVERSION\"," + ctx + ",3", "OK", resp, 10000)) {
        LOG_ERROR("NET", "Falha SSLVERSION: %s", resp.c_str());
        return false;
    }

    // Não valida validade do certificado contra o RTC (útil antes do NTP)
    if (!at.sendCommand("AT+CSSLCFG=\"IGNORERTCTIME\"," + ctx + ",1", "OK", resp, 10000)) {
        LOG_WARN("NET", "Falha IGNORERTCTIME: %s", resp.c_str());
    }

    if (enableSNI && !sniHost.empty()) {
        if (!at.sendCommand("AT+CSSLCFG=\"SNI\"," + ctx + ",\"" + sniHost + "\"", "OK", resp, 10000)) {
            LOG_WARN("NET", "Falha SNI: %s", resp.c_str());
        }
    }
    return true;
}

bool NetworkManager::uploadFile(int dirIndex, const std::string &fileName, const std::string &content) {
    if (content.size() >= 10240) {
        LOG_ERROR("NET", "File too large (%u bytes, max 10239)", (unsigned)content.size());
        return false;
    }
    std::string resp;
    at.sendCommand("AT+CFSTERM", "OK", resp, 3000); // libera buffer de uma operação anterior

    if (!at.sendCommand("AT+CFSINIT", "OK", resp, 5000)) {
        LOG_ERROR("NET", "AT+CFSINIT failed: %s", resp.c_str());
        return false;
    }

    const int inputTimeMs = 10000;
    std::string cmd = "AT+CFSWFILE=" + std::to_string(dirIndex) + ",\"" + fileName + "\",0," +
                      std::to_string(content.size()) + "," + std::to_string(inputTimeMs);
    bool ok = at.sendCommandWithData(cmd, "DOWNLOAD", content, "OK", resp, inputTimeMs + 5000);
    if (!ok)
        LOG_ERROR("NET", "AT+CFSWFILE failed: %s", resp.c_str());
    else
        LOG_INFO("NET", "File %s written (%u bytes)", fileName.c_str(), (unsigned)content.size());

    at.sendCommand("AT+CFSTERM", "OK", resp, 3000);
    return ok;
}

bool NetworkManager::convertCertificate(int sslType,
                                        const std::string &certName,
                                        const std::string &keyName,
                                        const std::string &passKey) {
    std::string cmd = "AT+CSSLCFG=\"CONVERT\"," + std::to_string(sslType) + ",\"" + certName + "\"";
    if (!keyName.empty()) {
        cmd += ",\"" + keyName + "\"";
        if (!passKey.empty())
            cmd += ",\"" + passKey + "\"";
    }
    std::string resp;
    if (!at.sendCommand(cmd, "OK", resp, 10000)) {
        LOG_ERROR("NET", "CONVERT failed: %s", resp.c_str());
        return false;
    }
    LOG_INFO("NET", "Certificate %s converted", certName.c_str());
    return true;
}
