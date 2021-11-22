#include <Windows.h>
#include <stdio.h>
#include "..\PMLE4-Stuff\DriverCommon.h"

int main() {
	HANDLE hDevice = CreateFileA(
		"\\\\.\\pml",
		GENERIC_READ,
		FILE_SHARE_WRITE | FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	);

	if (!hDevice) {
		printf("Could not open handle : %d\n", GetLastError());
		return -1;
	}

	const char* dummy_data = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	DWORD64 userVirtual = (DWORD64)dummy_data;
	DWORD dwBytesReturned = 0;

	BOOL res = DeviceIoControl(hDevice, IOCTL_VIRT_TO_PHYS, &userVirtual, sizeof(DWORD64), &userVirtual, sizeof(DWORD64), &dwBytesReturned, NULL);
	if (!res) {
		printf("DeviceIoControl Failed : %d\n", GetLastError());
	}
	printf("%p has physical address of %p\n", dummy_data, userVirtual);
	while (TRUE) {}
}