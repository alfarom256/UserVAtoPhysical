#include <ntifs.h>
#include <ntddk.h>
#include <sal.h>
#include "PML4.h"


void SimpleDriverUnload(PDRIVER_OBJECT pDrvObj)
{
	UNICODE_STRING symlinkName = RTL_CONSTANT_STRING(L"\\??\\pml");
	IoDeleteSymbolicLink(&symlinkName);
	IoDeleteDevice(pDrvObj->DeviceObject);
}


NTSTATUS SimpleDriverCreateClose(PDEVICE_OBJECT, _In_ PIRP pIrp)
{
	pIrp->IoStatus.Information = 0;
	pIrp->IoStatus.Status = STATUS_SUCCESS;

	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT pDriverObject, _In_ PUNICODE_STRING pRegPath) {
	UNREFERENCED_PARAMETER(pRegPath);

	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT pDevObj = NULL;
	UNICODE_STRING DevName = RTL_CONSTANT_STRING(L"\\Device\\pml");
	UNICODE_STRING symName = RTL_CONSTANT_STRING(L"\\??\\pml");

	status = IoCreateDevice(pDriverObject, 0, &DevName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &pDevObj);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	pDevObj->Flags |= DO_BUFFERED_IO;
	status = IoCreateSymbolicLink(&symName, &DevName);

	if (!NT_SUCCESS(status)) {
		IoDeleteDevice(pDevObj);
		return status;
	}


	pDriverObject->DriverUnload = SimpleDriverUnload;
	pDriverObject->MajorFunction[IRP_MJ_CREATE] = SimpleDriverCreateClose;
	pDriverObject->MajorFunction[IRP_MJ_CLOSE] = SimpleDriverCreateClose;
	pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControlDispatch;
	return status;
}