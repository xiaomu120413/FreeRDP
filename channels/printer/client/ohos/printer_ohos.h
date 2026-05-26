/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Print Virtual Channel - HarmonyOS native print driver
 */

#ifndef FREERDP_CHANNELS_PRINTER_CLIENT_OHOS_PRINTER_OHOS_H
#define FREERDP_CHANNELS_PRINTER_CLIENT_OHOS_PRINTER_OHOS_H

#include <stddef.h>

#include <BasicServicesKit/ohprint.h>

#include <freerdp/channels/log.h>
#include <freerdp/client/printer.h>

#define PRINTER_OHOS_TAG CHANNELS_TAG("printer.client.ohos")
#define OHOS_VIRTUAL_PRINTER_NAME "HarmonyOS Printer"

typedef struct
{
	rdpPrinterDriver driver;

	size_t id_sequence;
	size_t references;
	BOOL initialized;
} rdpOhosPrinterDriver;

typedef struct
{
	rdpPrintJob printjob;

	int fd;
	char* path;
	BOOL failed;
} rdpOhosPrintJob;

typedef struct
{
	rdpPrinter printer;

	char* printer_id;
	rdpOhosPrintJob* printjob;
} rdpOhosPrinter;

const char* printer_ohos_error_name(Print_ErrorCode code);
BOOL printer_ohos_ensure_init(rdpOhosPrinterDriver* driver);
const char* printer_ohos_default_driver_name(void);
const char* printer_ohos_virtual_printer_name(void);
rdpOhosPrinter* printer_ohos_cast_printer(rdpPrinter* printer);

rdpPrintJob* printer_ohos_create_printjob(rdpPrinter* printer, UINT32 id);
rdpPrintJob* printer_ohos_find_printjob(rdpPrinter* printer, UINT32 id);

#endif /* FREERDP_CHANNELS_PRINTER_CLIENT_OHOS_PRINTER_OHOS_H */
