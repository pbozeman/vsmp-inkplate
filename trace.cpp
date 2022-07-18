#include <stdarg.h>
#include <stdio.h>

#include "Inkplate.h"

void trace(const char* fname, int line, const char* func, char* fmt, ...) {
  char buf[1024];
  int len;

  va_list args;
  va_start(args, fmt);

  len = snprintf(buf, sizeof(buf), "%s:%d:%s() ", fname, line, func);
  if (len > 0) {
    len = vsnprintf(buf + len, sizeof(buf) - len, fmt, args);
  }

  if (len > 0) {
    Serial.println(buf);
  }
}
