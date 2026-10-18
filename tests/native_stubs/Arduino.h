#pragma once
#include <stddef.h>
#include <stdint.h>

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(const uint8_t *buffer, size_t size) = 0;
};
