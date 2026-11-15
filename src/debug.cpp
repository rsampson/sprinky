#include "sprinky.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define PRINT_BUFFER_SIZE 256

// Recent debug output for the Status page: one NUL-terminated string, oldest
// line first. The status handler sends it in place, with no second copy. The
// last byte is never written, so a reader always finds a terminator.
char logBuf[bufferSize];
static size_t logLen = 0;

// On ESP32 the web handlers (AsyncTCP task) and loop() both log, so the buffer
// update needs a lock. On ESP8266 handlers never pre-empt loop().
#if defined(ESP32)
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;
#define LOG_LOCK() portENTER_CRITICAL(&logMux)
#define LOG_UNLOCK() portEXIT_CRITICAL(&logMux)
#else
#define LOG_LOCK()
#define LOG_UNLOCK()
#endif

void webPrint(const char *format, ...) {
  char buffer[PRINT_BUFFER_SIZE];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, PRINT_BUFFER_SIZE, format, args);
  va_end(args);

  if (len < 0) {
    Serial.println("Error: Formatting failed");
    return;
  }
  if (len >= PRINT_BUFFER_SIZE) {
    Serial.println("Warning: Output truncated");
  }

  size_t bufferLen = strlen(buffer);

  Serial.print(buffer); // Direct debugging output

  // Make room by dropping whole lines from the front.
  const size_t cap = bufferSize - 1;
  LOG_LOCK();
  while (logLen + bufferLen > cap) {
    char *nl = (char *)memchr(logBuf, '\n', logLen);
    size_t drop = nl ? (size_t)(nl - logBuf) + 1 : logLen;
    memmove(logBuf, logBuf + drop, logLen - drop);
    logLen -= drop;
    logBuf[logLen] = 0;
  }
  memcpy(logBuf + logLen, buffer, bufferLen);
  logLen += bufferLen;
  logBuf[logLen] = 0;
  LOG_UNLOCK();
}
