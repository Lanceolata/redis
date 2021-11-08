#ifndef CRC64_H
#define CRC64_H

/**
 * 循环冗余校验
 */

#include <stdint.h>

// 初始化
void crc64_init(void);
// 计算crc64 TODO
uint64_t crc64(uint64_t crc, const unsigned char *s, uint64_t l);

#ifdef REDIS_TEST
int crc64Test(int argc, char *argv[]);
#endif

#endif
