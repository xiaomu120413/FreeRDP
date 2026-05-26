/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Print Virtual Channel - HarmonyOS native print job handling
 */

#include "printer_ohos.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <freerdp/utils/helpers.h>
#include <winpr/assert.h>
#include <winpr/crt.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

static void printer_ohos_close_fd(rdpOhosPrintJob* printjob)
{
	if (!printjob || printjob->fd < 0)
		return;
	(void)close(printjob->fd);
	printjob->fd = -1;
}

static const char* printer_ohos_temp_dir(void)
{
	const char* tmp = getenv("TMPDIR");
	if (tmp && tmp[0] != '\0')
		return tmp;

	const char* home = getenv("HOME");
	if (home && home[0] != '\0')
		return home;

	return ".";
}

static char* printer_ohos_printjob_path(UINT32 id, size_t attempt)
{
	char* path = nullptr;
	size_t length = 0;
	const int rc =
	    winpr_asprintf(&path, &length, "%s/freerdp-print-%ld-%" PRIu32 "-%zu.prn",
	                   printer_ohos_temp_dir(), (long)getpid(), id, attempt);
	if (rc <= 0)
	{
		free(path);
		return nullptr;
	}
	return path;
}

static char* printer_ohos_printjob_name(UINT32 id)
{
	struct tm time_result = WINPR_C_ARRAY_INIT;
	const time_t now = time(nullptr);
	const struct tm* local = localtime_r(&now, &time_result);

	char* name = nullptr;
	size_t length = 0;
	if (!local)
	{
		const int rc =
		    winpr_asprintf(&name, &length, "%s Print - Job %" PRIu32,
		                   freerdp_getApplicationDetailsString(), id);
		if (rc <= 0)
		{
			free(name);
			return nullptr;
		}
		return name;
	}

	const int rc = winpr_asprintf(&name, &length,
	                              "%s Print %04d-%02d-%02d %02d-%02d-%02d - Job %" PRIu32,
	                              freerdp_getApplicationDetailsString(), local->tm_year + 1900,
	                              local->tm_mon + 1, local->tm_mday, local->tm_hour,
	                              local->tm_min, local->tm_sec, id);
	if (rc <= 0)
	{
		free(name);
		return nullptr;
	}
	return name;
}

static BOOL printer_ohos_bytes_look_like_text(const BYTE* data, size_t size)
{
	if (!data || size == 0)
		return FALSE;

	for (size_t x = 0; x < size; x++)
	{
		const BYTE c = data[x];
		if (c == '\t' || c == '\n' || c == '\r')
			continue;
		if (c >= 0x20 && c <= 0x7e)
			continue;
		return FALSE;
	}
	return TRUE;
}

static Print_DocumentFormat printer_ohos_detect_document_format(const char* path)
{
	BYTE header[512] = WINPR_C_ARRAY_INIT;
	const int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return DOCUMENT_FORMAT_AUTO;

	ssize_t rc = 0;
	do
	{
		rc = read(fd, header, sizeof(header));
	} while (rc < 0 && errno == EINTR);
	(void)close(fd);

	if (rc <= 0)
		return DOCUMENT_FORMAT_AUTO;

	const size_t size = (size_t)rc;
	if (size >= 4 && memcmp(header, "%PDF", 4) == 0)
		return DOCUMENT_FORMAT_PDF;
	if (size >= 4 && memcmp(header, "%!PS", 4) == 0)
		return DOCUMENT_FORMAT_POSTSCRIPT;
	if (size >= 3 && header[0] == 0xff && header[1] == 0xd8 && header[2] == 0xff)
		return DOCUMENT_FORMAT_JPEG;
	if (printer_ohos_bytes_look_like_text(header, size))
		return DOCUMENT_FORMAT_TEXT;
	return DOCUMENT_FORMAT_AUTO;
}

static char* printer_ohos_resolve_printer_id(const char* requested)
{
	if (requested && requested[0] != '\0')
	{
		Print_PrinterInfo* info = nullptr;
		const Print_ErrorCode info_rc = OH_Print_QueryPrinterInfo(requested, &info);
		if (info_rc == PRINT_ERROR_NONE)
		{
			const char* id = (info && info->printerId && info->printerId[0] != '\0')
			                     ? info->printerId
			                     : requested;
			char* resolved = _strdup(id);
			if (info)
				OH_Print_ReleasePrinterInfo(info);
			return resolved;
		}
	}

	Print_StringList printer_ids = { 0 };
	Print_ErrorCode rc = OH_Print_QueryPrinterList(&printer_ids);
	if (rc != PRINT_ERROR_NONE)
	{
		WLog_WARN(PRINTER_OHOS_TAG, "OH_Print_QueryPrinterList failed: %s [%" PRIu32 "]",
		          printer_ohos_error_name(rc), (UINT32)rc);
		return nullptr;
	}

	char* first = nullptr;
	char* fallback_default = nullptr;
	char* matched = nullptr;
	for (uint32_t index = 0; index < printer_ids.count; index++)
	{
		const char* list_id = printer_ids.list ? printer_ids.list[index] : nullptr;
		if (!list_id || list_id[0] == '\0')
			continue;

		Print_PrinterInfo* info = nullptr;
		rc = OH_Print_QueryPrinterInfo(list_id, &info);
		if (rc != PRINT_ERROR_NONE)
			WLog_WARN(PRINTER_OHOS_TAG, "OH_Print_QueryPrinterInfo(%s) returned %s [%" PRIu32 "]",
			          list_id, printer_ohos_error_name(rc), (UINT32)rc);

		const char* printer_id =
		    (rc == PRINT_ERROR_NONE && info && info->printerId && info->printerId[0] != '\0')
		        ? info->printerId
		        : list_id;
		const char* printer_name =
		    (rc == PRINT_ERROR_NONE && info && info->printerName) ? info->printerName : nullptr;

		if (!first)
			first = _strdup(printer_id);
		if (!fallback_default && rc == PRINT_ERROR_NONE && info && info->isDefaultPrinter)
			fallback_default = _strdup(printer_id);

		if (requested && requested[0] != '\0' &&
		    ((printer_name && _stricmp(printer_name, requested) == 0) ||
		     strcmp(printer_id, requested) == 0 || strcmp(list_id, requested) == 0))
		{
			matched = _strdup(printer_id);
		}

		if (info)
			OH_Print_ReleasePrinterInfo(info);
		if (matched)
			break;
	}

	OH_Print_ReleasePrinterList(&printer_ids);

	if (matched)
	{
		free(first);
		free(fallback_default);
		return matched;
	}
	if (requested && requested[0] != '\0')
		WLog_WARN(PRINTER_OHOS_TAG, "requested HarmonyOS printer was not found: %s", requested);
	if (fallback_default)
	{
		free(first);
		return fallback_default;
	}
	return first;
}

static BOOL printer_ohos_start_printjob(rdpOhosPrintJob* ohos_printjob)
{
	WINPR_ASSERT(ohos_printjob);
	WINPR_ASSERT(ohos_printjob->printjob.printer);

	rdpOhosPrinter* ohos_printer = printer_ohos_cast_printer(ohos_printjob->printjob.printer);
	if (!ohos_printjob->path)
		return FALSE;

	rdpOhosPrinterDriver* ohos_driver = (rdpOhosPrinterDriver*)ohos_printer->printer.backend;
	if (!printer_ohos_ensure_init(ohos_driver))
		return FALSE;

	char* resolved_printer_id = printer_ohos_resolve_printer_id(ohos_printer->printer_id);
	const char* printer_id = resolved_printer_id;
	if (!printer_id || printer_id[0] == '\0')
	{
		WLog_WARN(PRINTER_OHOS_TAG, "no HarmonyOS printer is available for print job");
		free(resolved_printer_id);
		return FALSE;
	}

	Print_DocumentFormat format = printer_ohos_detect_document_format(ohos_printjob->path);
	const int fd = open(ohos_printjob->path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
	{
		WLog_WARN(PRINTER_OHOS_TAG, "open print spool file failed: errno=%d", errno);
		free(resolved_printer_id);
		return FALSE;
	}

	char* job_name = printer_ohos_printjob_name(ohos_printjob->printjob.id);
	if (!job_name)
	{
		(void)close(fd);
		free(resolved_printer_id);
		return FALSE;
	}

	Print_PrintJob native_job = { 0 };
	uint32_t fd_list[1] = { (uint32_t)fd };
	native_job.jobName = job_name;
	native_job.fdList = fd_list;
	native_job.fdListCount = ARRAYSIZE(fd_list);
	native_job.printerId = printer_id;
	native_job.copyNumber = 1;
	native_job.colorMode = COLOR_MODE_AUTO;
	native_job.duplexMode = DUPLEX_MODE_ONE_SIDED;
	native_job.orientationMode = ORIENTATION_MODE_NONE;
	native_job.printQuality = PRINT_QUALITY_NORMAL;
	native_job.documentFormat = format;

	Print_ErrorCode rc = OH_Print_ConnectPrinter(printer_id);
	if (rc != PRINT_ERROR_NONE)
		WLog_WARN(PRINTER_OHOS_TAG, "OH_Print_ConnectPrinter(%s) returned %s [%" PRIu32 "]",
		          printer_id, printer_ohos_error_name(rc), (UINT32)rc);

	rc = OH_Print_StartPrintJob(&native_job);
	if (rc != PRINT_ERROR_NONE)
		WLog_WARN(PRINTER_OHOS_TAG, "OH_Print_StartPrintJob(%s) failed: %s [%" PRIu32 "]",
		          ohos_printer->printer.name ? ohos_printer->printer.name : "<unnamed>",
		          printer_ohos_error_name(rc), (UINT32)rc);

	free(job_name);
	(void)close(fd);
	free(resolved_printer_id);
	return rc == PRINT_ERROR_NONE;
}

static UINT printer_ohos_write_printjob(rdpPrintJob* printjob, const BYTE* data, size_t size)
{
	rdpOhosPrintJob* ohos_printjob = (rdpOhosPrintJob*)printjob;
	WINPR_ASSERT(ohos_printjob);

	if (ohos_printjob->failed || ohos_printjob->fd < 0)
		return CHANNEL_RC_OK;

	size_t written = 0;
	while (written < size)
	{
		const ssize_t rc =
		    write(ohos_printjob->fd, &data[written], size - written);
		if (rc < 0)
		{
			if (errno == EINTR)
				continue;
			WLog_WARN(PRINTER_OHOS_TAG, "write print spool file failed: errno=%d", errno);
			ohos_printjob->failed = TRUE;
			printer_ohos_close_fd(ohos_printjob);
			return CHANNEL_RC_OK;
		}
		if (rc == 0)
		{
			WLog_WARN(PRINTER_OHOS_TAG, "write print spool file returned zero bytes");
			ohos_printjob->failed = TRUE;
			printer_ohos_close_fd(ohos_printjob);
			return CHANNEL_RC_OK;
		}
		written += (size_t)rc;
	}

	return CHANNEL_RC_OK;
}

static void printer_ohos_close_printjob(rdpPrintJob* printjob)
{
	rdpOhosPrintJob* ohos_printjob = (rdpOhosPrintJob*)printjob;
	WINPR_ASSERT(ohos_printjob);

	rdpOhosPrinter* ohos_printer = printer_ohos_cast_printer(printjob->printer);
	WINPR_ASSERT(ohos_printer);

	if (!ohos_printjob->failed)
	{
		if (ohos_printjob->fd >= 0 && fsync(ohos_printjob->fd) != 0)
		{
			WLog_WARN(PRINTER_OHOS_TAG, "fsync print spool file failed: errno=%d", errno);
			ohos_printjob->failed = TRUE;
		}
		printer_ohos_close_fd(ohos_printjob);

		if (!ohos_printjob->failed && !printer_ohos_start_printjob(ohos_printjob))
			ohos_printjob->failed = TRUE;
	}
	else
		printer_ohos_close_fd(ohos_printjob);

	if (ohos_printjob->path)
		(void)unlink(ohos_printjob->path);

	ohos_printer->printjob = nullptr;
	free(ohos_printjob->path);
	free(ohos_printjob);
}

rdpPrintJob* printer_ohos_create_printjob(rdpPrinter* printer, UINT32 id)
{
	rdpOhosPrinter* ohos_printer = printer_ohos_cast_printer(printer);
	WINPR_ASSERT(ohos_printer);

	if (ohos_printer->printjob != nullptr)
	{
		WLog_WARN(PRINTER_OHOS_TAG, "printjob [printer '%s'] already existing, abort!",
		          printer->name);
		return nullptr;
	}

	rdpOhosPrintJob* ohos_printjob = (rdpOhosPrintJob*)calloc(1, sizeof(rdpOhosPrintJob));
	if (!ohos_printjob)
		return nullptr;

	ohos_printjob->fd = -1;
	for (size_t attempt = 0; attempt < 64; attempt++)
	{
		ohos_printjob->path = printer_ohos_printjob_path(id, attempt);
		if (!ohos_printjob->path)
			goto fail;

		ohos_printjob->fd =
		    open(ohos_printjob->path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		if (ohos_printjob->fd >= 0)
			break;

		const int saved_errno = errno;
		free(ohos_printjob->path);
		ohos_printjob->path = nullptr;
		if (saved_errno != EEXIST)
		{
			WLog_WARN(PRINTER_OHOS_TAG, "create print spool file failed: errno=%d", saved_errno);
			goto fail;
		}
	}

	if (ohos_printjob->fd < 0)
		goto fail;

	ohos_printjob->printjob.id = id;
	ohos_printjob->printjob.printer = printer;
	ohos_printjob->printjob.Write = printer_ohos_write_printjob;
	ohos_printjob->printjob.Close = printer_ohos_close_printjob;

	ohos_printer->printjob = ohos_printjob;
	return &ohos_printjob->printjob;

fail:
	printer_ohos_close_fd(ohos_printjob);
	free(ohos_printjob->path);
	free(ohos_printjob);
	return nullptr;
}

rdpPrintJob* printer_ohos_find_printjob(rdpPrinter* printer, UINT32 id)
{
	rdpOhosPrinter* ohos_printer = printer_ohos_cast_printer(printer);
	WINPR_ASSERT(ohos_printer);

	if (!ohos_printer->printjob)
		return nullptr;
	if (ohos_printer->printjob->printjob.id != id)
		return nullptr;
	return &ohos_printer->printjob->printjob;
}
