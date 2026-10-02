#include "GNSSManager.h"
#include "Logging.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

GNSSManager::GNSSManager(ATCommandManager &at)
    : at(at) {}

bool GNSSManager::simpleCommand(const char *cmd, uint32_t timeout)
{
    std::string resp;
    if (!at.sendCommand(cmd, "OK", resp, timeout))
    {
        LOG_ERROR("GNSS", "%s failed: %s", cmd, resp.c_str());
        return false;
    }
    return true;
}

bool GNSSManager::powerOn()
{
    LOG_INFO("GNSS", "Powering on GNSS");
    return simpleCommand("AT+CGNSPWR=1");
}

bool GNSSManager::powerOff()
{
    LOG_INFO("GNSS", "Powering off GNSS");
    return simpleCommand("AT+CGNSPWR=0");
}

int GNSSManager::powerState()
{
    std::string resp;
    int mode = -1;
    if (!at.sendCommand("AT+CGNSPWR?", "+CGNSPWR:", resp, 5000) ||
        sscanf(resp.c_str(), "+CGNSPWR: %d", &mode) != 1)
        return -1;
    return mode;
}

bool GNSSManager::setWorkMode(bool glonass, bool beidou, bool galileo, bool qzss)
{
    int extra = (glonass ? 1 : 0) + (beidou ? 1 : 0) + (galileo ? 1 : 0) + (qzss ? 1 : 0);
    if (extra > 1)
    {
        LOG_ERROR("GNSS", "Only one of GLONASS/BEIDOU/GALILEO/QZSS can be enabled");
        return false;
    }
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "AT+CGNSMOD=1,%d,%d,%d,%d",
             glonass ? 1 : 0, beidou ? 1 : 0, galileo ? 1 : 0, qzss ? 1 : 0);
    LOG_INFO("GNSS", "Work mode: %s (effective after reboot)", cmd);
    return simpleCommand(cmd);
}

bool GNSSManager::coldStart() { return simpleCommand("AT+CGNSCOLD"); }
bool GNSSManager::warmStart() { return simpleCommand("AT+CGNSWARM"); }
bool GNSSManager::hotStart() { return simpleCommand("AT+CGNSHOT"); }

bool GNSSManager::setXtra(bool enable)
{
    return simpleCommand(enable ? "AT+CGNSXTRA=1" : "AT+CGNSXTRA=0");
}

bool GNSSManager::copyXtra()
{
    std::string resp;
    int ret = -1;
    if (!at.sendCommand("AT+CGNSCPY", "+CGNSCPY:", resp, 10000) ||
        sscanf(resp.c_str(), "+CGNSCPY: %d", &ret) != 1)
    {
        LOG_ERROR("GNSS", "AT+CGNSCPY failed: %s", resp.c_str());
        return false;
    }
    if (ret != 0)
        LOG_WARN("GNSS", "XTRA file does not exist");
    return ret == 0;
}

bool GNSSManager::parseCgnsinf(const std::string &line, GnssInfo &info)
{
    info = GnssInfo();

    auto colon = line.find(':');
    if (colon == std::string::npos)
        return false;

    // Divide mantendo campos vazios (",,")
    std::vector<std::string> f;
    std::string cur;
    for (size_t i = colon + 1; i < line.size(); ++i)
    {
        char c = line[i];
        if (c == ',')
        {
            f.push_back(cur);
            cur.clear();
        }
        else if (c != ' ' && c != '\r')
        {
            cur.push_back(c);
        }
    }
    f.push_back(cur);

    if (f.size() < 2)
        return false;

    auto toD = [&f](size_t i) -> double { return (i < f.size() && !f[i].empty()) ? strtod(f[i].c_str(), nullptr) : 0.0; };
    auto toI = [&f](size_t i) -> int { return (i < f.size() && !f[i].empty()) ? atoi(f[i].c_str()) : 0; };

    // Índices conforme tabela 8-1 (base 0)
    info.running = toI(0) == 1;
    info.fixed = toI(1) == 1;
    if (f.size() > 2 && !f[2].empty())
    {
        strncpy(info.utc, f[2].c_str(), sizeof(info.utc) - 1);
        sscanf(f[2].c_str(), "%4d%2d%2d%2d%2d%f",
               &info.year, &info.month, &info.day, &info.hour, &info.minute, &info.second);
    }
    info.latitude = toD(3);
    info.longitude = toD(4);
    info.altitude = (float)toD(5);
    info.speedKmh = (float)toD(6);
    info.course = (float)toD(7);
    info.fixMode = toI(8);
    // 9 = Reserved1
    info.hdop = (float)toD(10);
    info.pdop = (float)toD(11);
    info.vdop = (float)toD(12);
    // 13 = Reserved2
    info.gpsSatsInView = toI(14);
    info.gnssSatsUsed = toI(15);
    info.glonassSatsInView = toI(16);
    // 17 = Reserved3
    info.cn0Max = toI(18);
    info.hpa = (float)toD(19);
    info.vpa = (float)toD(20);
    return true;
}

bool GNSSManager::getInfo(GnssInfo &info)
{
    std::string resp;
    if (!at.sendCommand("AT+CGNSINF", "+CGNSINF:", resp, 5000))
    {
        LOG_ERROR("GNSS", "AT+CGNSINF failed: %s", resp.c_str());
        return false;
    }
    if (!parseCgnsinf(resp, info))
    {
        LOG_ERROR("GNSS", "Failed to parse: %s", resp.c_str());
        return false;
    }
    if (info.fixed)
    {
        LOG_DEBUG("GNSS", "Fix: %.6f,%.6f alt=%.1f sats=%d",
                  info.latitude, info.longitude, info.altitude, info.gnssSatsUsed);
    }
    return true;
}

bool GNSSManager::waitForFix(GnssInfo &info, uint32_t timeoutMs, uint32_t intervalMs)
{
    if (powerState() != 1 && !powerOn())
        return false;

    LOG_INFO("GNSS", "Waiting for fix (timeout %lu ms)", (unsigned long)timeoutMs);
    uint32_t start = millis();
    while (millis() - start < timeoutMs)
    {
        if (getInfo(info) && info.fixed)
        {
            LOG_INFO("GNSS", "Fix OK: %.6f,%.6f (%d sats)", info.latitude, info.longitude, info.gnssSatsUsed);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(intervalMs));
    }
    LOG_WARN("GNSS", "No fix within timeout");
    return false;
}

bool GNSSManager::getLocation(double &latitude, double &longitude)
{
    GnssInfo info;
    if (!getInfo(info) || !info.fixed)
        return false;
    latitude = info.latitude;
    longitude = info.longitude;
    return true;
}
