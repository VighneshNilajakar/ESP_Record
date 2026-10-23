#pragma once
#include <stddef.h>
#include <stdint.h>

class Print {
public:
  virtual ~Print() {}
  // Mirrors the real Print: write(uint8_t) is pure, and the buffer overload is
  // virtual with a default that forwards one byte at a time. Keeping the stub
  // faithful matters, because a class that forgets write(uint8_t) compiles
  // against a looser stub and then fails only on the real target.
  virtual size_t write(uint8_t value) = 0;
  virtual size_t write(const uint8_t *buffer, size_t size) {
    size_t written = 0;
    while (written < size) written += write(buffer[written]);
    return written;
  }
};
