#pragma once
class CWFDHelper
{
public:
	CWFDHelper();
	~CWFDHelper();

	HRESULT Init();
	HRESULT Connect(const wchar_t* deviceId);
	HRESULT Close();

protected:
	static VOID WFD_OPEN_SESSION_COMPLETE_Handle (
		_In_ HANDLE         hSessionHandle,
		_In_ PVOID          pvContext,
		_In_ GUID           guidSessionInterface,
		_In_ DWORD          dwError,
		_In_ DWORD          dwReasonCode
		);

private:
	HANDLE m_clientHandle;
	HANDLE m_sessionHandle;
};

