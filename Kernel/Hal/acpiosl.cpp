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

#include <Hal/acpiosl.h>
#include <acpi.h>
#include <Mm/vmmngr.h>
#include <Mm/kmalloc.h>
#include <pcie.h>
#include <Mm/pmmngr.h>
#include <aucon.h>
#include <Hal/basicacpi.h>
#include <Hal/x86_64_lowlevel.h>

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsInitialize() {
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsTerminate() {
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer() {
	void* rsdp = AuACPIGetRSDP();
	return (ACPI_PHYSICAL_ADDRESS)rsdp;
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *PredefinedObject, ACPI_STRING *NewValue) {
	*NewValue = 0;
	return AE_OK;
}

AU_EXTERN AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER* ExistingTable, ACPI_TABLE_HEADER **NewTable) {
	*NewTable = 0;
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_PHYSICAL_ADDRESS *NewAddress,
	UINT32 *NewTableLength) {
	*NewAddress = 0;
	*NewTableLength = 0;
	return AE_OK;
}

AU_EXTERN AU_FUNC void* AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length) {
	size_t align_off = PhysicalAddress & (PAGE_SIZE - 1);
	uint64_t* loc = AuGetFreePage(false, NULL);
	uint64_t virt = (uint64_t)loc;
	uint64_t phys = PhysicalAddress - align_off;
	for (int i = 0; i < Length + align_off; i++)
		AuMapPage(phys + i * 4096, virt + i * 4096, 0);
	return (void*)(virt + align_off);
}

AU_EXTERN AU_FUNC void AcpiOsUnmapMemory(void* where, ACPI_SIZE length) {
	AuFreePages((uint64_t)where, true, length);
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsGetPhysicalAddress(void* logicalAddress, ACPI_PHYSICAL_ADDRESS *PhysicalAddress) {
	void* phys = AuGetPhysicalAddress((uint64_t)logicalAddress);
	if (phys)
		*PhysicalAddress = (ACPI_PHYSICAL_ADDRESS)phys;
	else
		*PhysicalAddress = (ACPI_PHYSICAL_ADDRESS)logicalAddress;

	return AE_OK;
}

AU_EXTERN AU_FUNC void* AcpiOsAllocate(ACPI_SIZE Size) {
	return kmalloc(Size);
}

AU_EXTERN AU_FUNC void AcpiOsFree(void* memory) {
	AuTextOut("Freeing up \n");
	return kfree(memory);
}

AU_EXTERN AU_FUNC BOOLEAN AcpiOsReadable(void *memory, ACPI_SIZE Length) {
	return TRUE;
}

AU_EXTERN AU_FUNC BOOLEAN AcpiOsWritable(void* memory, ACPI_SIZE Length) {
	return TRUE;
}

AU_EXTERN AU_FUNC ACPI_THREAD_ID AcpiOsGetThreadId() {
	return (ACPI_THREAD_ID)-1;
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void* Context) {
	Function(Context);
	/* here we need scheduler to execute threads */
	return AE_OK;
}

AU_EXTERN AU_FUNC void AcpiOsSleep(UINT64 Millis) {
	for (int i = 0; i < Millis; i++)
		;
}

AU_EXTERN AU_FUNC void AcpiOsStall(UINT32 Micros) {
	for (int i = 0; i < Micros; i++)
		;
}

typedef struct _mutex_ {
	uint8_t locked;
}mutex_t;

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsCreateMutex(ACPI_MUTEX *OutHandle) {
	
	mutex_t *mut = (mutex_t*)kmalloc(sizeof(mutex_t));
	mut->locked = 0;
	*OutHandle = mut;
	return AE_OK;//AcpiOsCreateSemaphore(1, 1, OutHandle);
}

AU_EXTERN AU_FUNC void AcpiOsDeleteMutex(ACPI_MUTEX Handle) {
	kfree(Handle);
}


AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout)
{
	/*mutex_t* mut = (mutex_t*)Handle;
	mut->locked = 1;*/
	return AE_OK;//AcpiOsWaitSemaphore(Handle, 1, Timeout);
}
AU_EXTERN AU_FUNC void AcpiOsReleaseMutex(ACPI_MUTEX Handle)
{
	/*mutex_t* mut = (mutex_t*)Handle;
	mut->locked = 0;*/
	//AcpiOsSignalSemaphore(Handle, 1);
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE *OutHandle)
{
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)
{
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout)
{
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units)
{
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle)
{
	mutex_t *mut = (mutex_t*)kmalloc(sizeof(mutex_t));
	mut->locked = 0;
	*OutHandle = mut;
	return AE_OK;
}

AU_EXTERN AU_FUNC void AcpiOsDeleteLock(ACPI_SPINLOCK Handle)
{
}

AU_EXTERN AU_FUNC ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle)
{
	return AE_OK;
}

AU_EXTERN AU_FUNC void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags)
{
}

struct context_converter
{
	ACPI_OSD_HANDLER handler;
	void* ctxt;
};

static uint8_t interrupt_converter(size_t vector, void* param)
{
	AuTextOut("ACPI interrupt handler called\n");
	context_converter* ctx = (context_converter*)param;
	return ctx->handler(ctx->ctxt);
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler, void *Context){
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER Handler)
{
	return AE_OK;
}

AU_EXTERN AU_FUNC void ACPI_INTERNAL_VAR_XFACE AcpiOsPrintf(const char *Format, ...)
{
	va_list args;
	va_start(args, Format);
	AuTextOut(Format);
	va_end(args);
}

AU_EXTERN AU_FUNC void AcpiOsVprintf(const char *Format, va_list Args)
{
	AuTextOut(Format);
}

AU_EXTERN AU_FUNC int AcpiOsAcquireGlobalLock(UINT32 *lock)
{
	return AE_OK;
}
AU_EXTERN AU_FUNC int AcpiOsReleaseGlobalLock(UINT32 *lock)
{
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_STATUS AcpiOsSignal(UINT32 Function, void *Info)
{
	return AE_NOT_FOUND;
}

AU_EXTERN AU_FUNC ACPI_STATUS
AcpiOsEnterSleep(
UINT8                   SleepState,
UINT32                  RegaValue,
UINT32                  RegbValue)
{
	return AE_NOT_IMPLEMENTED;
}

AU_EXTERN AU_FUNC ACPI_STATUS
AcpiOsReadMemory(
ACPI_PHYSICAL_ADDRESS   Address,
UINT64                  *Value,
UINT32                  Width)
{
	AuTextOut("read mem\n");
	switch (Width)
	{
	case 8:
		*Value = *(UINT8*)Address;
		break;
	case 16:
		*Value = *(UINT16*)Address;
		break;
	case 32:
		*Value = *(UINT32*)Address;
		break;
	case 64:
		*Value = *(UINT64*)Address;
		break;
	default:
		return AE_BAD_VALUE;
	}
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_STATUS
AcpiOsWriteMemory(
ACPI_PHYSICAL_ADDRESS Address,
UINT64 Value,
UINT32 Width)
{
	AuTextOut("Write mem\n");
	switch (Width)
	{
	case 8:
		*(UINT8*)Address = Value;
		break;
	case 16:
		*(UINT16*)Address = Value;
		break;
	case 32:
		*(UINT32*)Address = Value;
		break;
	case 64:
		*(UINT64*)Address = Value;
		break;
	default:
		return AE_BAD_VALUE;
	}
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_STATUS
AcpiOsReadPort(
ACPI_IO_ADDRESS Address,
UINT32 *Value,
UINT32 Width)
{
	AuTextOut("read port\n");
	switch (Width)
	{
	case 8:
	case 16:
	case 32:
		*Value = x64_inportd(Address);
		break;
	default:
		return AE_BAD_VALUE;
	}
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_STATUS
AcpiOsWritePort(
ACPI_IO_ADDRESS Address,
UINT32 Value,
UINT32 Width)
{
	AuTextOut("Write port\n");
	switch (Width)
	{
	case 8:
	case 16:
	case 32:
		x64_outportd(Address, Value);
		break;
	default:
		return AE_BAD_VALUE;
	}
	return AE_OK;
}

AU_EXTERN AU_FUNC UINT64 AcpiOsGetTimer()
{
	return 0; // sys_timer() * 10000;
}

AU_EXTERN AU_FUNC void AcpiOsWaitEventsComplete()
{
}

AU_EXTERN AU_FUNC ACPI_STATUS
AcpiOsReadPciConfiguration(
ACPI_PCI_ID             *PciId,
UINT32                  Reg,
UINT64                  *Value,
UINT32                  Width)
{
	uint32_t result = 0;
	result = AuPCIERead(PciId->Device, Reg, PciId->Bus, PciId->Device, PciId->Function);
	*Value = result;
	if (!result)
		return AE_NOT_IMPLEMENTED;
	return AE_OK;
}

AU_EXTERN AU_FUNC ACPI_STATUS
AcpiOsWritePciConfiguration(
ACPI_PCI_ID             *PciId,
UINT32                  Reg,
UINT64                  Value,
UINT32                  Width)
{
	AuPCIEWrite(PciId->Device, Reg, Value, PciId->Bus, PciId->Device, PciId->Function);
	return AE_OK;
}

