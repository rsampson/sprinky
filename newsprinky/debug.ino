

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define BUFFER_SIZE 256

// Create a circular buffer for debug output on web page
CircularBuffer < char, (bufferSize - 4) > circBuff;

// write string into circular buffer for later printout on browser
void webPrint(const char* format, ...)
{
  char buffer[BUFFER_SIZE];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, BUFFER_SIZE, format, args); 
  va_end(args);
  
  if(len < 0){
    return; // Error during formatting
  }
   Serial.print(buffer); 
  // put formated string into circular buffer
  for (int i = 0; i < strlen(buffer); i++) 
  {
    if(!circBuff.push(buffer[i])) // if data was overwritten
    {
      // dump old data up untill first end of line
      while(circBuff.first() != '\n') {
        circBuff.shift();
      }
      circBuff.shift(); // get rid of \n
    }
  }
}

//void webPrint(const char* format, ...)
//{
//  char buffer[256];
//  va_list args;
//  va_start (args, format);
//  vsprintf (buffer, format, args);
//  Serial.print(buffer);
//  // put formated string into circular buffer
//  for (int i = 0; i < strlen(buffer); i++) 
//  {
//    if(!circBuff.push(buffer[i])) // if data was overwritten
//    {
//      // dump old data up untill first end of line
//      while(circBuff.first() != '\n') {
//        circBuff.shift();
//      }
//      circBuff.shift(); // get rid of \n
//    }
//  }
//  va_end (args);
//}


void fetchDebugText() { // displays the recent operations/ debug info
  // read all of circular buffer into charBuff, it contains recent debug print out
  int qty = circBuff.size();
  int i = 0;
  // unload ring buffer contents
  for (i = 0; i < qty; i++) {
    charBuf[i] = circBuff.shift();
    circBuff.push(charBuf[i]);
  }
  charBuf[i+1] = 0; //terminate string
}
