#include "ohos_ime.h"

#include <stdio.h>

static int freerdp_ohos_ime_is_high_surrogate(uint16_t code)
{
	return code >= 0xD800U && code <= 0xDBFFU;
}

static int freerdp_ohos_ime_is_low_surrogate(uint16_t code)
{
	return code >= 0xDC00U && code <= 0xDFFFU;
}

int freerdp_ohos_ime_build_committed_text_packets(const uint16_t* text, size_t length,
                                                  FREERDP_OHOS_IME_PACKET* packets,
                                                  size_t capacity, size_t* count,
                                                  size_t* skipped)
{
	size_t output = 0;
	size_t skippedUnits = 0;

	if (count)
		*count = 0;
	if (skipped)
		*skipped = 0;

	if (!text || !packets || !count)
		return 0;

	for (size_t index = 0; index < length; ++index)
	{
		const uint16_t code = text[index];

		if (code == 0)
			continue;

		if (freerdp_ohos_ime_is_high_surrogate(code))
		{
			if ((index + 1) < length && freerdp_ohos_ime_is_low_surrogate(text[index + 1]))
				++index;
			++skippedUnits;
			continue;
		}

		if (freerdp_ohos_ime_is_low_surrogate(code))
		{
			++skippedUnits;
			continue;
		}

		if ((output + 2U) > capacity)
			return 0;

		packets[output].codeUnit = code;
		packets[output].down = 1;
		++output;
		packets[output].codeUnit = code;
		packets[output].down = 0;
		++output;
	}

	*count = output;
	if (skipped)
		*skipped = skippedUnits;
	return 1;
}

int freerdp_ohos_ime_format_committed_text_result(size_t textUnits, size_t packetCount,
                                                  size_t skipped, char* buffer, size_t size)
{
	int written = 0;

	if (!buffer || size == 0)
		return 0;

	written = snprintf(buffer, size,
	                   "OHOS IME committed text: units=%zu unicodePackets=%zu skippedSurrogates=%zu",
	                   textUnits, packetCount, skipped);
	if (written < 0)
	{
		buffer[0] = '\0';
		return 0;
	}
	if ((size_t)written >= size)
		buffer[size - 1] = '\0';
	return 1;
}
