#pragma once
#include <ntifs.h>
#include "DriverCommon.h"

NTSTATUS DeviceControlDispatch(_In_ PDEVICE_OBJECT pDevObj, _In_ PIRP pIrp);
NTSTATUS ManualVirtualToPhys(_In_ PVOID lpSystemBuffer, _Inout_ PULONG pcbWritten);