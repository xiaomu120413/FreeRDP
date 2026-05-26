/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Print Virtual Channel - HarmonyOS native print driver entry
 */

#include "printer_ohos.h"

#include <stdlib.h>
#include <string.h>

#include <winpr/assert.h>
#include <winpr/crt.h>
#include <winpr/error.h>

static rdpOhosPrinterDriver* uniq_ohos_driver = nullptr;

static void printer_ohos_free_printer(rdpPrinter* printer)
{
	rdpOhosPrinter* ohos_printer = printer_ohos_cast_printer(printer);
	WINPR_ASSERT(ohos_printer);

	if (ohos_printer->printjob)
	{
		WINPR_ASSERT(ohos_printer->printjob->printjob.Close);
		ohos_printer->printjob->printjob.Close(&ohos_printer->printjob->printjob);
	}

	if (printer->backend)
	{
		WINPR_ASSERT(printer->backend->ReleaseRef);
		printer->backend->ReleaseRef(printer->backend);
	}

	free(ohos_printer->printer_id);
	free(printer->name);
	free(printer->driver);
	free(printer);
}

static void printer_ohos_add_ref_printer(rdpPrinter* printer)
{
	if (printer)
		printer->references++;
}

static void printer_ohos_release_ref_printer(rdpPrinter* printer)
{
	if (!printer)
		return;
	if (printer->references <= 1)
		printer_ohos_free_printer(printer);
	else
		printer->references--;
}

static rdpPrinter* printer_ohos_new_printer(rdpOhosPrinterDriver* ohos_driver,
                                            const char* name, const char* printer_id,
                                            const char* driverName, BOOL is_default)
{
	rdpOhosPrinter* ohos_printer = (rdpOhosPrinter*)calloc(1, sizeof(rdpOhosPrinter));
	if (!ohos_printer)
		return nullptr;

	ohos_printer->printer.backend = &ohos_driver->driver;
	ohos_printer->printer.id = ohos_driver->id_sequence++;
	ohos_printer->printer.name = _strdup((name && name[0] != '\0')
	                                         ? name
	                                         : ((printer_id && printer_id[0] != '\0')
	                                                ? printer_id
	                                                : printer_ohos_virtual_printer_name()));
	ohos_printer->printer.driver =
	    _strdup((driverName && driverName[0] != '\0') ? driverName
	                                                  : printer_ohos_default_driver_name());
	ohos_printer->printer.is_default = is_default;
	ohos_printer->printer.CreatePrintJob = printer_ohos_create_printjob;
	ohos_printer->printer.FindPrintJob = printer_ohos_find_printjob;
	ohos_printer->printer.AddRef = printer_ohos_add_ref_printer;
	ohos_printer->printer.ReleaseRef = printer_ohos_release_ref_printer;
	if (printer_id && printer_id[0] != '\0')
		ohos_printer->printer_id = _strdup(printer_id);

	if (!ohos_printer->printer.name || !ohos_printer->printer.driver ||
	    ((printer_id && printer_id[0] != '\0') && !ohos_printer->printer_id))
		goto fail;

	WINPR_ASSERT(ohos_printer->printer.AddRef);
	ohos_printer->printer.AddRef(&ohos_printer->printer);

	WINPR_ASSERT(ohos_printer->printer.backend->AddRef);
	ohos_printer->printer.backend->AddRef(ohos_printer->printer.backend);
	return &ohos_printer->printer;

fail:
	free(ohos_printer->printer_id);
	free(ohos_printer->printer.name);
	free(ohos_printer->printer.driver);
	free(ohos_printer);
	return nullptr;
}

static void printer_ohos_release_enum_printers(rdpPrinter** printers)
{
	rdpPrinter** current = printers;
	while (current && *current)
	{
		if ((*current)->ReleaseRef)
			(*current)->ReleaseRef(*current);
		current++;
	}
	free(printers);
}

static rdpPrinter** printer_ohos_enum_printers(rdpPrinterDriver* driver)
{
	rdpOhosPrinterDriver* ohos_driver = (rdpOhosPrinterDriver*)driver;
	WINPR_ASSERT(ohos_driver);

	rdpPrinter** printers = (rdpPrinter**)calloc(2, sizeof(rdpPrinter*));
	if (!printers)
		return nullptr;

	printers[0] = printer_ohos_new_printer(ohos_driver, printer_ohos_virtual_printer_name(),
	                                       nullptr, nullptr, TRUE);
	if (!printers[0])
	{
		free(printers);
		return nullptr;
	}
	return printers;
}

static rdpPrinter* printer_ohos_get_printer(rdpPrinterDriver* driver, const char* name,
                                            const char* driverName, BOOL isDefault)
{
	rdpOhosPrinterDriver* ohos_driver = (rdpOhosPrinterDriver*)driver;
	WINPR_ASSERT(ohos_driver);

	if (!name || name[0] == '\0')
		return nullptr;

	const char* requested_id =
	    (_stricmp(name, printer_ohos_virtual_printer_name()) == 0) ? nullptr : name;
	return printer_ohos_new_printer(ohos_driver, name, requested_id, driverName, isDefault);
}

static void printer_ohos_add_ref_driver(rdpPrinterDriver* driver)
{
	rdpOhosPrinterDriver* ohos_driver = (rdpOhosPrinterDriver*)driver;
	if (ohos_driver)
		ohos_driver->references++;
}

static void printer_ohos_release_ref_driver(rdpPrinterDriver* driver)
{
	rdpOhosPrinterDriver* ohos_driver = (rdpOhosPrinterDriver*)driver;
	WINPR_ASSERT(ohos_driver);

	if (ohos_driver->references <= 1)
	{
		if (ohos_driver->initialized)
			(void)OH_Print_Release();
		if (uniq_ohos_driver == ohos_driver)
			uniq_ohos_driver = nullptr;
		free(ohos_driver);
	}
	else
		ohos_driver->references--;
}

FREERDP_ENTRY_POINT(UINT VCAPITYPE ohos_freerdp_printer_client_subsystem_entry(void* arg))
{
	rdpPrinterDriver** ppPrinter = (rdpPrinterDriver**)arg;
	if (!ppPrinter)
		return ERROR_INVALID_PARAMETER;

	if (!uniq_ohos_driver)
	{
		uniq_ohos_driver = (rdpOhosPrinterDriver*)calloc(1, sizeof(rdpOhosPrinterDriver));
		if (!uniq_ohos_driver)
			return ERROR_OUTOFMEMORY;

		uniq_ohos_driver->driver.EnumPrinters = printer_ohos_enum_printers;
		uniq_ohos_driver->driver.ReleaseEnumPrinters = printer_ohos_release_enum_printers;
		uniq_ohos_driver->driver.GetPrinter = printer_ohos_get_printer;
		uniq_ohos_driver->driver.AddRef = printer_ohos_add_ref_driver;
		uniq_ohos_driver->driver.ReleaseRef = printer_ohos_release_ref_driver;
		uniq_ohos_driver->id_sequence = 1;
	}

	WINPR_ASSERT(uniq_ohos_driver->driver.AddRef);
	uniq_ohos_driver->driver.AddRef(&uniq_ohos_driver->driver);

	*ppPrinter = &uniq_ohos_driver->driver;
	return CHANNEL_RC_OK;
}
