#ifndef __CRC_H__
#define __CRC_H__

#include <stdint.h>

uint16_t crc16_ccitt(const uint8_t* data, uint8_t len);
#endif // __CRC_H__
