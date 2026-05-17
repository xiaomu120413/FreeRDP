#ifndef FREERDP_CLIENT_OHOS_IME_H
#define FREERDP_CLIENT_OHOS_IME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
	uint32_t codeUnit;
	int down;
} FREERDP_OHOS_IME_PACKET;

int freerdp_ohos_ime_build_committed_text_packets(const uint16_t* text, size_t length,
                                                  FREERDP_OHOS_IME_PACKET* packets,
                                                  size_t capacity, size_t* count,
                                                  size_t* skipped);
int freerdp_ohos_ime_format_committed_text_result(size_t textUnits, size_t packetCount,
                                                  size_t skipped, char* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_IME_H */
