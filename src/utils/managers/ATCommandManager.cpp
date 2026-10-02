#include "ATCommandManager.h"
#include <cstring>

ATCommandManager::ATCommandManager(HardwareSerial &serial, uint32_t defaultTimeout)
    : serial_(serial), defaultTimeout_(defaultTimeout)
{
    mutex_ = xSemaphoreCreateMutex();
}

void ATCommandManager::begin(uint32_t baud, int rxPin, int txPin)
{
    if (rxPin >= 0 && txPin >= 0)
    {
        serial_.begin(baud, SERIAL_8N1, rxPin, txPin);
    }
    else
    {
        serial_.begin(baud);
    }
    LOG_INFO("AT", "UART initialized at %lu bps", (unsigned long)baud);
}

// ----------------- Helpers -----------------

bool ATCommandManager::startsWith(const std::string &s, const char *prefix)
{
    return prefix && strncmp(s.c_str(), prefix, strlen(prefix)) == 0;
}

bool ATCommandManager::isErrorLine(const std::string &s)
{
    return startsWith(s, "ERROR") ||
           startsWith(s, "+CME ERROR") ||
           startsWith(s, "+CMS ERROR");
}

bool ATCommandManager::lock(uint32_t timeout, const char *command)
{
    if (!mutex_)
        return false;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(timeout)) != pdTRUE)
    {
        LOG_ERROR("AT", "Mutex timeout for '%s'", command ? command : "");
        return false;
    }
    return true;
}

void ATCommandManager::unlock()
{
    xSemaphoreGive(mutex_);
}

void ATCommandManager::writeCommand(const char *command)
{
    serial_.print(command);
    serial_.print("\r\n");
    LOG_DEBUG("AT", "TX -> %s", command);
}

void ATCommandManager::dispatchUrc(const std::string &line)
{
    if (line.empty())
        return;
    LOG_DEBUG("AT", "URC <- %s", line.c_str());
    if (urcHandler_)
        urcHandler_(line);
}

bool ATCommandManager::feedChar(char c, std::string &line, const char *prompt)
{
    if (c == '\n')
    {
        if (!rxLine_.empty() && rxLine_.back() == '\r')
            rxLine_.pop_back();
        if (rxLine_.empty())
            return false;
        line.swap(rxLine_);
        rxLine_.clear();
        return true;
    }

    if (rxLine_.size() < kMaxLineLength)
        rxLine_.push_back(c);

    // Prompts (">" / "DOWNLOAD") podem chegar sem CRLF final.
    // Só vale no início da linha para não confundir com '>' dentro de um payload.
    if (prompt && prompt[0] && startsWith(rxLine_, prompt))
    {
        line.swap(rxLine_);
        rxLine_.clear();
        return true;
    }
    return false;
}

bool ATCommandManager::readLine(std::string &line, uint32_t timeout, const char *prompt)
{
    uint32_t start = millis();
    while (millis() - start < timeout)
    {
        if (serial_.available())
        {
            if (feedChar((char)serial_.read(), line, prompt))
                return true;
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    return false;
}

void ATCommandManager::flushInput()
{
    std::string line;
    uint32_t start = millis();
    // Lê tudo que já chegou; se houver uma linha pela metade, espera até 50 ms para completá-la
    while (serial_.available() || (!rxLine_.empty() && millis() - start < 50))
    {
        if (serial_.available())
        {
            if (feedChar((char)serial_.read(), line, nullptr))
                dispatchUrc(line);
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    rxLine_.clear();
}

void ATCommandManager::drainFinalResult(uint32_t timeout)
{
    std::string line;
    uint32_t start = millis();
    while (millis() - start < timeout)
    {
        if (!readLine(line, timeout - (millis() - start)))
            return;
        LOG_DEBUG("AT", "RX <- %s", line.c_str());
        if (line == "OK" || isErrorLine(line))
            return;
        dispatchUrc(line);
    }
}

bool ATCommandManager::readResponse(std::string &out, const char *prefix, uint32_t timeout)
{
    const bool anyLine = (prefix == nullptr || prefix[0] == '\0');
    uint32_t start = millis();
    std::string line;

    while (true)
    {
        uint32_t elapsed = millis() - start;
        if (elapsed >= timeout || !readLine(line, timeout - elapsed))
            break;

        LOG_DEBUG("AT", "RX <- %s", line.c_str());

        // A resposta esperada tem prioridade (ex.: "+CPIN:" também aparece como URC de boot)
        if (!anyLine && startsWith(line, prefix))
        {
            out = line;
            // Linha de informação: consome o "OK" que vem em seguida
            if (strcmp(prefix, "OK") != 0)
                drainFinalResult(300);
            return true;
        }

        if (isErrorLine(line))
        {
            out = line;
            LOG_WARN("AT", "Error response: %s", line.c_str());
            return false;
        }

        if (anyLine)
        {
            out = line;
            return true;
        }

        // Linha que não é a resposta esperada: eco, OK intermediário ou URC
        dispatchUrc(line);
    }

    out.clear();
    LOG_WARN("AT", "Response timeout (%s)", anyLine ? "any" : prefix);
    return false;
}

// ----------------- API pública -----------------

bool ATCommandManager::sendCommand(const char *command,
                                   const char *expectedPrefix,
                                   std::string &response,
                                   uint32_t timeout)
{
    if (timeout == 0)
        timeout = defaultTimeout_;
    if (!lock(timeout, command))
    {
        response.clear();
        return false;
    }

    flushInput();
    writeCommand(command);
    bool ok = readResponse(response, expectedPrefix, timeout);

    unlock();
    return ok;
}

bool ATCommandManager::sendCommand(const char *command,
                                   const char *expectedPrefix,
                                   char *responseBuffer,
                                   uint32_t timeout)
{
    std::string resp;
    bool ok = sendCommand(command, expectedPrefix, resp, timeout);
    strncpy(responseBuffer, resp.c_str(), 255);
    responseBuffer[255] = '\0';
    return ok;
}

bool ATCommandManager::sendCommand(const std::string &command,
                                   const char *expectedPrefix,
                                   std::string &response,
                                   uint32_t timeout)
{
    return sendCommand(command.c_str(), expectedPrefix, response, timeout);
}

bool ATCommandManager::sendCommandWithData(const char *command,
                                           const char *prompt,
                                           const uint8_t *data,
                                           size_t len,
                                           const char *expectedPrefix,
                                           std::string &response,
                                           uint32_t timeout)
{
    if (timeout == 0)
        timeout = defaultTimeout_;
    if (!lock(timeout, command))
    {
        response.clear();
        return false;
    }

    flushInput();
    writeCommand(command);

    // 1) Espera o prompt
    uint32_t start = millis();
    bool gotPrompt = false;
    std::string line;
    while (millis() - start < timeout)
    {
        if (!readLine(line, timeout - (millis() - start), prompt))
            break;
        LOG_DEBUG("AT", "RX <- %s", line.c_str());
        if (startsWith(line, prompt))
        {
            gotPrompt = true;
            break;
        }
        if (isErrorLine(line))
        {
            response = line;
            LOG_WARN("AT", "Error waiting prompt: %s", line.c_str());
            unlock();
            return false;
        }
        dispatchUrc(line);
    }

    if (!gotPrompt)
    {
        response.clear();
        LOG_WARN("AT", "Prompt '%s' timeout for '%s'", prompt, command);
        unlock();
        return false;
    }

    // 2) Envia exatamente len bytes (o modem sai do modo de edição ao completar o tamanho)
    serial_.write(data, len);
    LOG_DEBUG("AT", "TX -> <%u bytes>", (unsigned)len);

    // 3) Espera a resposta final
    uint32_t elapsed = millis() - start;
    uint32_t remaining = (elapsed < timeout) ? (timeout - elapsed) : 1000;
    bool ok = readResponse(response, expectedPrefix, remaining);

    unlock();
    return ok;
}

bool ATCommandManager::waitForResponse(const char *expectedPrefix,
                                       std::string &response,
                                       uint32_t timeout)
{
    if (!lock(timeout, expectedPrefix))
    {
        response.clear();
        return false;
    }
    bool ok = readResponse(response, expectedPrefix, timeout);
    unlock();
    return ok;
}

void ATCommandManager::poll()
{
    if (!mutex_ || xSemaphoreTake(mutex_, 0) != pdTRUE)
        return; // outra tarefa está usando a UART; as linhas serão entregues por ela

    std::string line;
    while (serial_.available())
    {
        if (feedChar((char)serial_.read(), line, nullptr))
            dispatchUrc(line);
    }
    unlock();
}
