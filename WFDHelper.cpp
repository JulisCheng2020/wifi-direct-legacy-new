#include "stdafx.h"
#include "WFDHelper.h"
#include <wlanapi.h>
#include <wchar.h>

#define OD_LOGA(fmt,...)	{char buf[1024]; sprintf_s(buf, _countof(buf), fmt, __VA_ARGS__); OutputDebugStringA(buf);}
#define OD_LOGW(fmt,...)	{wchar_t buf[1024]; swprintf_s(buf, _countof(buf), fmt, __VA_ARGS__); OutputDebugStringW(buf);}

CWFDHelper::CWFDHelper():m_clientHandle(NULL), m_sessionHandle(NULL)
{

}

CWFDHelper::~CWFDHelper()
{

}

HRESULT CWFDHelper::Init()
{
	DWORD dwClientVersion = WFD_API_VERSION;
	DWORD pdwNegotiatedVersion = 0;
	DWORD connHandleStatus = WFDOpenHandle(dwClientVersion, &pdwNegotiatedVersion, &m_clientHandle); // Opening Client Handle  
	if (connHandleStatus != ERROR_SUCCESS)
	{
		int err = GetLastError();
		OD_LOGA("WFDOpenHandle: {error code: %d}.\n", err);

		return HRESULT_FROM_WIN32(err);
	}

	return S_OK;
}

VOID CWFDHelper::WFD_OPEN_SESSION_COMPLETE_Handle(
	_In_ HANDLE         hSessionHandle,
	_In_ PVOID          pvContext,
	_In_ GUID           guidSessionInterface,
	_In_ DWORD          dwError,
	_In_ DWORD          dwReasonCode
)
{
	OD_LOGA("WFDStartOpenSession: {error code: %x, reason code: %x}.\n", dwError, dwReasonCode);
}

HRESULT CWFDHelper::Connect(const wchar_t* deviceId)
{
	const wchar_t* sub = wcschr(deviceId, '#');
	if (sub)
	{
		DOT11_MAC_ADDRESS addr;
		unsigned int address[6];
		swscanf_s(sub, L"#%x:%x:%x:%x:%x:%x", &address[0], &address[1], &address[2], &address[3], &address[4], &address[5]);

		for (size_t i = 0; i < 6; i++)
		{
			addr[i] = address[i];
		}

		GUID inf = GUID_NULL;
		DWORD connHandleStatus = WFDOpenLegacySession(m_clientHandle, (PDOT11_MAC_ADDRESS)addr,  &m_sessionHandle, &inf);

		if (connHandleStatus != ERROR_SUCCESS)
		{
			int err = GetLastError();
			OD_LOGA("WFDStartOpenSession: {error code: %d}.\n", err);

			return HRESULT_FROM_WIN32(err);
		}
	}

	return S_OK;
}

HRESULT CWFDHelper::Close()
{
	if (m_sessionHandle != NULL)
	{
		WFDCloseSession(m_sessionHandle);
	}

	if (m_clientHandle != NULL)
	{
		WFDCloseHandle(m_clientHandle);
	}

	return S_OK;
}
