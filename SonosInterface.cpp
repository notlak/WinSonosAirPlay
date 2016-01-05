#include "stdafx.h"
#include "SonosInterface.h"

#include <UPnP.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "user32.lib")


class CUPnPDeviceFinderCallback : public IUPnPDeviceFinderCallback
{
public:
	CUPnPDeviceFinderCallback() { m_lRefCount = 0; }

	STDMETHODIMP QueryInterface(REFIID iid, LPVOID* ppvObject)
	{
		HRESULT hr = S_OK;

		if (NULL == ppvObject)
		{
			hr = E_POINTER;
		}
		else
		{
			*ppvObject = NULL;
		}

		if (SUCCEEDED(hr))
		{
			if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IUPnPDeviceFinderCallback))
			{
				*ppvObject = static_cast<IUPnPDeviceFinderCallback*>(this);
				AddRef();
			}
			else
			{
				hr = E_NOINTERFACE;
			}
		}

		return hr;
	}

	STDMETHODIMP_(ULONG) AddRef() { return ::InterlockedIncrement(&m_lRefCount); }

	STDMETHODIMP_(ULONG) Release()
	{
		LONG lRefCount = ::InterlockedDecrement(&m_lRefCount);
		if (0 == lRefCount)
		{
			delete this;
		}

		return lRefCount;
	}

	STDMETHODIMP DeviceAdded(LONG lFindData, IUPnPDevice* pDevice)
	{
		HRESULT hr = S_OK;

		//You should save pDevice so that it is accessible outside the scope of this method.

		BSTR UniqueDeviceName;
		hr = pDevice->get_UniqueDeviceName(&UniqueDeviceName);
		if (SUCCEEDED(hr))
		{
			BSTR FriendlyName;
			hr = pDevice->get_FriendlyName(&FriendlyName);
			if (SUCCEEDED(hr))
			{
				TRACE("Device Added: udn: %S, name: %S\n", FriendlyName, UniqueDeviceName);
				::SysFreeString(FriendlyName);
			}
			::SysFreeString(UniqueDeviceName);
		}

		BSTR type;
		hr = pDevice->get_Type(&type);
		::SysFreeString(type);

		return hr;
	}

	STDMETHODIMP DeviceRemoved(LONG lFindData, BSTR bstrUDN)
	{
		TRACE("Device Removed: udn: %S", bstrUDN);
		return S_OK;
	}

	STDMETHODIMP SearchComplete(LONG lFindData) { return S_OK; }

private:
	LONG m_lRefCount;

};

SonosInterface::SonosInterface()
{
	CoInitialize(nullptr);
}


SonosInterface::~SonosInterface()
{
	CoUninitialize();
}


bool SonosInterface::Init()
{
	return false;
}

bool SonosInterface::FindSpeakers()
{
	HRESULT hr = S_OK;

	// first test the synchronous search

	// ### AddRef ???

	IUPnPDeviceFinder* pUPnPDeviceFinder;
	
	hr = CoCreateInstance(CLSID_UPnPDeviceFinder, NULL, CLSCTX_INPROC_SERVER,
		IID_IUPnPDeviceFinder, reinterpret_cast<void**>(&pUPnPDeviceFinder));
	
	if (SUCCEEDED(hr))
	{
		IUPnPDevices* pDevices;
		
		pUPnPDeviceFinder->FindByType(_bstr_t("upnp:rootdevice"), 0, &pDevices);

		// --- how to traverse devices returned ---

		IUnknown * pUnk = NULL;
		hr = pDevices->get__NewEnum(&pUnk);
		if (SUCCEEDED(hr))
		{
			IEnumVARIANT * pEnumVar = NULL;
			hr = pUnk->QueryInterface(IID_IEnumVARIANT, (void **)&pEnumVar);
			if (SUCCEEDED(hr))
			{
				VARIANT varCurDevice;
				VariantInit(&varCurDevice);
				pEnumVar->Reset();
				// Loop through each device in the collection
				while (S_OK == pEnumVar->Next(1, &varCurDevice, NULL))
				{
					IUPnPDevice * pDevice = NULL;
					IDispatch * pdispDevice = V_DISPATCH(&varCurDevice);
					if (SUCCEEDED(pdispDevice->QueryInterface(IID_IUPnPDevice, (void **)&pDevice)))
					{
						// Do something interesting with pDevice
						BSTR bstrName = NULL;
						if (SUCCEEDED(pDevice->get_FriendlyName(&bstrName)))
						{
							TRACE("%S\n", bstrName);
							SysFreeString(bstrName);
						}
					}
					VariantClear(&varCurDevice);
					pDevice->Release();
				}
				pEnumVar->Release();
			}
			pUnk->Release();
		}


		// -----------------------------------------

		pDevices->Release();
		pUPnPDeviceFinder->Release();
	}

	// now the asynchronous

	IUPnPDeviceFinderCallback* pUPnPDeviceFinderCallback = new CUPnPDeviceFinderCallback();

	if (NULL != pUPnPDeviceFinderCallback)
	{
		pUPnPDeviceFinderCallback->AddRef();
		IUPnPDeviceFinder* pUPnPDeviceFinder;
		hr = CoCreateInstance(CLSID_UPnPDeviceFinder, NULL, CLSCTX_INPROC_SERVER,
			IID_IUPnPDeviceFinder, reinterpret_cast<void**>(&pUPnPDeviceFinder));
		if (SUCCEEDED(hr))
		{
			LONG lFindData;
			hr = pUPnPDeviceFinder->CreateAsyncFind(_bstr_t("upnp:rootdevice"), 0, pUPnPDeviceFinderCallback, &lFindData);
			if (SUCCEEDED(hr))
			{
				hr = pUPnPDeviceFinder->StartAsyncFind(lFindData);
				if (SUCCEEDED(hr))
				{
					// STA threads must pump messages
					MSG Message;
					BOOL bGetMessage = FALSE;
					while (bGetMessage = GetMessage(&Message, NULL, 0, 0) && -1 != bGetMessage)
					{
						DispatchMessage(&Message);
					}
				}
				pUPnPDeviceFinder->CancelAsyncFind(lFindData);
			}
			pUPnPDeviceFinder->Release();
		}
		pUPnPDeviceFinderCallback->Release();
	}
	else
	{
		hr = E_OUTOFMEMORY;
	}

	return hr == S_OK;
}