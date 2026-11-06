#include "term.h"

static const unsigned long IDLE_TIMEOUT_MS = 15UL * 60UL * 1000UL;

void Term::print(const char *s) {
  const char *start = s;
  for (const char *p = s;; p++) {
    if (*p == '\n' || *p == '\0') {
      if (p > start) rawWrite(start, p - start);
      if (*p == '\0') break;
      rawWrite("\r\n", 2);
      start = p + 1;
    }
  }
}

void Term::printf(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  print(buf);
}

bool Term::readLine(String &out, const char *prompt, bool echo, bool useHistory, bool &eof) {
  eof = false;
  out = "";
  print(prompt);
  int histPos = -1;
  int esc = 0;  // 0 none, 1 got ESC, 2 in CSI sequence
  String csi;
  lastActivity_ = millis();

  auto redraw = [&](const String &s) {
    rawWrite("\r\x1b[K", 4);
    print(prompt);
    if (echo && s.length()) rawWrite(s.c_str(), s.length());
  };

  while (true) {
    char c;
    int n = rawRead(&c, 1, 1000);
    if (n < 0) return false;
    if (n == 0) {
      if (millis() - lastActivity_ > IDLE_TIMEOUT_MS) {
        print("\nIdle timeout, closing session.\n");
        return false;
      }
      continue;
    }
    lastActivity_ = millis();

    if (esc == 1) {
      esc = (c == '[' || c == 'O') ? 2 : 0;
      csi = "";
      continue;
    }
    if (esc == 2) {
      if (c >= 0x40 && c <= 0x7e) {  // final byte
        esc = 0;
        if (useHistory && csi.length() == 0 && (c == 'A' || c == 'B')) {
          if (c == 'A' && histPos + 1 < histCount_) histPos++;
          else if (c == 'B' && histPos >= 0) histPos--;
          out = histPos >= 0 ? hist_[histPos] : String("");
          redraw(out);
        }
      } else {
        csi += c;
      }
      continue;
    }

    switch (c) {
      case 0x1b: esc = 1; break;
      case '\r':
      case '\n':
        print("\n");
        if (useHistory && out.length() && !(histCount_ && hist_[0] == out)) {
          for (int i = HIST - 1; i > 0; i--) hist_[i] = hist_[i - 1];
          hist_[0] = out;
          if (histCount_ < HIST) histCount_++;
        }
        return true;
      case 0x7f:
      case 0x08:
        if (out.length()) {
          out.remove(out.length() - 1);
          if (echo) rawWrite("\b \b", 3);
        }
        break;
      case 0x03:  // Ctrl-C: discard line
        out = "";
        print("^C\n");
        print(prompt);
        break;
      case 0x15:  // Ctrl-U
        out = "";
        redraw(out);
        break;
      case 0x04:  // Ctrl-D
        if (out.length() == 0) { eof = true; print("\n"); return false; }
        break;
      default:
        if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f && out.length() < 400) {
          out += c;
          if (echo) rawWrite(&c, 1);
        }
    }
  }
}
