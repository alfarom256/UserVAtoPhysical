#include <sal.h>
#include <intrin.h>
#include "PML4.h"

// https://github.com/Cr4sh/PTBypass-PoC/blob/master/driver/src/memory.h
// fucking sick blog and github
typedef struct _MMPTE_HARDWARE
{
	ULONG Valid : 1;
	ULONG Write : 1; // UP version
	ULONG Owner : 1;
	ULONG WriteThrough : 1;
	ULONG CacheDisable : 1;
	ULONG Accessed : 1;
	ULONG Dirty : 1;
	ULONG LargePage : 1;
	ULONG Global : 1;
	ULONG CopyOnWrite : 1; // software field
	ULONG Prototype : 1; // software field
	ULONG reserved : 1; // software field
	ULONG PageFrameNumber : 20;

} MMPTE_HARDWARE, * PMMPTE_HARDWARE;

typedef struct VAD_SUBSECTION{
	PVOID ControlArea;
	DWORD64* SubsectionBase;
	VAD_SUBSECTION* NextSubsection;
	PVOID GlobalPerSessionHead;
	ULONG u;
	ULONG StartingSector;
	ULONG NumberOfFullSections;
	ULONG PtesInSubsection;
	ULONG u1;
	ULONG UnusedPtes;
}VAD_SUBSECTION, *PVAD_SUBSECTION;

typedef struct VAD_NODE {
	VAD_NODE* Left; 
	VAD_NODE* Right;
	VAD_NODE* Parent; 
	ULONG StartingVpn; 
	ULONG EndingVpn; 	
	ULONG ulVpnInfo;
	ULONG ReferenceCount;
	PVOID PushLock;
	ULONG u;
	ULONG u1;
	PVOID u5;
	PVOID u2;
	VAD_SUBSECTION* Subsection; // 0x48 - 0x50
	PVOID FirstProtoPte; // 0x50 - 0x58
	PVOID LastPte; // 0x58 - 0x60
	_LIST_ENTRY ViewLinks;
	PEPROCESS VadsProcess; // 0x60 - 0x68
	PVOID u4;
	PVOID FileObject;
}VAD_NODE, *PVAD_NODE;


uintptr_t get_adjusted_va(_In_ BOOLEAN start, _In_ PVAD_NODE pVad) {
	UCHAR byteOffset = (start ? ((UCHAR*)&pVad->ulVpnInfo)[0] : ((UCHAR*)&pVad->ulVpnInfo)[1]);
	DWORD64 hi_va_start = 0x100000000 * byteOffset;
	hi_va_start += start ? pVad->StartingVpn : pVad->EndingVpn;
	return (uintptr_t)hi_va_start;
}


_Use_decl_annotations_
NTSTATUS DeviceControlDispatch(PDEVICE_OBJECT , PIRP pIrp)
{
	NTSTATUS ret = STATUS_SUCCESS;
	PVOID pIoBuffer = pIrp->AssociatedIrp.SystemBuffer;
	PIO_STACK_LOCATION pIoStack = IoGetCurrentIrpStackLocation(pIrp);
	ULONG cbIncoming = pIoStack->Parameters.DeviceIoControl.InputBufferLength;
	ULONG cbOutgoing = pIoStack->Parameters.DeviceIoControl.OutputBufferLength;
	ULONG ioctl_code = pIoStack->Parameters.DeviceIoControl.IoControlCode;
	ULONG cbWritten = 0;

	switch (ioctl_code) {

	case (ULONG)IOCTL_VIRT_TO_PHYS:
		if (cbOutgoing < sizeof(DWORD64)) {
			return STATUS_BUFFER_TOO_SMALL;
		}

		if (cbIncoming < sizeof(DWORD64)) {
			return STATUS_INVALID_DEVICE_REQUEST;
		}

		ret = ManualVirtualToPhys(pIoBuffer, &cbWritten);
		pIrp->IoStatus.Information = cbWritten;

		break;
	}

	pIrp->IoStatus.Status = ret;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return ret;
}

_Use_decl_annotations_
NTSTATUS ManualVirtualToPhys(PVOID lpSystemBuffer, PULONG pcbWritten)
{
	*pcbWritten = 0;
	DWORD64 lpAddress = *(PDWORD64)lpSystemBuffer;

	ULONG byteIndexIntoPhys = lpAddress & 0xFFF; // the last three bytes indicate the offset into the pte's described page
	DWORD64 userAddressVpn = (lpAddress & 0xFFFFFFFFFFFFF000) / 0x1000;

	PEPROCESS pCurrentProcess = IoGetCurrentProcess();

	// for vad in vads
	PVAD_NODE lpVadRoot = *(PVAD_NODE*)((uintptr_t)pCurrentProcess + 0x7d8);
	PVAD_NODE lpVadIter = lpVadRoot;
	PVAD_NODE lpTargetVad = NULL;

	while (lpVadIter) {
		
		DWORD64 start_va_adjusted = get_adjusted_va(TRUE, lpVadIter);
		DWORD64 end_va_adjusted = get_adjusted_va(FALSE, lpVadIter);

		// if the userAddressVpn is larger than the adjusted end_va
		// of the current VAD, we need to go to the right to get the 
		// next "largest" entry in the tree
		if (userAddressVpn > end_va_adjusted) {
			if (lpVadIter->Right == NULL) {
				return STATUS_INVALID_DEVICE_REQUEST;
			}
			lpVadIter = lpVadIter->Right;
		}

		// if the userAddress vpn is SMALLER than the adjusted end_va
		// of the current VAD, we need to go to the right to get the 
		// next "largest" entry in the tree 
		else if (userAddressVpn < start_va_adjusted){
			if (lpVadIter->Left == NULL) {
				return STATUS_INVALID_DEVICE_REQUEST;
			}
			lpVadIter = lpVadIter->Left;
		}
		// if start_va < userAddress > end_va
		// we found the right VAD
		else {
			lpTargetVad = lpVadIter;
			break;
		}
	}

	if (lpTargetVad == NULL) {
		return STATUS_NOT_FOUND;
	}

	// this will give us the "count" of pages we need to iterate over
	// e.g. 
	// vad_start = 0x1230
	// user_vpn = 0x1250
	// user_vpn - vad_start = 0x20
	// num_pages_to_skip = 0x20
	ULONG indexOfPage = (ULONG)(userAddressVpn - lpTargetVad->StartingVpn);

	PVAD_SUBSECTION pSection = lpTargetVad->Subsection;
	PVAD_SUBSECTION pTargetSection = NULL;
	
	while (pSection->NextSubsection != NULL) {
		// if the index of the page is less than the number of pages
		// in the current section, we've found the section that has the 
		// pte that describes our target memory!
		if (indexOfPage < pSection->PtesInSubsection) {
			pTargetSection = pSection;
			break;
		}

		indexOfPage -= pSection->PtesInSubsection;
		pSection = pSection->NextSubsection;
	}

	if (pTargetSection == NULL) {
		return STATUS_NOT_FOUND;
	}

	// get the indexed pte
	DWORD64 targetPte = pTargetSection->SubsectionBase[indexOfPage];
	DWORD64 pPte = ((targetPte >> 12) & 0xFFFFFFFFF) * 0x1000;
	PVOID pPhysicalAddr = (PVOID)(pPte + (DWORD64)byteIndexIntoPhys);


	*(DWORD64*)lpSystemBuffer = (DWORD64)pPhysicalAddr;
	*pcbWritten = 8;
	return STATUS_SUCCESS;
}
