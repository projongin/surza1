#pragma once

#include <stdint.h>
#include <stdbool.h>

uint32_t crc32(const char* buf, unsigned len);

bool crc32_check(const char* buf, unsigned len, uint32_t crc);




