//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

/// App-specific exception class
class WlanHostedNetworkException : public std::exception
{
public:
    WlanHostedNetworkException(const char* message)
        : std::exception(message),
          _hr(S_OK)
    {}

    WlanHostedNetworkException(const char* message, HRESULT hr)
        : std::exception(message),
        _hr(hr)
    {}

    HRESULT GetErrorCode() const
    {
        return _hr;
    }
private:
    HRESULT _hr;
};

/// Helper interface that can be notified about changes in the "soft AP"
class IWlanHostedNetworkListener
{
public:
    virtual ~IWlanHostedNetworkListener() {}

    virtual void OnDeviceConnected(std::wstring remoteHostName) = 0;
    virtual void OnDeviceDisconnected(std::wstring deviceId) = 0;

    virtual void OnAdvertisementStarted() = 0;
    virtual void OnAdvertisementStopped(std::wstring message) = 0;
    virtual void OnAdvertisementAborted(std::wstring message) = 0;
	virtual void OnEnumerationCompleted(std::wstring message) = 0;
	virtual void OnEnumerationStopped(std::wstring message) = 0;

	virtual void OnDeviceAdded(std::wstring id, std::wstring name) = 0;
	virtual void OnDeviceRemoved(std::wstring message) = 0;
	virtual void OnDeviceUnpaired(std::wstring message) = 0;
	virtual void OnDevicePaired(std::wstring message) = 0;
    virtual void OnDevicePairedError(std::wstring message, int errorCode) = 0;

    virtual void OnAsyncException(std::wstring message) = 0;

    virtual void LogMessage(std::wstring message) = 0;
};

/// Helper interface to handle user input
class IWlanHostedNetworkPrompt
{
public:
    virtual ~IWlanHostedNetworkPrompt() {};

    virtual bool AcceptIncommingConnection() = 0;
};

/// Helper interface to handle user input
class IWlanHostedNetworkDevicePairRequest
{
public:
	virtual ~IWlanHostedNetworkDevicePairRequest() {};

	virtual bool PairRequest(ABI::Windows::Devices::Enumeration::DevicePairingKinds kinds, std::wstring& strPin) = 0;
};

/// Wraps code to call into the WiFiDirect WinRT APIs as a replacement for the WlanHostedNetwork* functions
/// https://msdn.microsoft.com/en-us/library/windows.devices.wifidirect.aspx
class WlanHostedNetworkHelper
{
public:
    WlanHostedNetworkHelper();
    ~WlanHostedNetworkHelper();

    /// Set SSID (optional, falls back to a Wi-FI Direct default SSID which begins with "DIRECT-")
    void SetSSID(const std::wstring& ssid)
    {
        _ssid = ssid;
        _ssidProvided = true;
    }

    std::wstring GetSSID() const
    {
        return _ssid;
    }

    /// Set Passphrase (optional, falls back to a random string)
    void SetPassphrase(const std::wstring& passphrase)
    {
        _passphraseProvided = true;
        _passphrase = passphrase;
    }

    std::wstring GetPassphrase() const
    {
        return _passphrase;
    }

    /// Register listener to receive updates (only one listener is supported)
    void RegisterListener(IWlanHostedNetworkListener* listener)
    {
        _listener = listener;
    }

    /// Register user prompt to get user input
    void RegisterPrompt(IWlanHostedNetworkPrompt* prompt)
    {
        _prompt = prompt;
    }

	/// Register user prompt to get user input
	void RegisterPairRequest(IWlanHostedNetworkDevicePairRequest* request)
	{
		_PairRequest = request;
	}

    /// Change behavior to auto-accept or ask user
    void SetAutoAccept(bool autoAccept)
    {
        _autoAccept = autoAccept;
    }

    /// Start advertising
    void Start();

    /// Stop advertising
    void Stop();

	/// Scan network
	void Scan();

	/// Connect device
	void ConnectDevice(const wchar_t* szDeviceId);
	void Disconnect(const wchar_t* szDeviceId);
	void Pair(const wchar_t* szDeviceId);
	void Unpair(const wchar_t* szDeviceId);

private:
    /// Start connection listener
    void StartListener();

    /// Clear out old state
    void Reset();

	/// Connect device
	void ConnectDeviceInternal(HSTRING deviceId);
    void PairDeviceInternal(const wchar_t* szDeviceId, ABI::Windows::Devices::Enumeration::IDeviceInformation2* pDevInfo2);

    // WinRT helpers

    /// Main class that is used to start advertisement
    Microsoft::WRL::ComPtr<ABI::Windows::Devices::WiFiDirect::IWiFiDirectAdvertisementPublisher> _publisher;
    /// Advertisement Settings within the publisher
    Microsoft::WRL::ComPtr<ABI::Windows::Devices::WiFiDirect::IWiFiDirectAdvertisement> _advertisement;
    /// Legacy settings within the advertisement settings
    Microsoft::WRL::ComPtr<ABI::Windows::Devices::WiFiDirect::IWiFiDirectLegacySettings> _legacySettings;
    /// Listen for incoming connections
    Microsoft::WRL::ComPtr<ABI::Windows::Devices::WiFiDirect::IWiFiDirectConnectionListener> _connectionListener;

    /// Keep references to all connected peers
    std::map<std::wstring, Microsoft::WRL::ComPtr<ABI::Windows::Devices::WiFiDirect::IWiFiDirectDevice>> _connectedDevices;
    std::map<std::wstring, EventRegistrationToken> _connectedDeviceStatusChangedTokens;

	Microsoft::WRL::ComPtr <ABI::Windows::Devices::Enumeration::IDeviceWatcher> _deviceWatcher;
	std::map<std::wstring, Microsoft::WRL::ComPtr<ABI::Windows::Devices::Enumeration::IDeviceInformation2>> _discoverDevices;

    /// Used to un-register for events
    EventRegistrationToken _connectionRequestedToken;
    EventRegistrationToken _statusChangedToken;

	EventRegistrationToken _DeviceUpdatedToken;
	EventRegistrationToken _DeviceAddToken;
	EventRegistrationToken _DeviceRemoveToken;
	EventRegistrationToken _EnumerationStopToken;
	EventRegistrationToken _EnumerationCompletedToken;

	EventRegistrationToken _DevicePairToken;

    // Settings for "soft AP"

    bool _ssidProvided;
    std::wstring _ssid;
    bool _passphraseProvided;
    std::wstring _passphrase;

    // Listeners that can be notified of changes to the "soft AP"

    IWlanHostedNetworkListener* _listener;

    IWlanHostedNetworkPrompt* _prompt;

	IWlanHostedNetworkDevicePairRequest* _PairRequest;

    /// tracks whether we should accept incoming connections or ask the user
    bool _autoAccept;
};