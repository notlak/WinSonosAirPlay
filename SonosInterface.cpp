#include "stdafx.h"
#include "SonosInterface.h"
#include "tinyxml2.h"

#include <winsock2.h>
#include <UPnP.h>

#include <iostream>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "user32.lib")

class CUPnPDeviceFinderCallback : public IUPnPDeviceFinderCallback
{
public:
	CUPnPDeviceFinderCallback(SonosInterface* pSonosInt) : m_lRefCount(0), m_pSonosInt(pSonosInt) {}

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

		std::string devUdn;
		std::string devUrl;

		bool gotUdn = false;
		bool gotUrl = false;

		//You should save pDevice so that it is accessible outside the scope of this method.

		_bstr_t udn;
		hr = pDevice->get_UniqueDeviceName(&udn.GetBSTR());

		if (SUCCEEDED(hr))
		{
			devUdn = udn;

			gotUdn = true;

			BSTR friendlyName;
			hr = pDevice->get_FriendlyName(&friendlyName);

			if (SUCCEEDED(hr))
			{
				TRACE("Device Added: udn: %S, name: %S\n", udn, friendlyName);
				::SysFreeString(friendlyName);
			}
			::SysFreeString(udn.GetBSTR());
		}

		// find the url for device info

		IUPnPDeviceDocumentAccess* idoc = 0;

		// URL will be stored into this variable
		_bstr_t url;

		// query for IUPnPDeviceDocumentAccess
		hr = pDevice->QueryInterface(IID_IUPnPDeviceDocumentAccess, (void**)&idoc);

		if (SUCCEEDED(hr))
		{
			// get URL and write to BSTR
			hr = idoc->GetDocumentURL(&url.GetBSTR());

			if (SUCCEEDED(hr))
			{
				devUrl = url;

				// on finish release resources
				SysFreeString(url.GetBSTR());
			}

			idoc->Release();
		}

		if (gotUdn && gotUrl)
			m_pSonosInt->UpnpDeviceAdded(udn, url);

		return hr;
	}

	STDMETHODIMP DeviceRemoved(LONG lFindData, BSTR bstrUDN)
	{
		TRACE("Device Removed: udn: %S", bstrUDN);

		_bstr_t udn(bstrUDN);

		m_pSonosInt->UpnpDeviceRemoved(udn);

		return S_OK;
	}

	STDMETHODIMP SearchComplete(LONG lFindData) 
	{
		m_pSonosInt->UpnpSearchComplete();
		return S_OK;
	}

private:
	LONG m_lRefCount;
	SonosInterface* m_pSonosInt;
};

SonosInterface::SonosInterface()
{
	CoInitialize(nullptr);

	ParseZoneTopology("need to pass xml");
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
	/*
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

	return true;
	*/
	// now the asynchronous

	IUPnPDeviceFinderCallback* pUPnPDeviceFinderCallback = new CUPnPDeviceFinderCallback(this);

	if (NULL != pUPnPDeviceFinderCallback)
	{
		pUPnPDeviceFinderCallback->AddRef();
		IUPnPDeviceFinder* pUPnPDeviceFinder;
		hr = CoCreateInstance(CLSID_UPnPDeviceFinder, NULL, CLSCTX_INPROC_SERVER,
			IID_IUPnPDeviceFinder, reinterpret_cast<void**>(&pUPnPDeviceFinder));
		if (SUCCEEDED(hr))
		{
			LONG lFindData;

			hr = pUPnPDeviceFinder->CreateAsyncFind(_bstr_t("urn:schemas-upnp-org:device:ZonePlayer:1"), 0, pUPnPDeviceFinderCallback, &lFindData);
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

bool SonosInterface::HttpRequest(const char* ip, int port, const char* path, std::string& document)
{
	///// this should have been done by live555 stuff
	// initialize Winsock library
	//WSADATA wsadata;
	//WSAStartup(MAKEWORD(2, 2), &wsadata);

	bool success = false;

	// prepare inet address from URL of document
	sockaddr_in addr;
	addr.sin_addr.s_addr = inet_addr(ip); // put here ip address of device
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port); // put here port for connection

	const int rbsize = 4096;    // internal buffer size
	char rbuff[rbsize] = { 0 };    // internal temporary receive buffer
	int rbshift = 0;            // index in internal buffer of current begin of free space
	int b = 0;                    // bytes curently received
	int tb = 0;                    // bytes totally received
	std::string respbuff;            // response buffer
	std::string headertail("\r\n\r\n"); // request tail

	document = "";

	std::ostringstream os;

	os << "GET " << path << " HTTP/1.1\r\nHost: " << inet_ntoa(addr.sin_addr)
		<< ':' << ntohs(addr.sin_port)
		<< headertail;

	// create TCP socket
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	
	DWORD timeOutMs = 2000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeOutMs, sizeof DWORD);

	// connect socket
	connect(s, (sockaddr*)&addr, sizeof(sockaddr_in));

	// send request
	b = send(s, os.str().c_str(), os.str().length(), 0);

	if (b == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		TRACE("Error: %d\n", err);
	}


	// receive response data - document
	while ((b = recv(s, rbuff + rbshift, rbsize - rbshift, 0)) != SOCKET_ERROR)
	{
		TRACE("read %d bytes\n", b);
		// finish loop if connection has been gracefully closed
		if (b == 0)
			break;

		// sum of all received chars
		tb += b;
		// sum of currently received chars
		rbshift += b;

		// temporary buffer has been filled
		// thus copy data to response buffer
		if (rbshift == rbsize)
		{
			respbuff.append(rbuff, rbshift);

			// reset current counter
			rbshift = 0;
		}
	}

	// close connection gracefully
	shutdown(s, SD_SEND);
	//while (int bc = recv(s, rbuff, rbsize, 0))
		//if (bc == SOCKET_ERROR) break;
	closesocket(s);

	// analyse received data
	if (tb > 0)
	{
		// copy any remaining data to response buffer
		if (rbshift > 0)
			respbuff.append(rbuff, rbshift);

		// check response code in header
		if (respbuff.substr(0, respbuff.find("\r\n")).find("200 OK") != std::string::npos)
		{
			int cntlen_comp = 0;    // computed response's content length
			int cntlen_get = 0;        // retrieved response's content length
			std::string::size_type pos;
			std::string::size_type posdata = respbuff.find(headertail);

			if (posdata != std::string::npos)
			{
				// compute content length
				cntlen_comp = tb - (posdata + headertail.length());

				if (cntlen_comp > 0)
				{
					if (b == 0)
					{
						// connection has been gracefully closed
						// thus received data should be valid
						cntlen_get = cntlen_comp;
					}
					else
					{
						// get content length from http header
						// to check if number of received data is equal to number of sent data
						std::string header = respbuff.substr(0, posdata);
						std::transform(header.begin(), header.end(), header.begin(), tolower);
						if ((pos = header.find("content-length:")) != std::string::npos)
							std::istringstream(header.substr(pos, header.find("\r\n", pos)).substr(15)) >> cntlen_get;
					}

					// if number of received bytes is valid then save document content
					if (cntlen_comp == cntlen_get)
						document.assign(respbuff.begin() + posdata + headertail.length(), respbuff.end());

					success = true;
				}
			}
		}
	}

	return success;
}

bool SonosInterface::ParseUrl(const char* url, std::string& host, int& port, std::string& path)
{
	std::string urlStr(url);

	size_t pos1 = urlStr.find("//");
	size_t pos2;

	host = "";
	path = "";
	port = -1; // default to no port

	if (pos1 == std::string::npos)
		return false;

	pos1 += 2; // move past "//"

	pos2 = urlStr.find(":", pos1);

	if (pos2 != std::string::npos) // there's a port specified
	{
		host = urlStr.substr(pos1, pos2 - pos1).c_str();

		pos1 = pos2 + 1;

		pos2 = urlStr.find('/', pos1);

		if (pos2 != std::string::npos)
		{
			port = atoi(urlStr.substr(pos1, pos2 - pos1).c_str());
			pos1 = pos2;

			path = urlStr.substr(pos1);
		}
		else
		{
			port = atoi(urlStr.substr(pos1).c_str());
		}
	}
	else // no port
	{
		pos2 = urlStr.find('/',  pos1);

		if (pos2 != std::string::npos) // a path specified
		{
			host = urlStr.substr(pos1, pos2 - pos1).c_str();

			pos1 = pos2;

			path = urlStr.substr(pos1);

		}
		else // no path specified
		{
			host = urlStr.substr(pos1);
		}
	}

	return true;
}


bool SonosInterface::ParseZoneTopology(const char* pXml)
{
	const char* pZoneXml = R"(<?xml version="1.0" ?>
<?xml-stylesheet type="text/xsl" href="/xml/review.xsl"?>
<ZPSupportInfo>
<ZonePlayers>
<ZonePlayer group='RINCON_5CAAFD2A5F9601400:1' coordinator='true' wirelessmode='1' hasconfiguredssid='1' channelfreq='2437' behindwifiext='0' wifienabled='1' location='http://192.168.1.67:1400/xml/device_description.xml' version='31.8-24090' mincompatibleversion='29.0-00000' legacycompatibleversion='24.0-00000' bootseq='4' uuid='RINCON_5CAAFD2A5F9601400'>Bedroom</ZonePlayer>
<ZonePlayer group='RINCON_5CAAFD2A5F9601400:1' coordinator='false' wirelessmode='1' hasconfiguredssid='1' channelfreq='2437' behindwifiext='0' wifienabled='1' location='http://192.168.1.69:1400/xml/device_description.xml' version='31.8-24090' mincompatibleversion='29.0-00000' legacycompatibleversion='24.0-00000' bootseq='4' uuid='RINCON_5CAAFD2A5F9621400'>Spare</ZonePlayer>
<ZonePlayer group='RINCON_5CAAFD2A5F9611400:1' coordinator='true' wirelessmode='1' hasconfiguredssid='1' channelfreq='2437' behindwifiext='0' wifienabled='1' location='http://192.168.1.68:1400/xml/device_description.xml' version='31.8-24090' mincompatibleversion='29.0-00000' legacycompatibleversion='24.0-00000' bootseq='4' uuid='RINCON_5CAAFD2A5F9611400'>Kitchen</ZonePlayer>
</ZonePlayers><MediaServers><MediaServer location='192.168.1.68:3401' uuid='mobile-iPhone-3CA83CB2-36D0-4E58-AA2E-228617907F1C' version='' canbedisplayed='false' unavailable='false' type='0' ext=''>rdok-iPhone</MediaServer></MediaServers></ZPSupportInfo>
)";
	
	tinyxml2::XMLDocument xmlDoc;

	tinyxml2::XMLError ok = xmlDoc.Parse(pZoneXml);

	if (ok != tinyxml2::XML_NO_ERROR)
		return false;

	SonosDevice dev;

	tinyxml2::XMLElement* pElem = xmlDoc.FirstChildElement("ZPSupportInfo");

	if (!pElem)
		return false;

	pElem = pElem->FirstChildElement("ZonePlayers");
	
	if (!pElem)
		return false;

	pElem = pElem->FirstChildElement("ZonePlayer");

	std::string path;

	SonosGroup group;

	while (pElem)
	{
		// add device to the list, but only if it's not already there and isn't a BRIDGE or BOOST device

		dev._udn = std::string("uuid:") + pElem->Attribute("uuid"); // 'uuid:' is not included in xml version but is in UPnP?
		dev._name = pElem->GetText();

		if (dev._name != "BOOST" && dev._name != "BRIDGE" && !IsDeviceInList(dev._udn.c_str()))
		{
			dev._group = pElem->Attribute("group");
			dev._isCoordinator = strcmp(pElem->Attribute("coordinator"), "true") == 0;

			ParseUrl(pElem->Attribute("location"), dev._address, dev._port, path);

			_deviceList.push_back(dev);

			// now add to group or create a new one

			bool found = false;

			for (std::list<SonosGroup>::iterator it = _groupList.begin(); it != _groupList.end() && !found; ++it)
			{
				if (dev._group == it->_name)
				{
					found = true;

					it->_members.push_back(dev._udn);

					if (dev._isCoordinator)
						it->_coordinator = dev._udn;
				}
			}

			if (!found)
			{
				group._name = dev._group;
				group._members.push_back(dev._udn);
				
				if (dev._isCoordinator)
					group._coordinator = dev._udn;

				_groupList.push_back(group);
			}

		}

		TRACE("group:%s name:%s coordinator:%s\n", pElem->Attribute("group"), pElem->GetText(), pElem->Attribute("coordinator"));
		pElem = pElem->NextSiblingElement("ZonePlayer");
	}

	TRACE("Done\n");

	return true;
}


bool SonosInterface::IsDeviceInList(const char* pUdn)
{
	bool found = false;

	for (std::list<SonosDevice>::iterator it = _deviceList.begin(); it != _deviceList.end() && !found; it++)
	{
		if (it->_udn == pUdn)
			found = true;
	}

	return found;
}

bool SonosInterface::GetDeviceByUdn(const char* pUdn, SonosDevice& device)
{
	bool found = false;

	for (std::list<SonosDevice>::iterator it = _deviceList.begin(); it != _deviceList.end() && !found; it++)
	{
		if (it->_udn == pUdn)
		{
			device = *it;
			found = true;
		}
	}

	return found;
}

void SonosInterface::UpnpDeviceAdded(const char* pUdn, const char* pUrl)
{
	if (!IsDeviceInList(pUdn))
	{
		std::string host, path, xml;
		int port;

		if (ParseUrl(pUrl, host, port, path))
		{
			if (HttpRequest(host.c_str(), port, "/status/topology", xml))
			{
				ParseZoneTopology(xml.c_str());
			}
		}
	}
}

void SonosInterface::UpnpDeviceRemoved(const char* pUdn)
{
	//### do something 
}

void SonosInterface::UpnpSearchComplete()
{

}

bool SonosInterface::Play(const char* pUdn)
{
	HRESULT hr;

	// for now get from the UDN but this may be slow so we may have to keep the IUPnPDevice pointer
	// as the search can take 9 secs if not found

	IUPnPDeviceFinder* pUPnPDeviceFinder;

	hr = CoCreateInstance(CLSID_UPnPDeviceFinder, NULL, CLSCTX_INPROC_SERVER,
		IID_IUPnPDeviceFinder, reinterpret_cast<void**>(&pUPnPDeviceFinder));

	if (SUCCEEDED(hr))
	{
		IUPnPDevice* pDevice;
		_bstr_t serviceName("urn:upnp-org:serviceId:AVTransport"); // may need :1 on the end

		pUPnPDeviceFinder->FindByUDN(_bstr_t(pUdn), &pDevice);

		if (SUCCEEDED(hr))
		{
			// now get the services

			IUPnPServices* pServices;
			hr = pDevice->get_Services(&pServices);

			if (SUCCEEDED(hr))
			{
				// Retrieve the service we are interested in
				IUPnPService * pService = NULL;
				hr = pServices->get_Item(serviceName.GetBSTR(), &pService);
				if (SUCCEEDED(hr))
				{
					// now actually invoke the Play action

					SAFEARRAYBOUND  rgsaBound[1];
					SAFEARRAY       *psa = nullptr;

					rgsaBound[0].lLbound = 0;
					rgsaBound[0].cElements = 0;

					psa = SafeArrayCreate(VT_VARIANT, 1, rgsaBound);

					if (psa)
					{
						LONG    lStatus;
						VARIANT varInArgs;

						VariantInit(&varInArgs);

						varInArgs.vt = VT_VARIANT | VT_ARRAY;

						V_ARRAY(&varInArgs) = psa;

						pService->InvokeAction(_bstr_t("Play"), varInArgs, nullptr, nullptr);

						if (SUCCEEDED(hr))
							TRACE("Sent play\n");
						else
							TRACE("Error: Play() failed\n");

						SafeArrayDestroy(psa);
					}

					pService->Release();
				}
				pServices->Release();
			}
		}

		pDevice->Release();
	}

	pUPnPDeviceFinder->Release();

	return SUCCEEDED(hr);
}