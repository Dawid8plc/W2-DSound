/**
* Copyright (C) 2020 Elisha Riedlinger
*
* This software is  provided 'as-is', without any express  or implied  warranty. In no event will the
* authors be held liable for any damages arising from the use of this software.
* Permission  is granted  to anyone  to use  this software  for  any  purpose,  including  commercial
* applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*   1. The origin of this software must not be misrepresented; you must not claim that you  wrote the
*      original  software. If you use this  software  in a product, an  acknowledgment in the product
*      documentation would be appreciated but is not required.
*   2. Altered source versions must  be plainly  marked as such, and  must not be  misrepresented  as
*      being the original software.
*   3. This notice may not be removed or altered from any source distribution.
*/

#include "dsound.h"
#include <windows.h>

#pragma comment (lib, "dxguid.lib")

//std::ofstream Log::LOG("dsound.log");
AddressLookupTable<void> ProxyAddressLookupTable = AddressLookupTable<void>();
int sfxVolume = 100;

DirectSoundCreateProc m_pDirectSoundCreate;
DirectSoundEnumerateAProc m_pDirectSoundEnumerateA;
DirectSoundEnumerateWProc m_pDirectSoundEnumerateW;
DllCanUnloadNowProc m_pDllCanUnloadNow;
DllGetClassObjectProc m_pDllGetClassObject;
DirectSoundCaptureCreateProc m_pDirectSoundCaptureCreate;
DirectSoundCaptureEnumerateAProc m_pDirectSoundCaptureEnumerateA;
DirectSoundCaptureEnumerateWProc m_pDirectSoundCaptureEnumerateW;
GetDeviceIDProc m_pGetDeviceID;
DirectSoundFullDuplexCreateProc m_pDirectSoundFullDuplexCreate;
DirectSoundCreate8Proc m_pDirectSoundCreate8;
DirectSoundCaptureCreate8Proc m_pDirectSoundCaptureCreate8;


HMODULE Modules[128];
int Count = 0;
HMODULE dsounddll;

BOOL EnableWK(void)
{
	BOOL RetVal = 1;
	char* cmd = GetCommandLineA();
	BOOLEAN in_QM = FALSE, in_TEXT = FALSE, in_SPACE = TRUE;
	int st, en;
	char tmp[16];
	int i = 0, j = 0;
	while (1) {
		char a = cmd[i];
		if (in_QM) {
			if (a == '\"') { in_QM = FALSE; }
			else { if (cmd[i + 1] == 0) in_QM = FALSE; if (j < 15) { tmp[j] = a; j++; } }
		}
		else {
			switch (a) {
			case '\"':
				in_QM = TRUE;
				in_TEXT = TRUE;
				if (in_SPACE) { st = i; j = 0; }
				in_SPACE = FALSE;
				break;
			case ' ':
			case '\t':
			case '\n':
			case '\r':
			case '\0':
				if (in_TEXT) {
					tmp[j] = '\0'; en = i;
					if (!strcmp(tmp, "/nowk")) {
						RetVal = 0;
						for (int k = st; k < en; k++) cmd[k] = ' ';
					}
				}
				in_TEXT = FALSE;
				in_SPACE = TRUE;
				break;
			default:
				in_TEXT = TRUE;
				if (in_SPACE) { st = i; j = 0; }
				if (j < 15) { tmp[j] = a; j++; }
				in_SPACE = FALSE;
				break;
			}
		}
		if (!cmd[i]) break;
		i++;
	}
	return RetVal;
}

void setVolume() {
	FILE* fptr;
	// Open a file in read mode
	fptr = fopen("volumeSFX.txt", "r");
	if (fptr == NULL) {
		sfxVolume = 100;
	}
	else {
		// Store the content of the file
		char strVol[4];
		// Read the content and store it inside strVol
		fgets(strVol, 4, fptr);
		// Close the file
		fclose(fptr);

		char* endptr;
		int newVol = strtol(strVol, &endptr, 10);

		if (*endptr != '\0' || endptr == strVol) {
			//Invalid number, set to default
			sfxVolume = 100;
		}
		else {
			//Set Volume to the number in the file
			sfxVolume = newVol;
		}
	}
}

struct ThreadData {
	HANDLE directoryHandle;
	wchar_t* directoryPath;
	wchar_t* targetFileName;
};

void MonitorDirectoryThread(void* data) {
	struct ThreadData* threadData = (struct ThreadData*)data;
	HANDLE directoryHandle = threadData->directoryHandle;
	wchar_t* directoryPath = threadData->directoryPath;
	wchar_t* targetFileName = threadData->targetFileName;

	// Buffer to store the changes
	const int bufferSize = 4096;
	BYTE buffer[4096];

	DWORD bytesRead;
	FILE_NOTIFY_INFORMATION* fileInfo;

	while (ReadDirectoryChangesW(
		directoryHandle,
		buffer,
		bufferSize,
		FALSE, // Ignore subtree
		FILE_NOTIFY_CHANGE_LAST_WRITE, // Monitor file write changes
		&bytesRead,
		NULL,
		NULL
	)) {
		fileInfo = (FILE_NOTIFY_INFORMATION*)buffer;

		//Make sure that the file that got written to is the file we are monitoring
		if (wcsncmp(fileInfo->FileName, targetFileName, fileInfo->FileNameLength / sizeof(wchar_t)) != 0)
			continue;

		do {

			switch (fileInfo->Action) {
			case FILE_ACTION_MODIFIED:
				setVolume();
				break;
			default:
				break;
			}

			// Move to the next entry in the buffer
			fileInfo = (FILE_NOTIFY_INFORMATION*)((char*)fileInfo + fileInfo->NextEntryOffset);

		} while (fileInfo->NextEntryOffset != 0);
	}

	// Close the directory handle when the monitoring loop exits
	CloseHandle(directoryHandle);
}

void MonitorDirectory(wchar_t* directoryPath, wchar_t* targetFileName)
{
	// Create a directory handle
	HANDLE directoryHandle = CreateFileW(
		directoryPath,
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		NULL
	);

	if (directoryHandle == INVALID_HANDLE_VALUE) {
		wprintf(L"Error opening directory: %d\n", GetLastError());
		return;
	}

	// Prepare data to pass to the thread
	struct ThreadData* threadData = (struct ThreadData*)malloc(sizeof(struct ThreadData));
	if (threadData == NULL) {
		wprintf(L"Memory allocation failed\n");
		CloseHandle(directoryHandle);
		return;
	}
	threadData->directoryHandle = directoryHandle;
	threadData->directoryPath = directoryPath;
	threadData->targetFileName = targetFileName;

	// Create a thread for monitoring
	HANDLE threadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MonitorDirectoryThread, threadData, 0, NULL);

	//Closes the handle to the thread, however this does not stop the thread
	CloseHandle(threadHandle);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved)
{
	UNREFERENCED_PARAMETER(lpReserved);

	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
	{
		// Load dll
		char path[MAX_PATH];
		GetSystemDirectoryA(path, MAX_PATH);
		strcat_s(path, "\\dsound.dll");
		//Log() << "Loading " << path;
		dsounddll = LoadLibraryA(path);

		// Get function addresses
		m_pDirectSoundCreate = (DirectSoundCreateProc)GetProcAddress(dsounddll, "DirectSoundCreate");
		m_pDirectSoundEnumerateA = (DirectSoundEnumerateAProc)GetProcAddress(dsounddll, "DirectSoundEnumerateA");
		m_pDirectSoundEnumerateW = (DirectSoundEnumerateWProc)GetProcAddress(dsounddll, "DirectSoundEnumerateW");
		m_pDllCanUnloadNow = (DllCanUnloadNowProc)GetProcAddress(dsounddll, "DllCanUnloadNow");
		m_pDllGetClassObject = (DllGetClassObjectProc)GetProcAddress(dsounddll, "DllGetClassObject");
		m_pDirectSoundCaptureCreate = (DirectSoundCaptureCreateProc)GetProcAddress(dsounddll, "DirectSoundCaptureCreate");
		m_pDirectSoundCaptureEnumerateA = (DirectSoundCaptureEnumerateAProc)GetProcAddress(dsounddll, "DirectSoundCaptureEnumerateA");
		m_pDirectSoundCaptureEnumerateW = (DirectSoundCaptureEnumerateWProc)GetProcAddress(dsounddll, "DirectSoundCaptureEnumerateW");
		m_pGetDeviceID = (GetDeviceIDProc)GetProcAddress(dsounddll, "GetDeviceID");
		m_pDirectSoundFullDuplexCreate = (DirectSoundFullDuplexCreateProc)GetProcAddress(dsounddll, "DirectSoundFullDuplexCreate");
		m_pDirectSoundCreate8 = (DirectSoundCreate8Proc)GetProcAddress(dsounddll, "DirectSoundCreate8");
		m_pDirectSoundCaptureCreate8 = (DirectSoundCaptureCreate8Proc)GetProcAddress(dsounddll, "DirectSoundCaptureCreate8");

		if (EnableWK()) {
			//Load wk*.dll
			GetModuleFileNameA(0, path, MAX_PATH);
			char* dirend = strrchr(path, '\\') + 1;
			*dirend = 0;
			strcat(path, "wk*.dll");
			WIN32_FIND_DATAA fd;
			HANDLE fh = FindFirstFileA(path, &fd);
			if (fh != INVALID_HANDLE_VALUE)
			{
				do {
					HMODULE hm = LoadLibraryA(fd.cFileName);
					if (hm && Count < 128) Modules[Count++] = hm;
					
				} while (FindNextFileA(fh, &fd));
				FindClose(fh);
			}
		}

		//Gets the current working directory, and creates a path containing it and the volumeSFX.txt file that we want to monitor for changes
		wchar_t directoryPath[1024];
		_wgetcwd(directoryPath, sizeof(directoryPath) / sizeof(directoryPath[0]));
		wchar_t* targetFileName = L"volumeSFX.txt";
		MonitorDirectory(directoryPath, targetFileName);

		//Load the volume
		setVolume();

		break;
	}
	case DLL_PROCESS_DETACH:
		while (Count) FreeLibrary(Modules[--Count]);
		FreeLibrary(dsounddll);
		break;
	}
	return TRUE;
}

HRESULT WINAPI DirectSoundCreate(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter)
{
	if (!m_pDirectSoundCreate)
	{
		return E_FAIL;
	}

	HRESULT hr = m_pDirectSoundCreate(pcGuidDevice, ppDS, pUnkOuter);

	if (SUCCEEDED(hr) && ppDS)
	{
		*ppDS = new m_IDirectSound8((IDirectSound8*)*ppDS);
	}

	return hr;
}

HRESULT WINAPI DirectSoundEnumerateA(LPDSENUMCALLBACKA pDSEnumCallback, LPVOID pContext)
{
	if (!m_pDirectSoundEnumerateA)
	{
		return E_FAIL;
	}

	return m_pDirectSoundEnumerateA(pDSEnumCallback, pContext);
}

HRESULT WINAPI DirectSoundEnumerateW(LPDSENUMCALLBACKW pDSEnumCallback, LPVOID pContext)
{
	if (!m_pDirectSoundEnumerateW)
	{
		return E_FAIL;
	}

	return m_pDirectSoundEnumerateW(pDSEnumCallback, pContext);
}

HRESULT WINAPI DllCanUnloadNow()
{
	if (!m_pDllCanUnloadNow)
	{
		return E_FAIL;
	}

	return m_pDllCanUnloadNow();
}

HRESULT WINAPI DllGetClassObject(IN REFCLSID rclsid, IN REFIID riid, OUT LPVOID FAR* ppv)
{
	if (!m_pDllGetClassObject)
	{
		return E_FAIL;
	}

	HRESULT hr = m_pDllGetClassObject(rclsid, riid, ppv);

	if (SUCCEEDED(hr))
	{
		genericQueryInterface(riid, ppv);
	}

	return hr;
}

HRESULT WINAPI DirectSoundCaptureCreate(LPCGUID pcGuidDevice, LPDIRECTSOUNDCAPTURE *ppDSC, LPUNKNOWN pUnkOuter)
{
	if (!m_pDirectSoundCaptureCreate)
	{
		return E_FAIL;
	}

	HRESULT hr = m_pDirectSoundCaptureCreate(pcGuidDevice, ppDSC, pUnkOuter);

	if (SUCCEEDED(hr) && ppDSC)
	{
		*ppDSC = new m_IDirectSoundCapture8(*ppDSC);
	}

	return hr;
}

HRESULT WINAPI DirectSoundCaptureEnumerateA(LPDSENUMCALLBACKA pDSEnumCallback, LPVOID pContext)
{
	if (!m_pDirectSoundCaptureEnumerateA)
	{
		return E_FAIL;
	}

	return m_pDirectSoundCaptureEnumerateA(pDSEnumCallback, pContext);
}

HRESULT WINAPI DirectSoundCaptureEnumerateW(LPDSENUMCALLBACKW pDSEnumCallback, LPVOID pContext)
{
	if (!m_pDirectSoundCaptureEnumerateW)
	{
		return E_FAIL;
	}

	return m_pDirectSoundCaptureEnumerateW(pDSEnumCallback, pContext);
}

HRESULT WINAPI GetDeviceID(LPCGUID pGuidSrc, LPGUID pGuidDest)
{
	return m_pGetDeviceID(pGuidSrc, pGuidDest);
}

HRESULT WINAPI DirectSoundFullDuplexCreate(LPCGUID pcGuidCaptureDevice, LPCGUID pcGuidRenderDevice, LPCDSCBUFFERDESC pcDSCBufferDesc, LPCDSBUFFERDESC pcDSBufferDesc, HWND hWnd,
	DWORD dwLevel, LPDIRECTSOUNDFULLDUPLEX* ppDSFD, LPDIRECTSOUNDCAPTUREBUFFER8 *ppDSCBuffer8, LPDIRECTSOUNDBUFFER8 *ppDSBuffer8, LPUNKNOWN pUnkOuter)
{
	if (!m_pDirectSoundFullDuplexCreate)
	{
		return E_FAIL;
	}

	HRESULT hr = m_pDirectSoundFullDuplexCreate(pcGuidCaptureDevice, pcGuidRenderDevice, pcDSCBufferDesc, pcDSBufferDesc, hWnd, dwLevel, ppDSFD, ppDSCBuffer8, ppDSBuffer8, pUnkOuter);

	if (SUCCEEDED(hr))
	{
		if (ppDSFD)
		{
			*ppDSFD = new m_IDirectSoundFullDuplex8(*ppDSFD);
		}
		if (ppDSCBuffer8)
		{
			*ppDSCBuffer8 = new m_IDirectSoundCaptureBuffer8(*ppDSCBuffer8);
		}
		if (ppDSBuffer8)
		{
			*ppDSBuffer8 = new m_IDirectSoundBuffer8(*ppDSBuffer8);
		}
	}

	return hr;
}

HRESULT WINAPI DirectSoundCreate8(LPCGUID pcGuidDevice, LPDIRECTSOUND8 *ppDS8, LPUNKNOWN pUnkOuter)
{
	if (!m_pDirectSoundCreate8)
	{
		return E_FAIL;
	}

	HRESULT hr = m_pDirectSoundCreate8(pcGuidDevice, ppDS8, pUnkOuter);

	if (SUCCEEDED(hr) && ppDS8)
	{
		*ppDS8 = new m_IDirectSound8(*ppDS8);
	}

	return hr;
}

HRESULT WINAPI DirectSoundCaptureCreate8(LPCGUID pcGuidDevice, LPDIRECTSOUNDCAPTURE8 *ppDSC8, LPUNKNOWN pUnkOuter)
{
	if (!m_pDirectSoundCaptureCreate8)
	{
		return E_FAIL;
	}

	HRESULT hr = m_pDirectSoundCaptureCreate8(pcGuidDevice, ppDSC8, pUnkOuter);

	if (SUCCEEDED(hr) && ppDSC8)
	{
		*ppDSC8 = new m_IDirectSoundCapture8(*ppDSC8);
	}

	return hr;
}
