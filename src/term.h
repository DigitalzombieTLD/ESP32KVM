#pragma once
#include <Arduino.h>
#include <stdarg.h>

// Minimal terminal abstraction used by the shell (implemented over an SSH channel).
class Term {
 public:
  virtual ~Term() {}
  virtual int rawWrite(const char *data, size_t len) = 0;
  // >0 bytes read, 0 timeout, <0 connection closed/error
  virtual int rawRead(char *buf, size_t len, int timeoutMs) = 0;

  void print(const char *s);  // '\n' -> "\r\n"
  void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  // Reads a line with simple editing and history. Returns false if the connection closed
  // or the user pressed Ctrl-D on an empty line (eof=true).
  bool readLine(String &out, const char *prompt, bool echo, bool useHistory, bool &eof);

 private:
  static const int HIST = 8;
  String hist_[HIST];
  int histCount_ = 0;
  unsigned long lastActivity_ = 0;
};
