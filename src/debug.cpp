#include "sprinky.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define PRINT_BUFFER_SIZE 256

// Create a circular buffer for debug output on web page
CircularBuffer<char, (bufferSize - 4)> circBuff;

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
  // Add formatted string to circular buffer
  for (size_t i = 0; i < bufferLen; i++) {
    if (!circBuff.push(buffer[i])) {
      // Remove old data until encountering '\n'
      while (!circBuff.isEmpty() && circBuff.first() != '\n') {
        circBuff.shift();
      }
      if (!circBuff.isEmpty()) {
        circBuff.shift(); // Remove the trailing '\n'
      }
    }
  }
}

void fetchDebugText(void) { // displays the recent operations/ debug info
  // read all of circular buffer into charBuff, it contains recent debug print
  // out
  int qty = circBuff.size();
  int i = 0;
  // unload ring buffer contents
  for (i = 0; i < qty; i++) {
    charBuf[i] = circBuff.shift();
    circBuff.push(charBuf[i]);
  }
  charBuf[i] = 0; // terminate string
}
