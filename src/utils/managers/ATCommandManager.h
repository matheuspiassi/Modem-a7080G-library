#pragma once

#include <string>
#include <functional>
#include <HardwareSerial.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Logging.h"

/**
 * Gerenciador de comandos AT para o SIM7080G.
 *
 * Todas as operações na UART do modem passam por aqui e são protegidas por mutex.
 * Linhas recebidas que não pertencem à resposta esperada (URCs como +SMSUB,
 * +SMSTATE, +APP PDP, *PSUTTZ...) são entregues ao handler registrado em
 * setUrcHandler(), em vez de serem descartadas.
 */
class ATCommandManager {
public:
    using UrcHandler = std::function<void(const std::string &line)>;

    /**
     * @param serial UART to use (e.g., Serial2)
     * @param defaultTimeout Timeout in ms for commands when not specified
     */
    ATCommandManager(HardwareSerial& serial, uint32_t defaultTimeout = 1000);

    /**
     * Must be called once to initialize the UART pins and baud rate
     */
    void begin(uint32_t baud, int rxPin = -1, int txPin = -1);

    /**
     * Send AT command and wait for a single-line response matching expectedPrefix
     * @param command        Null-terminated command string (without CRLF)
     * @param expectedPrefix Expected line prefix to match (e.g., "OK", "+CSQ"). Empty string means accept first line.
     * @param responseBuffer Buffer to receive C-string response (must be >=256 bytes)
     * @param timeout        Timeout in ms
     * @return true if prefix matched, false on error (ERROR / +CME ERROR) or timeout
     */
    bool sendCommand(const char* command,
                     const char* expectedPrefix,
                     char* responseBuffer,
                     uint32_t timeout);

    /**
     * Overload: C-string command, std::string response
     */
    bool sendCommand(const char* command,
                     const char* expectedPrefix,
                     std::string& response,
                     uint32_t timeout);

    /**
     * Overload: std::string command, std::string response
     */
    bool sendCommand(const std::string& command,
                     const char* expectedPrefix,
                     std::string& response,
                     uint32_t timeout);

    /**
     * Convenience: use defaultTimeout_
     */
    bool sendCommand(const char* command,
                     const char* expectedPrefix,
                     char* responseBuffer) {
        return sendCommand(command, expectedPrefix, responseBuffer, defaultTimeout_);
    }
    bool sendCommand(const char* command,
                     const char* expectedPrefix,
                     std::string& response) {
        return sendCommand(command, expectedPrefix, response, defaultTimeout_);
    }
    bool sendCommand(const std::string& command,
                     const char* expectedPrefix,
                     std::string& response) {
        return sendCommand(command.c_str(), expectedPrefix, response, defaultTimeout_);
    }

    /**
     * Envia um comando que abre um prompt de dados (ex.: AT+SMPUB -> ">",
     * AT+CFSWFILE -> "DOWNLOAD"), escreve exatamente `len` bytes e aguarda a
     * resposta final.
     * @param command        Comando AT (sem CRLF)
     * @param prompt         Início da linha de prompt esperada (">" ou "DOWNLOAD")
     * @param data           Dados a enviar após o prompt
     * @param len            Quantidade de bytes de `data`
     * @param expectedPrefix Resposta final esperada (normalmente "OK")
     * @param response       Recebe a linha final
     * @param timeout        Timeout total em ms
     */
    bool sendCommandWithData(const char* command,
                             const char* prompt,
                             const uint8_t* data,
                             size_t len,
                             const char* expectedPrefix,
                             std::string& response,
                             uint32_t timeout);

    bool sendCommandWithData(const std::string& command,
                             const char* prompt,
                             const std::string& data,
                             const char* expectedPrefix,
                             std::string& response,
                             uint32_t timeout) {
        return sendCommandWithData(command.c_str(), prompt,
                                   (const uint8_t*)data.data(), data.size(),
                                   expectedPrefix, response, timeout);
    }

    /**
     * Aguarda uma linha começando com expectedPrefix sem enviar comando
     * (útil para URCs como "+APP PDP: 0,ACTIVE").
     */
    bool waitForResponse(const char* expectedPrefix,
                         std::string& response,
                         uint32_t timeout);

    /**
     * Registra o handler que recebe as linhas não solicitadas (URCs).
     * O handler é chamado com o mutex da UART travado: ele NÃO pode enviar
     * comandos AT, apenas armazenar/sinalizar.
     */
    void setUrcHandler(UrcHandler handler) { urcHandler_ = handler; }

    /**
     * Lê o que estiver disponível na UART (sem bloquear) e entrega as linhas
     * completas ao handler de URC. Chame periodicamente (ex.: mqtt.loop()).
     */
    void poll();

    HardwareSerial& serial() { return serial_; }

private:
    static const size_t kMaxLineLength = 2048;

    HardwareSerial& serial_;
    SemaphoreHandle_t mutex_;
    uint32_t defaultTimeout_;
    UrcHandler urcHandler_;
    std::string rxLine_;   ///< Linha parcial em recepção

    bool lock(uint32_t timeout, const char* command);
    void unlock();
    void writeCommand(const char* command);

    /** Alimenta um caractere; retorna true quando uma linha (ou prompt) ficou completa. */
    bool feedChar(char c, std::string& line, const char* prompt);
    bool readLine(std::string& line, uint32_t timeout, const char* prompt = nullptr);

    /**
     * Internal: reads lines from UART until one starts with prefix
     */
    bool readResponse(std::string& out, const char* prefix, uint32_t timeout);

    /** Consome o "OK" final que segue uma linha de informação (+XXX: ...). */
    void drainFinalResult(uint32_t timeout);

    /** Descarta/encaminha o lixo pendente na UART antes de um novo comando. */
    void flushInput();

    void dispatchUrc(const std::string& line);
    static bool startsWith(const std::string& s, const char* prefix);
    static bool isErrorLine(const std::string& s);
};
