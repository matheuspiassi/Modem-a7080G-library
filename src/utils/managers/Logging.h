#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>

// Níveis de log
enum LogLevel {
    LOG_LEVEL_SIMPLE = 0,  // Apenas mensagens essenciais (INFO, WARN, ERROR)
    LOG_LEVEL_DEBUG = 1    // Todas as mensagens incluindo comandos AT (DEBUG)
};

// Variável global para controlar o nível de log
extern LogLevel g_logLevel;

// buffer único para formatação
static char _log_buffer[128];

// Macro genérica
#define LOG(level, tag, fmt, ...)                   \
  do {                                              \
    snprintf(_log_buffer, sizeof(_log_buffer), fmt, ##__VA_ARGS__); \
    Serial.print("["); Serial.print(tag); Serial.print("] ");        \
    Serial.print(level); Serial.print(": ");                        \
    Serial.println(_log_buffer);                                    \
  } while (0)

// Macros de níveis
#define LOG_INFO(tag, fmt, ...)  LOG("I", tag, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  LOG("W", tag, fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) LOG("E", tag, fmt, ##__VA_ARGS__)

// LOG_DEBUG só imprime se estiver em modo DEBUG
#define LOG_DEBUG(tag, fmt, ...) \
  do { \
    if (g_logLevel >= LOG_LEVEL_DEBUG) { \
      LOG("D", tag, fmt, ##__VA_ARGS__); \
    } \
  } while (0)

#endif // LOGGING_H
