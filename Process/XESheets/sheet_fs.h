/**
 * @file sheet_fs.h
 * @brief RFC 4180 CSV file persistence for XESheets
 */

#ifndef __SHEET_FS_H__
#define __SHEET_FS_H__

#include "sheet_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Export active tab to CSV file following RFC 4180 */
bool SheetFSExportCSV(SheetWorkbook* wb, const char* filepath);

/* Import CSV file into active tab following RFC 4180 */
bool SheetFSImportCSV(SheetWorkbook* wb, const char* filepath);

#ifdef __cplusplus
}
#endif

#endif /* __SHEET_FS_H__ */
