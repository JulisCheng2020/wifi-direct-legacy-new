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

#include "stdafx.h"
#include "WlanHostedNetworkWinRT.h"
#include <vector>
#include <string>

using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::Devices::Enumeration;
using namespace ABI::Windows::Devices::WiFiDirect;
using namespace ABI::Windows::Security::Credentials;
using namespace ABI::Windows::Networking;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;


typedef __FITypedEventHandler_2_Windows__CDevices__CWiFiDirect__CWiFiDirectConnectionListener_Windows__CDevices__CWiFiDirect__CWiFiDirectConnectionRequestedEventArgs ConnectionRequestedHandler;
typedef __FITypedEventHandler_2_Windows__CDevices__CWiFiDirect__CWiFiDirectAdvertisementPublisher_Windows__CDevices__CWiFiDirect__CWiFiDirectAdvertisementPublisherStatusChangedEventArgs StatusChangedHandler;
typedef __FITypedEventHandler_2_Windows__CDevices__CWiFiDirect__CWiFiDirectDevice_IInspectable ConnectionStatusChangedHandler;

typedef __FIAsyncOperationCompletedHandler_1_Windows__CDevices__CWiFiDirect__CWiFiDirectDevice FromIdAsyncHandler;
typedef __FIAsyncOperationCompletedHandler_1_Windows__CDevices__CEnumeration__CDeviceUnpairingResult UnpairAsyncHandler;
typedef __FIAsyncOperationCompletedHandler_1_Windows__CDevices__CEnumeration__CDevicePairingResult PairAsyncHandler;

typedef __FITypedEventHandler_2_Windows__CDevices__CEnumeration__CDeviceWatcher_Windows__CDevices__CEnumeration__CDeviceInformation DeviceAddHandler;
typedef __FITypedEventHandler_2_Windows__CDevices__CEnumeration__CDeviceWatcher_Windows__CDevices__CEnumeration__CDeviceInformationUpdate DeviceRemovedHandler;
typedef __FITypedEventHandler_2_Windows__CDevices__CEnumeration__CDeviceWatcher_IInspectable EnumerationNotifyHandler;
typedef __FITypedEventHandler_2_Windows__CDevices__CEnumeration__CDeviceInformationCustomPairing_Windows__CDevices__CEnumeration__CDevicePairingRequestedEventArgs CustomPairHandler;

typedef __FIVectorView_1_Windows__CNetworking__CEndpointPair EndpointPairCollection;

WlanHostedNetworkHelper::WlanHostedNetworkHelper()
    : _ssidProvided(false),
      _passphraseProvided(false),
      _listener(nullptr),
      _autoAccept(true)
{
}

WlanHostedNetworkHelper::~WlanHostedNetworkHelper()
{
    if (_publisher.Get() != nullptr)
    {
        _publisher->Stop();
    }
    Reset();
}

void WlanHostedNetworkHelper::Start()
{
    HRESULT hr = S_OK;

    // Clean up old state
    Reset();

    // Create WiFiDirectAdvertisementPublisher
    hr = Windows::Foundation::ActivateInstance(HStringReference(RuntimeClass_Windows_Devices_WiFiDirect_WiFiDirectAdvertisementPublisher).Get(), &_publisher);
    if (FAILED(hr))
    {
        throw WlanHostedNetworkException("ActivateInstance for WiFiDirectAdvertisementPublisher failed", hr);
    }

    // Add event handler for advertisement StatusChanged
    hr = _publisher->add_StatusChanged(
        Callback<StatusChangedHandler>([this](IWiFiDirectAdvertisementPublisher* sender, IWiFiDirectAdvertisementPublisherStatusChangedEventArgs* args) -> HRESULT
    {
        HRESULT hr = S_OK;
        WiFiDirectAdvertisementPublisherStatus status;
        WiFiDirectError error;

        try
        {
            hr = args->get_Status(&status);
            if (FAILED(hr))
            {
                throw WlanHostedNetworkException("Get Status for AdvertisementPubliserStatusChangedEventArgs failed", hr);
            }

            switch (status)
            {
                case WiFiDirectAdvertisementPublisherStatus_Started:
                {
                    // Begin listening for connections and notify listener that the advertisement started
                    StartListener();

                    if (_listener != nullptr)
                    {
                        _listener->OnAdvertisementStarted();
                    }
                    break;
                }
                case WiFiDirectAdvertisementPublisherStatus_Aborted:
                {
                    // Check error and notify listener that the advertisement stopped
                    hr = args->get_Error(&error);
                    if (FAILED(hr))
                    {
                        throw WlanHostedNetworkException("Get Error for AdvertisementPubliserStatusChangedEventArgs failed", hr);
                    }

                    if (_listener != nullptr)
                    {
                        std::wstring message;

                        switch (error)
                        {
                        case WiFiDirectError_RadioNotAvailable:
                            message = L"Advertisement aborted, Wi-Fi radio is turned off";
                            break;

                        case WiFiDirectError_ResourceInUse:
                            message = L"Advertisement aborted, Resource In Use";
                            break;

                        default:
                            message = L"Advertisement aborted, unknown reason";
                            break;
                        }

                        _listener->OnAdvertisementAborted(message);
                    }
                    break;
                }
                case WiFiDirectAdvertisementPublisherStatus_Stopped:
                {
                    // Notify listener that the advertisement is stopped
                    if (_listener != nullptr)
                    {
                        _listener->OnAdvertisementStopped(L"Advertisement stopped");
                    }
                    break;
                }
            }
        }
        catch (WlanHostedNetworkException& e)
        {
            if (_listener != nullptr)
            {
                std::wostringstream ss;
                ss << e.what() << ": " << e.GetErrorCode();
                _listener->OnAsyncException(ss.str());
            }
            return e.GetErrorCode();
        }

        return hr;
    }).Get(), &_statusChangedToken);

    // Set Advertisement required settings

    hr = _publisher->get_Advertisement(_advertisement.GetAddressOf());
    if (FAILED(hr))
    {
        throw WlanHostedNetworkException("Get advertisement for WiFiDirectAdvertisementPublisher failed", hr);
    }

    // Must set the autonomous group owner (GO) enabled flag
    // Legacy Wi-Fi Direct advertisement uses a Wi-Fi Direct GO to act as an access point to legacy settings
    hr = _advertisement->put_IsAutonomousGroupOwnerEnabled(false);
    if (FAILED(hr))
    {
        throw WlanHostedNetworkException("Set is autonomous group owner for WiFiDirectAdvertisement failed", hr);
    }

	hr = _advertisement->put_ListenStateDiscoverability(ABI::Windows::Devices::WiFiDirect::WiFiDirectAdvertisementListenStateDiscoverability_Normal);
	if (FAILED(hr))
	{
		throw WlanHostedNetworkException("Set put_ListenStateDiscoverability failed", hr);
	}

    //hr = _advertisement->get_LegacySettings(_legacySettings.GetAddressOf());
    //if (FAILED(hr))
    //{
    //    throw WlanHostedNetworkException("Get legacy settings for WiFiDirectAdvertisement failed", hr);
    //}

    // Must enable legacy settings so that non-Wi-Fi Direct peers can connect in legacy mode
    //hr = _legacySettings->put_IsEnabled(true);
    //if (FAILED(hr))
    //{
    //    throw WlanHostedNetworkException("Set is enabled for WiFiDirectLegacySettings failed", hr);
    //}

#if 0
    HString hstrSSID;
    HString hstrPassphrase;

    // Either specify an SSID, or read the randomly generated one
    if (_ssidProvided)
    {
        hr = hstrSSID.Set(_ssid.c_str());
        if (FAILED(hr))
        {
            throw WlanHostedNetworkException("Failed to create HSTRING representation for SSID", hr);
        }

        hr = _legacySettings->put_Ssid(hstrSSID.Get());
        if (FAILED(hr))
        {
            throw WlanHostedNetworkException("Set SSID for WiFiDirectLegacySettings failed", hr);
        }
    }
    else
    {
        hr = _legacySettings->get_Ssid(hstrSSID.GetAddressOf());
        if (FAILED(hr))
        {
            throw WlanHostedNetworkException("Get SSID for WiFiDirectLegacySettings failed", hr);
        }

        _ssid = hstrSSID.GetRawBuffer(nullptr);
    }

    // Either specify a passphrase, or read the randomly generated one
    ComPtr<IPasswordCredential> passwordCredential;

    hr = _legacySettings->get_Passphrase(passwordCredential.GetAddressOf());
    if (FAILED(hr))
    {
        throw WlanHostedNetworkException("Get Passphrase for WiFiDirectLegacySettings failed", hr);
    }

    if (_passphraseProvided)
    {
        hr = hstrPassphrase.Set(_passphrase.c_str());
        if (FAILED(hr))
        {
            throw WlanHostedNetworkException("Failed to create HSTRING representation for Passphrase", hr);
        }

        hr = passwordCredential->put_Password(hstrPassphrase.Get());
        if (FAILED(hr))
        {
            throw WlanHostedNetworkException("Set Passphrase for WiFiDirectLegacySettings failed", hr);
        }
    }
    else
    {
        hr = passwordCredential->get_Password(hstrPassphrase.GetAddressOf());
        if (FAILED(hr))
        {
            throw WlanHostedNetworkException("Get Passphrase for WiFiDirectLegacySettings failed", hr);
        }

        _passphrase = hstrPassphrase.GetRawBuffer(nullptr);
    }
#endif

    // Start the advertisement, which will create an access point that other peers can connect to
    hr = _publisher->Start();
    if (FAILED(hr))
    {
        throw WlanHostedNetworkException("Start WiFiDirectAdvertisementPublisher failed", hr);
    }
}

void WlanHostedNetworkHelper::Stop()
{
    HRESULT hr = S_OK;

    // Call stop on the publisher and expect the status changed callback
    if (_publisher.Get() != nullptr)
    {
        hr = _publisher->Stop();
        if (FAILED(hr))
        {
            throw WlanHostedNetworkException("Stop WiFiDirectAdvertisementPublisher failed", hr);
        }
    }
    else
    {
        throw WlanHostedNetworkException("WiFiDirectAdvertisementPublisher is not running", hr);
    }
}

void WlanHostedNetworkHelper::ConnectDevice(const wchar_t* szDeviceId)
{
	HString deviceId;
	deviceId.Set(szDeviceId);

	ConnectDeviceInternal(deviceId.Get());
}

void WlanHostedNetworkHelper::Disconnect(const wchar_t* szDeviceId)
{
	auto itDevice = _connectedDevices.find(szDeviceId);
	auto itToken = _connectedDeviceStatusChangedTokens.find(szDeviceId);

	if (itToken != _connectedDeviceStatusChangedTokens.end())
	{
		if (itDevice != _connectedDevices.end())
		{
			itDevice->second->remove_ConnectionStatusChanged(itToken->second);
		}
		_connectedDeviceStatusChangedTokens.erase(itToken);
	}

	if (itDevice != _connectedDevices.end())
	{
		ComPtr<IClosable> spInterface;
		HRESULT hr = itDevice->second.As(&spInterface);
		if (SUCCEEDED(hr))
		{
			spInterface->Close();
		}

		_connectedDevices.erase(itDevice);
	}
}

void WlanHostedNetworkHelper::PairDeviceInternal(const wchar_t* szDeviceId, ABI::Windows::Devices::Enumeration::IDeviceInformation2* pDevInfo2)
{
	ComPtr<IDeviceInformationPairing> devInfoPair;
	HRESULT hr = pDevInfo2->get_Pairing(&devInfoPair);
	if (SUCCEEDED(hr))
	{
		boolean bCanPair = false;
		boolean isPaired = false;
		hr = devInfoPair->get_IsPaired(&isPaired);
		hr = devInfoPair->get_CanPair(&bCanPair);
		if (!isPaired)
		{
			ComPtr<IDeviceInformationPairing2> devInfoPair2;
			hr = devInfoPair.As(&devInfoPair2);
			if (SUCCEEDED(hr))
			{
				ComPtr<IDeviceInformationCustomPairing> spCustomPairing;

				hr = devInfoPair2->get_Custom(spCustomPairing.GetAddressOf());
				if (FAILED(hr))
				{
					throw WlanHostedNetworkException("Get IDeviceInformationCustomPairing failed", hr);
				}

				ComPtr<IWiFiDirectConnectionParameters> param;
				hr = Windows::Foundation::ActivateInstance(HStringReference(RuntimeClass_Windows_Devices_WiFiDirect_WiFiDirectConnectionParameters).Get(), &param);
				if (FAILED(hr))
				{
					throw WlanHostedNetworkException("ActivateInstance IWiFiDirectConnectionParameters failed", hr);
				}

				hr = param->put_GroupOwnerIntent(15);

				DevicePairingKinds devicePairingKinds = DevicePairingKinds::DevicePairingKinds_ConfirmOnly |
					DevicePairingKinds::DevicePairingKinds_DisplayPin/* |
					DevicePairingKinds::DevicePairingKinds_ProvidePin*/;

				ComPtr<IWiFiDirectConnectionParameters2> spConParam2;
				hr = param.As(&spConParam2);
				if (FAILED(hr))
				{
					throw WlanHostedNetworkException("Get IDevicePairingSettings failed", hr);
				}

				WiFiDirectPairingProcedure proc = WiFiDirectPairingProcedure::WiFiDirectPairingProcedure_GroupOwnerNegotiation;
				spConParam2->put_PreferredPairingProcedure(proc);

				ComPtr<IDevicePairingSettings> spSetting;
				hr = param.As(&spSetting);
				if (FAILED(hr))
				{
					throw WlanHostedNetworkException("Get IDevicePairingSettings failed", hr);
				}

				spCustomPairing->add_PairingRequested(Callback<CustomPairHandler>([this](IDeviceInformationCustomPairing* pCustomPairing, IDevicePairingRequestedEventArgs* pArgs) -> HRESULT
					{
						OutputDebugString(L"pair requested.\n");

						HString pin;
						ABI::Windows::Devices::Enumeration::DevicePairingKinds kinds;
						pArgs->get_PairingKind(&kinds);
						if (kinds == ABI::Windows::Devices::Enumeration::DevicePairingKinds::DevicePairingKinds_DisplayPin)
						{
							pArgs->get_Pin(pin.GetAddressOf());
						}

						if (_PairRequest)
						{
							std::wstring strPin = pin.GetRawBuffer(NULL);

							bool bAllowed = _PairRequest->PairRequest(kinds, strPin);
							if (bAllowed)
							{
								if (kinds & ABI::Windows::Devices::Enumeration::DevicePairingKinds::DevicePairingKinds_ConfirmOnly |
									kinds & ABI::Windows::Devices::Enumeration::DevicePairingKinds::DevicePairingKinds_DisplayPin)
								{
									pArgs->Accept();
								}
								else if (kinds & ABI::Windows::Devices::Enumeration::DevicePairingKinds::DevicePairingKinds_ProvidePin)
								{
									pin.Set(strPin.c_str());
									pArgs->AcceptWithPin(pin.Get());
								}
							}
						}

						return S_OK;
					}).Get(), &_DevicePairToken);

				ComPtr<IAsyncOperation<ABI::Windows::Devices::Enumeration::DevicePairingResult*>> asyncAction;
				hr = spCustomPairing->PairWithProtectionLevelAndSettingsAsync(devicePairingKinds, DevicePairingProtectionLevel::DevicePairingProtectionLevel_Default,
					spSetting.Get(), &asyncAction);
				if (SUCCEEDED(hr))
				{
					IDeviceInformation2* pDevInfo = pDevInfo2;
					asyncAction->put_Completed(Callback<PairAsyncHandler>([this, pDevInfo](IAsyncOperation<DevicePairingResult*>* pHandler, AsyncStatus status) -> HRESULT
						{
							if (status == AsyncStatus::Completed)
							{
								HString id;
								ABI::Windows::Devices::Enumeration::DevicePairingResultStatus pairStatus;
								ComPtr<IDevicePairingResult> spResult;
								ComPtr<IDeviceInformation> devInfoInfo;

								HRESULT hr = pHandler->GetResults(spResult.GetAddressOf());
								if (SUCCEEDED(hr))
								{
									hr = spResult->get_Status(&pairStatus);
								}

								pDevInfo->QueryInterface(IID_PPV_ARGS(devInfoInfo.GetAddressOf()));
								if (devInfoInfo)
								{
									devInfoInfo->get_Id(id.GetAddressOf());
								}

								if (pairStatus == ABI::Windows::Devices::Enumeration::DevicePairingResultStatus::DevicePairingResultStatus_Paired)
								{
									if (_listener != nullptr)
										_listener->OnDevicePaired(id.GetRawBuffer(NULL));
								}
								else
								{
									if (_listener != nullptr) _listener->OnDevicePairedError(id.GetRawBuffer(NULL), pairStatus);
								}
							}
							else
							{
								if (_listener != nullptr)
								{
									switch (status)
									{
									case AsyncStatus::Started:
										_listener->LogMessage(L"Device pair, status=Started");
										break;
									case AsyncStatus::Canceled:
										_listener->LogMessage(L"Device pair, status=Canceled");
										break;
									case AsyncStatus::Error:
										_listener->LogMessage(L"Device pair, status=Error");
										break;
									}
								}
							}

							return S_OK;
						}).Get());
				}
			}

		}
	}
}

void WlanHostedNetworkHelper::Pair(const wchar_t* szDeviceId)
{
	auto itDeviceInfo = _discoverDevices.find(szDeviceId);

	if (itDeviceInfo != _discoverDevices.end())
	{
		this->PairDeviceInternal(szDeviceId, itDeviceInfo->second.Get());
	}
}

void WlanHostedNetworkHelper::Unpair(const wchar_t* szDeviceId)
{
	auto itDevice = _connectedDevices.find(szDeviceId);
	auto itToken = _connectedDeviceStatusChangedTokens.find(szDeviceId);

	if (itToken != _connectedDeviceStatusChangedTokens.end())
	{
		if (itDevice != _connectedDevices.end())
		{
			itDevice->second->remove_ConnectionStatusChanged(itToken->second);
		}
		_connectedDeviceStatusChangedTokens.erase(itToken);
	}

	if (itDevice != _connectedDevices.end())
	{
		//ComPtr<IClosable> spInterface;
		//HRESULT hr = itDevice->second.As(&spInterface);
		//if (SUCCEEDED(hr))
		//{
		//	spInterface->Close();
		//}

		_connectedDevices.erase(itDevice);
	}

	auto itDeviceInfo = _discoverDevices.find(szDeviceId);

	if (itDeviceInfo != _discoverDevices.end())
	{
		ComPtr<IDeviceInformationPairing> devInfoPair;
		HRESULT hr = itDeviceInfo->second->get_Pairing(&devInfoPair);
		if (SUCCEEDED(hr))
		{
			ComPtr<IDeviceInformationPairing2> devInfoPair2;
			devInfoPair.As(&devInfoPair2);
			if (devInfoPair2)
			{
				ComPtr<IAsyncOperation<DeviceUnpairingResult*>> asyncUnpairAction;

				hr = devInfoPair2->UnpairAsync(&asyncUnpairAction);
				if (SUCCEEDED(hr))
				{
					HString devId;
					devId.Set(szDeviceId);
					HSTRING* id = devId.GetAddressOf();

					asyncUnpairAction->put_Completed(Callback<UnpairAsyncHandler>([this](IAsyncOperation<DeviceUnpairingResult*>* pHandler, AsyncStatus status) -> HRESULT
					{
						if (status == AsyncStatus::Completed)
						{
							if (_listener != nullptr)
								_listener->OnDeviceUnpaired(L"Device Unpair successfully");
						}
						else
						{
							if (_listener != nullptr)
							{
								switch (status)
								{
								case AsyncStatus::Started:
									_listener->LogMessage(L"Device Unpair, status=Started");
									break;
								case AsyncStatus::Canceled:
									_listener->LogMessage(L"Device Unpair, status=Canceled");
									break;
								case AsyncStatus::Error:
									_listener->LogMessage(L"Device Unpair, status=Error");
									break;
								}
							}
						}

						return S_OK;
					}).Get());
				}
			}
		}
	}
}

void WlanHostedNetworkHelper::ConnectDeviceInternal(HSTRING targetDeviceId)
{
	HRESULT hr = S_OK;
	ComPtr<IWiFiDirectDeviceStatics2> wfdStatics;

	hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_Devices_WiFiDirect_WiFiDirectDevice).Get(), &wfdStatics);
	if (FAILED(hr))
	{
		throw WlanHostedNetworkException("GetActivationFactory for WiFiDirectDevice failed", hr);
	}

	HString devId;
	devId.Set(targetDeviceId);

	auto itDeviceInfo = _discoverDevices.find(devId.GetRawBuffer(NULL));

	if (itDeviceInfo != _discoverDevices.end())
	{
		ComPtr<IDeviceInformationPairing> devInfoPair;
		hr = itDeviceInfo->second->get_Pairing(&devInfoPair);
		if (SUCCEEDED(hr))
		{
			boolean bCanPair = false;
			boolean isPaired = false;
			hr = devInfoPair->get_IsPaired(&isPaired);
			hr = devInfoPair->get_CanPair(&bCanPair);
			if (isPaired)
			{
				return;
			}			
		}
	}

	ComPtr<IWiFiDirectConnectionParameters> param;
	hr = Windows::Foundation::ActivateInstance(HStringReference(RuntimeClass_Windows_Devices_WiFiDirect_WiFiDirectConnectionParameters).Get(), &param);
	if (FAILED(hr))
	{
		throw WlanHostedNetworkException("ActivateInstance IWiFiDirectConnectionParameters failed", hr);
	}

	hr = param->put_GroupOwnerIntent(15);

	//ComPtr<IWiFiDirectConnectionParameters2> spConParam2;
	//hr = param.As(&spConParam2);
	//if (FAILED(hr))
	//{
	//	throw WlanHostedNetworkException("Get IDevicePairingSettings failed", hr);
	//}

	//WiFiDirectPairingProcedure proc = WiFiDirectPairingProcedure::WiFiDirectPairingProcedure_GroupOwnerNegotiation;
	//spConParam2->put_PreferredPairingProcedure(proc);

	ComPtr<IAsyncOperation<WiFiDirectDevice*>> asyncAction;
	hr = wfdStatics->FromIdAsync(targetDeviceId, param.Get(), &asyncAction);
	if (FAILED(hr))
	{
		throw WlanHostedNetworkException("From ID Async for WiFiDirectDevice failed", hr);
	}

	hr = asyncAction->put_Completed(Callback<FromIdAsyncHandler>([this](IAsyncOperation<WiFiDirectDevice*>* pHandler, AsyncStatus status) -> HRESULT
	{
		HRESULT hr = S_OK;
		ComPtr<IWiFiDirectDevice> wfdDevice;
		ComPtr<EndpointPairCollection> endpointPairs;
		ComPtr<IEndpointPair> endpointPair;
		ComPtr<IHostName> remoteHostName;
		HString remoteHostNameDisplay;
		HString deviceId;

		try
		{
			if (status == AsyncStatus::Completed)
			{
				// Get the WiFiDirectDevice object
				hr = pHandler->GetResults(wfdDevice.GetAddressOf());
				if (FAILED(hr))
				{
					throw WlanHostedNetworkException("Put Completed for FromIDAsync operation failed", hr);
				}

				// Now retrieve the endpoint pairs, which includes the IP address assigned to the peer
				hr = wfdDevice->GetConnectionEndpointPairs(endpointPairs.GetAddressOf());
				if (FAILED(hr))
				{
					throw WlanHostedNetworkException("Get EndpointPairs for WiFiDirectDevice failed", hr);
				}

				hr = endpointPairs->GetAt(0, endpointPair.GetAddressOf());
				if (FAILED(hr))
				{
					throw WlanHostedNetworkException("Get first EndpointPair in collection failed", hr);
				}

				hr = endpointPair->get_RemoteHostName(remoteHostName.GetAddressOf());
				if (FAILED(hr))
				{
					throw WlanHostedNetworkException("Get Remote HostName for EndpointPair failed", hr);
				}

				hr = remoteHostName->get_DisplayName(remoteHostNameDisplay.GetAddressOf());
				if (FAILED(hr))
				{
					throw WlanHostedNetworkException("Get Display Name for Remote HostName failed", hr);
				}

				// Add handler for connection status changed
				EventRegistrationToken statusChangedToken;
				hr = wfdDevice->add_ConnectionStatusChanged(Callback<ConnectionStatusChangedHandler>([this](IWiFiDirectDevice* sender, IInspectable*) -> HRESULT
				{
					WiFiDirectConnectionStatus status;
					HString deviceId;
					HRESULT hr = S_OK;

					try
					{
						hr = sender->get_ConnectionStatus(&status);
						if (FAILED(hr))
						{
							throw WlanHostedNetworkException("Get connection status for peer failed", hr);
						}

						switch (status)
						{
						case WiFiDirectConnectionStatus_Connected:
							// NO-OP
							break;
						case WiFiDirectConnectionStatus_Disconnected:
							// Clean-up state
							hr = sender->get_DeviceId(deviceId.GetAddressOf());
							if (FAILED(hr))
							{
								throw WlanHostedNetworkException("Get Device ID failed", hr);
							}

							auto itDevice = _connectedDevices.find(deviceId.GetRawBuffer(nullptr));
							auto itToken = _connectedDeviceStatusChangedTokens.find(deviceId.GetRawBuffer(nullptr));

							if (itToken != _connectedDeviceStatusChangedTokens.end())
							{
								if (itDevice != _connectedDevices.end())
								{
									itDevice->second->remove_ConnectionStatusChanged(itToken->second);
								}
								_connectedDeviceStatusChangedTokens.erase(itToken);
							}

							if (itDevice != _connectedDevices.end())
							{
								_connectedDevices.erase(itDevice);
							}

							// Notify listener of disconnect
							if (_listener != nullptr)
							{
								_listener->OnDeviceDisconnected(deviceId.GetRawBuffer(nullptr));
							}

							break;
						}
					}
					catch (WlanHostedNetworkException& e)
					{
						if (_listener != nullptr)
						{
							std::wostringstream ss;
							ss << e.what() << ": " << e.GetErrorCode();
							_listener->OnAsyncException(ss.str());
						}
						return e.GetErrorCode();
					}

					return hr;
				}).Get(), &statusChangedToken);

				// Store the connected peer
				hr = wfdDevice->get_DeviceId(deviceId.GetAddressOf());
				if (FAILED(hr))
				{
					throw WlanHostedNetworkException("Get Device ID failed", hr);
				}

				_connectedDevices.insert(std::make_pair(deviceId.GetRawBuffer(nullptr), wfdDevice));
				_connectedDeviceStatusChangedTokens.insert(std::make_pair(deviceId.GetRawBuffer(nullptr), statusChangedToken));

				// Notify Listener
				if (_listener != nullptr)
				{
					_listener->OnDeviceConnected(remoteHostNameDisplay.GetRawBuffer(nullptr));
				}
			}
			else
			{
				if (_listener != nullptr)
				{
					switch (status)
					{
					case AsyncStatus::Started:
						_listener->LogMessage(L"Device connected, status=Started");
						break;
					case AsyncStatus::Canceled:
						_listener->LogMessage(L"Device connected, status=Canceled");
						break;
					case AsyncStatus::Error:
						_listener->LogMessage(L"Device connected, status=Error");
						break;
					}
				}
			}
		}
		catch (WlanHostedNetworkException& e)
		{
			if (_listener != nullptr)
			{
				std::wostringstream ss;
				ss << e.what() << ": " << e.GetErrorCode();
				_listener->OnAsyncException(ss.str());
			}
			return e.GetErrorCode();
		}

		return hr;
	}).Get());
	if (FAILED(hr))
	{
		throw WlanHostedNetworkException("Put Completed for FromIDAsync operation failed", hr);
	}
}

void WlanHostedNetworkHelper::StartListener()
{
    HRESULT hr = S_OK;

    // Create WiFiDirectConnectionListener
    hr = Windows::Foundation::ActivateInstance(HStringReference(RuntimeClass_Windows_Devices_WiFiDirect_WiFiDirectConnectionListener).Get(), &_connectionListener);
    if (FAILED(hr))
    {
        throw WlanHostedNetworkException("ActivateInstance for WiFiDirectConnectionListener failed", hr);
    }

    hr = _connectionListener->add_ConnectionRequested(
        Callback<ConnectionRequestedHandler>([this](IWiFiDirectConnectionListener* sender, IWiFiDirectConnectionRequestedEventArgs* args) -> HRESULT
    {
        HRESULT hr = S_OK;
        ComPtr<IWiFiDirectConnectionRequest> request;

        if (_listener != nullptr)
        {
            _listener->LogMessage(L"Connection Requested...");
        }

        bool acceptConnection = true;
        if (!_autoAccept && _prompt != nullptr)
        {
            acceptConnection = _prompt->AcceptIncommingConnection();
        }

        try
        {
            hr = args->GetConnectionRequest(request.GetAddressOf());
            if (FAILED(hr))
            {
                throw WlanHostedNetworkException("Get connection request for ConnectionRequestedEventArgs failed", hr);
            }
            
            if (acceptConnection)
            {
				HString deviceId;
				ComPtr<IDeviceInformation> deviceInformation;

                hr = request->get_DeviceInformation(deviceInformation.GetAddressOf());
                if (FAILED(hr))
                {
                    throw WlanHostedNetworkException("Get device information for ConnectionRequest failed", hr);
                }

				hr = deviceInformation->get_Id(deviceId.GetAddressOf());
				if (FAILED(hr))
				{
					throw WlanHostedNetworkException("Get ID for DeviceInformation failed", hr);
				}

#if 0 
				this->ConnectDeviceInternal(deviceId.Get());
#else
				ComPtr<IDeviceInformation2> info2;
				HRESULT hr = deviceInformation->QueryInterface(IID_PPV_ARGS(&info2));
				if (FAILED(hr))
				{
					throw WlanHostedNetworkException("Get DeviceInformation2 failed", hr);
				}

				this->PairDeviceInternal(deviceId.GetRawBuffer(NULL), info2.Get());
#endif
            }
            else
            {
                if (_listener != nullptr)
                {
                    _listener->LogMessage(L"Declined");
                }
            }
        }
        catch (WlanHostedNetworkException& e)
        {
            if (_listener != nullptr)
            {
                std::wostringstream ss;
                ss << e.what() << ": " << e.GetErrorCode();
                _listener->OnAsyncException(ss.str());
            }
            return e.GetErrorCode();
        }

        return hr;
    }).Get(), &_connectionRequestedToken);
    if (FAILED(hr))
    {
        throw WlanHostedNetworkException("Add ConnectionRequested handler for WiFiDirectConnectionListener failed", hr);
    }

    if (_listener != nullptr)
    {
        _listener->LogMessage(L"Connection Listener is ready");
    }
}

void WlanHostedNetworkHelper::Reset()
{
	if (_deviceWatcher)
	{
		_deviceWatcher->remove_Added(_DeviceAddToken);
		_deviceWatcher->remove_Removed(_DeviceRemoveToken);
		_deviceWatcher->remove_Updated(_DeviceUpdatedToken);
		_deviceWatcher->remove_Stopped(_EnumerationStopToken);
		_deviceWatcher->remove_EnumerationCompleted(_EnumerationCompletedToken);
	}

    if (_connectionListener.Get() != nullptr)
    {
        _connectionListener->remove_ConnectionRequested(_connectionRequestedToken);
    }

    if (_publisher.Get() != nullptr)
    {
        _publisher->remove_StatusChanged(_statusChangedToken);
    }

	_deviceWatcher.Reset();
    _legacySettings.Reset();
    _advertisement.Reset();
    _publisher.Reset();
    _connectionListener.Reset();

    _connectedDevices.clear();
	_discoverDevices.clear();
}

void WlanHostedNetworkHelper::Scan()
{
	try
	{
		HRESULT hr = S_OK;
		if (_deviceWatcher.Get() == nullptr)
		{
			ComPtr<IDeviceInformationStatics> deviceInfoStatics;
			ComPtr<IWiFiDirectDeviceStatics2> wfdStatics;
			HString deviceSelector;

			//std::vector<HSTRING*> requestedProperties;
			//std::wstring str(L"System.Devices.WiFiDirect.InformationElements");
			//HString strIE;
			//strIE.Set(str.c_str());

			//requestedProperties.push_back(strIE.GetAddressOf());

			hr = Windows::Foundation::GetActivationFactory(HStringReference(RuntimeClass_Windows_Devices_WiFiDirect_WiFiDirectDevice).Get(), &wfdStatics);
			if (FAILED(hr))
			{
				throw WlanHostedNetworkException("GetActivationFactory for IWiFiDirectDeviceStatics2 failed", hr);
			}

			hr = wfdStatics->GetDeviceSelector(ABI::Windows::Devices::WiFiDirect::WiFiDirectDeviceSelectorType::WiFiDirectDeviceSelectorType_AssociationEndpoint, deviceSelector.GetAddressOf());
			if (FAILED(hr))
			{
				throw WlanHostedNetworkException("GetDeviceSelector for WiFiDirectDevice failed", hr);
			}

			hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_Devices_Enumeration_DeviceInformation).Get(), &deviceInfoStatics);
			if (FAILED(hr))
			{
				throw WlanHostedNetworkException("GetActivationFactory for IDeviceInformation failed", hr);
			}

			hr = deviceInfoStatics->CreateWatcherAqsFilter(deviceSelector.Get(), &_deviceWatcher);
			if (FAILED(hr))
			{
				throw WlanHostedNetworkException("CreateWatcherAqsFilterAndAdditionalProperties failed", hr);
			}

			_deviceWatcher->add_Added(Callback<DeviceAddHandler>([this](IDeviceWatcher* sender, IDeviceInformation* deviceInfo) -> HRESULT
			{
				//Add device
				HString id;
				HString name;

				deviceInfo->get_Id(id.GetAddressOf());
				deviceInfo->get_Name(name.GetAddressOf());

				ComPtr<IDeviceInformation2> info;
				HRESULT hr = deviceInfo->QueryInterface(IID_PPV_ARGS(&info));
				if (SUCCEEDED(hr))
				{
					_discoverDevices.insert(std::make_pair(id.GetRawBuffer(NULL), info));
				}
				else
				{
					OutputDebugStringA("Can't get IDeviceInformation2.\n");
				}

				_listener->OnDeviceAdded(id.GetRawBuffer(NULL), name.GetRawBuffer(NULL));

				return S_OK;
			}).Get(), &_DeviceAddToken);

			_deviceWatcher->add_Removed(Callback<DeviceRemovedHandler>([this](IDeviceWatcher* sender, IDeviceInformationUpdate* deviceInfoUpdate) -> HRESULT
			{
				//remove device
				HString id;
				deviceInfoUpdate->get_Id(id.GetAddressOf());

				auto it = _discoverDevices.find(id.GetRawBuffer(NULL));
				if (it != _discoverDevices.end())
				{
					_discoverDevices.erase(it);
				}

				_listener->OnDeviceRemoved(id.GetRawBuffer(NULL));

				return S_OK;
			}).Get(), &_DeviceRemoveToken);

			_deviceWatcher->add_Updated(Callback<DeviceRemovedHandler>([this](IDeviceWatcher* sender, IDeviceInformationUpdate* deviceInfoUpdate) -> HRESULT
			{
				//Update device
				OutputDebugStringA("device updated.\n");

				return S_OK;
			}).Get(), &_DeviceUpdatedToken);

			_deviceWatcher->add_Stopped(Callback<EnumerationNotifyHandler>([this](IDeviceWatcher* sender, IInspectable* object) -> HRESULT
			{
				//Enumeration stop
				OutputDebugStringA("Enumeration stopped.\n");

				_listener->OnEnumerationStopped(L"");

				return S_OK;
			}).Get(), &_EnumerationStopToken);

			_deviceWatcher->add_EnumerationCompleted(Callback<EnumerationNotifyHandler>([this](IDeviceWatcher* sender, IInspectable* object) -> HRESULT
			{
				//Enumeration completed
				OutputDebugStringA("Enumeration Completed.\n");

				_listener->OnEnumerationCompleted(L"");

				return _deviceWatcher->Stop();
			}).Get(), &_EnumerationCompletedToken);
		}

		_discoverDevices.clear();

		hr = _deviceWatcher->Start();
		if (FAILED(hr))
		{
			throw WlanHostedNetworkException("device watcher start failed", hr);
		}
	}
	catch (WlanHostedNetworkException& e)
	{
		if (_listener != nullptr)
		{
			std::wostringstream ss;
			ss << e.what() << ": " << e.GetErrorCode();
			_listener->OnAsyncException(ss.str());
		}
	}
}
