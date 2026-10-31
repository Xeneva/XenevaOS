/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

#ifndef __ACPIOSL_H__
#define __ACPIOSL_H__

#include <aurora.h>
#include <acpi.h>
/*
#ifdef __cplusplus
AU_EXTERN{
#endif
	AU_FUNC ACPI_STATUS AcpiOsInitialize();
	AU_FUNC ACPI_STATUS AcpiOsTerminate();
	AU_FUNC ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer();
	AU_FUNC ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *PredefinedObject, ACPI_STRING *NewValue);
	AU_FUNC ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER* ExistingTable, ACPI_TABLE_HEADER **NewTable);
	AU_FUNC ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_PHYSICAL_ADDRESS *NewAddress,
		UINT32 *NewTableLength);
	AU_FUNC void* AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length);
	AU_FUNC void AcpiOsUnmapMemory(void* where, ACPI_SIZE length);
	AU_FUNC ACPI_STATUS AcpiOsGetPhysicalAddress(void* logicalAddress, ACPI_PHYSICAL_ADDRESS *PhysicalAddress);
	AU_FUNC void* AcpiOsAllocate(ACPI_SIZE Size);
	AU_FUNC void AcpiOsFree(void* memory);
	AU_FUNC BOOLEAN AcpiOsReadable(void *memory, ACPI_SIZE Length);
	AU_FUNC BOOLEAN AcpiOsWritable(void* memory, ACPI_SIZE Length);
	AU_FUNC ACPI_THREAD_ID AcpiOsGetThreadId();
	AU_FUNC ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void* Context);
	AU_FUNC void AcpiOsSleep(UINT64 Millis);
	AU_FUNC void AcpiOsStall(UINT32 Micros);
	AU_FUNC ACPI_STATUS AcpiOsCreateMutex(ACPI_MUTEX *OutHandle);
	AU_FUNC void AcpiOsDeleteMutex(ACPI_MUTEX Handle);
	AU_FUNC void AcpiOsDeleteMutex(ACPI_MUTEX Handle);
	AU_FUNC ACPI_STATUS AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout);
	AU_FUNC void AcpiOsReleaseMutex(ACPI_MUTEX Handle);
	AU_FUNC ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE *OutHandle);
	AU_FUNC ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle);
	AU_FUNC ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout);
	AU_FUNC ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units);
	AU_FUNC ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle);
	AU_FUNC void AcpiOsDeleteLock(ACPI_SPINLOCK Handle);
	AU_FUNC ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle);
	AU_FUNC void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags);
	AU_FUNC ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler, void *Context);
	AU_FUNC ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER Handler);
	AU_FUNC void ACPI_INTERNAL_VAR_XFACE AcpiOsPrintf(const char *Format, ...);
	AU_FUNC void AcpiOsVprintf(const char *Format, va_list Args);
	AU_FUNC int AcpiOsAcquireGlobalLock(UINT32 *lock);
	AU_FUNC int AcpiOsReleaseGlobalLock(UINT32 *lock);
	AU_FUNC ACPI_STATUS AcpiOsSignal(UINT32 Function, void *Info);
	AU_FUNC ACPI_STATUS
		AcpiOsEnterSleep(
		UINT8                   SleepState,
		UINT32                  RegaValue,
		UINT32                  RegbValue);
	AU_FUNC ACPI_STATUS
		AcpiOsReadMemory(
		ACPI_PHYSICAL_ADDRESS   Address,
		UINT64                  *Value,
		UINT32                  Width);
	AU_FUNC ACPI_STATUS
		AcpiOsWriteMemory(
		ACPI_PHYSICAL_ADDRESS Address,
		UINT64 Value,
		UINT32 Width);
	AU_FUNC ACPI_STATUS
		AcpiOsReadPort(
		ACPI_IO_ADDRESS Address,
		UINT32 *Value,
		UINT32 Width);
	AU_FUNC ACPI_STATUS
		AcpiOsWritePort(
		ACPI_IO_ADDRESS Address,
		UINT32 Value,
		UINT32 Width);
	AU_FUNC UINT64 AcpiOsGetTimer();
	AU_FUNC void AcpiOsWaitEventsComplete();
	AU_FUNC ACPI_STATUS
		AcpiOsReadPciConfiguration(
		ACPI_PCI_ID             *PciId,
		UINT32                  Reg,
		UINT64                  *Value,
		UINT32                  Width);
	AU_FUNC ACPI_STATUS
		AcpiOsWritePciConfiguration(
		ACPI_PCI_ID             *PciId,
		UINT32                  Reg,
		UINT64                  Value,
		UINT32                  Width);

#ifdef __cplusplus
}
#endif

*/
#endif