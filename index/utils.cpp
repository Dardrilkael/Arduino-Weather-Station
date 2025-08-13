#include "pch.h" 
#include <Arduino.h>
//"... -../"
void morseCode(int pin, const char *pattern, int unit) {
  for (int i = 0; pattern[i]; i++) {
    char c = pattern[i];

    int onTime = (c == '.') * unit + (c == '-') * 3 * unit;
    int offTime = 0;

    if (onTime > 0) {
      // Dot or dash: ON duration
      digitalWrite(pin, HIGH);
      delay(onTime);
      digitalWrite(pin, LOW);

      // Gap between symbols in same letter: 1 unit
      // But if next char is space or slash or end, skip this gap
      char next = pattern[i+1];
      if (next != '.' && next != '-' && next != 0) {
        offTime = 0;  // no symbol gap here, handled by letter/word gap below
      } else {
        offTime = unit; // gap between symbols
      }
    } else if (c == ' ') {
      // gap between letters: 3 units
      offTime = 3 * unit;
    } else if (c == '/') {
      // gap between words: 7 units
      offTime = 7 * unit;
    }

    if (offTime > 0) delay(offTime);
  }
}

