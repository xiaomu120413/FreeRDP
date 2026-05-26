/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Print Virtual Channel - HarmonyOS native print driver
 */

#include "printer_ohos.h"

#include <inttypes.h>

#include <winpr/assert.h>

const char* printer_ohos_error_name(Print_ErrorCode code)
{
	switch (code)
	{
		case PRINT_ERROR_NONE:
			return "PRINT_ERROR_NONE";
		case PRINT_ERROR_NO_PERMISSION:
			return "PRINT_ERROR_NO_PERMISSION";
		case PRINT_ERROR_INVALID_PARAMETER:
			return "PRINT_ERROR_INVALID_PARAMETER";
		case PRINT_ERROR_GENERIC_FAILURE:
			return "PRINT_ERROR_GENERIC_FAILURE";
		case PRINT_ERROR_RPC_FAILURE:
			return "PRINT_ERROR_RPC_FAILURE";
		case PRINT_ERROR_SERVER_FAILURE:
			return "PRINT_ERROR_SERVER_FAILURE";
		case PRINT_ERROR_INVALID_EXTENSION:
			return "PRINT_ERROR_INVALID_EXTENSION";
		case PRINT_ERROR_INVALID_PRINTER:
			return "PRINT_ERROR_INVALID_PRINTER";
		case PRINT_ERROR_INVALID_PRINT_JOB:
			return "PRINT_ERROR_INVALID_PRINT_JOB";
		case PRINT_ERROR_FILE_IO:
			return "PRINT_ERROR_FILE_IO";
		case PRINT_ERROR_UNKNOWN:
			return "PRINT_ERROR_UNKNOWN";
		default:
			return "PRINT_ERROR_UNRECOGNIZED";
	}
}

BOOL printer_ohos_ensure_init(rdpOhosPrinterDriver* driver)
{
	WINPR_ASSERT(driver);

	if (driver->initialized)
		return TRUE;

	const Print_ErrorCode rc = OH_Print_Init();
	if (rc != PRINT_ERROR_NONE)
	{
		WLog_WARN(PRINTER_OHOS_TAG, "OH_Print_Init failed: %s [%" PRIu32 "]",
		          printer_ohos_error_name(rc), (UINT32)rc);
		return FALSE;
	}

	driver->initialized = TRUE;
	return TRUE;
}

const char* printer_ohos_default_driver_name(void)
{
	return "MS Publisher Imagesetter";
}

const char* printer_ohos_virtual_printer_name(void)
{
	return OHOS_VIRTUAL_PRINTER_NAME;
}

rdpOhosPrinter* printer_ohos_cast_printer(rdpPrinter* printer)
{
	return (rdpOhosPrinter*)printer;
}
