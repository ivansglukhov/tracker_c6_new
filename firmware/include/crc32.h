#pragma once

#include <Arduino.h>

uint32_t trackerCrc32(const void *data, size_t size, uint32_t seed = 0xFFFFFFFFU);

