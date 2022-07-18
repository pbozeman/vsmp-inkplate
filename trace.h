#ifndef TRACE_H
#define TRACE_H

#define __FILENAME__ \
  (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define TRACE(args...)                             \
  do {                                             \
    trace(__FILENAME__, __LINE__, __func__, args); \
  } while (0)

void trace(const char* fname, int line, const char* func, char* fmt, ...);

#endif
