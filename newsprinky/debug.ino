#include <CircularBuffer.hpp> // https://github.com/rlogiacco/CircularBuffer

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// Create a circular buffer for debug output on web page
CircularBuffer < char, (bufferSize - 40) > buff;

// write string into circular buffer for later printout on browser
void webPrint(const char* format, ...)
{
  char buffer[256];
  va_list args;
  va_start (args, format);
  vsprintf (buffer, format, args);
  Serial.println(buffer);
  // put formated string into circular buffer
  for (int i = 0; i < strlen(buffer); i++)
  {
    buff.push(buffer[i]);
  }
  va_end (args);
}

void fetchDebugText() { // displays the recent operations/ debug info
  // read all of circular buffer into charBuff
  // circular buffer contains recent debug print out

  // trim old debug data
  while (buff.available() < 40) {
    buff.shift();
  }

  // line up to first html break (<br>)
  //while(buff[0] != '<') {
  //   buff.shift();
  //}

  int qty = buff.size();
  int i = 0;
  // unload ring buffer contents
  for (i = 0; i < qty; i++) {

    charBuf[i] = buff.shift();
    buff.push(charBuf[i]);
  }
  charBuf[i + 1] = 0; //terminate string
}
