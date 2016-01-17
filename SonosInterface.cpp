#include "stdafx.h"
#include "SonosInterface.h"
#include "tinyxml2.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <sstream>
#include <algorithm>
#include <thread>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "user32.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

const char* AvTransportEndPoint = "/MediaRenderer/AVTransport/Control";
const char* RenderingEndPoint = "/MediaRenderer/RenderingControl/Control";

///////////////////////////////////////////////////////////////////////////////
// CUPnPDeviceFinderCallback implements IUPnPDeviceFinderCallback
///////////////////////////////////////////////////////////////////////////////

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

				gotUrl = true;
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

///////////////////////////////////////////////////////////////////////////////
// SonosInterface implementation
///////////////////////////////////////////////////////////////////////////////

SonosInterface* SonosInterface::InstancePtr = nullptr;

SonosInterface* SonosInterface::GetInstance()
{
	if (!InstancePtr)
	{
		InstancePtr = new SonosInterface();
	}

	return InstancePtr;
}

void SonosInterface::Delete()
{
	delete InstancePtr;
}

SonosInterface::SonosInterface()
	: _pSearchThread(nullptr), _shutdown(false), _pClient(nullptr), 
	_searching(false), _searchCompleted(false)
{
}

SonosInterface::~SonosInterface()
{
	_shutdown = true;

	if (_searching)
		CancelAsyncSearch();

	if (_pSearchThread)
		delete _pSearchThread;

	//CoUninitialize();
}


bool SonosInterface::Init()
{
	// start the search thread

	_pSearchThread = new std::thread(&SonosInterface::SearchThread, this);

	return true;
}

void SonosInterface::SearchThread()
{
	DWORD lastSearchTime = 0;
	const DWORD SearchInterval = 5 * 60 * 1000;
	MSG msg;

	while (!_shutdown)
	{
		DWORD now = GetTickCount();

		if (!_searching && now - lastSearchTime > SearchInterval)
		{

			if (StartAsyncSearch())
			{
				lastSearchTime = now; // move to actual finish
				_searching = true;
			}
			else
			{
				TRACE("Error: StartAsyncSearch() failed\n");
			}
		}

		if (_searchCompleted)
		{
			CancelAsyncSearch();

			//_pUPnPDeviceFinder->CancelAsyncFind(_lFindData);
			//_searchCompleted = false;
			//_searching = false;
		}

		// we need to pump messages for the seach to work apparently

		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			DispatchMessage(&msg);

		Sleep(500);
	}

	CoUninitialize();

}

bool SonosInterface::StartAsyncSearch()
{
	HRESULT hr = S_OK;
	bool ok = false;

	CoInitialize(nullptr);

	_searchCompleted = false;

	_pUPnPDeviceFinderCallback = new CUPnPDeviceFinderCallback(this);

	if (NULL != _pUPnPDeviceFinderCallback)
	{
		_pUPnPDeviceFinderCallback->AddRef();
		hr = CoCreateInstance(CLSID_UPnPDeviceFinder, NULL, CLSCTX_INPROC_SERVER,
			IID_IUPnPDeviceFinder, reinterpret_cast<void**>(&_pUPnPDeviceFinder));

		if (SUCCEEDED(hr))
		{
			hr = _pUPnPDeviceFinder->CreateAsyncFind(_bstr_t("urn:schemas-upnp-org:device:ZonePlayer:1"), 0, _pUPnPDeviceFinderCallback, &_lFindData);

			if (SUCCEEDED(hr))
			{
				hr = _pUPnPDeviceFinder->StartAsyncFind(_lFindData);

				if (SUCCEEDED(hr))
					ok = true;
			}
		}
		else
		{
			_pUPnPDeviceFinder->Release();
		}
	}
	else
	{
		_pUPnPDeviceFinderCallback->Release();
	}


	return ok;
}

void SonosInterface::CancelAsyncSearch()
{
	_pUPnPDeviceFinder->CancelAsyncFind(_lFindData);
	_pUPnPDeviceFinder->Release();
	_pUPnPDeviceFinderCallback->Release();
	_searchCompleted = false;
	_searching = false;
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

					DWORD start = GetTickCount();
					
					//while (bGetMessage = GetMessage(&Message, NULL, 0, 0) && -1 != bGetMessage)
					while (GetTickCount() - start < 5000)
					{
						while (PeekMessage(&Message, NULL, 0, 0, PM_REMOVE))
							DispatchMessage(&Message);

						Sleep(500);
						
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
	std::ostringstream req;

	std::string headertail("\r\n\r\n"); // request tail

	req << "GET " << path << " HTTP/1.1\r\nHost: " << ip
		<< ':' << port
		<< headertail;

	return NetworkRequest(ip, port, path, document, req.str().c_str());
}

bool SonosInterface::NetworkRequest(const char* ip, int port, const char* path, std::string& document, const char* req)
{
	///// this should have been done by live555 stuff
	// initialize Winsock library
	//WSADATA wsadata;
	//WSAStartup(MAKEWORD(2, 2), &wsadata);

	bool success = false;

	// prepare inet address from URL of document
	sockaddr_in addr;
	//addr.sin_addr.s_addr = inet_addr(ip); // put here ip address of device
	inet_pton(AF_INET, ip, &addr.sin_addr); // put here ip address of device
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port); // put here port for connection

	const int rbsize = 4096;    // internal buffer size
	char rbuff[rbsize] = { 0 };    // internal temporary receive buffer
	int rbshift = 0;            // index in internal buffer of current begin of free space
	int b = 0;                    // bytes curently received
	int tb = 0;                    // bytes totally received
	std::string respbuff;            // response buffer
	std::string headertail("\r\n");
	document = "";

	// create TCP socket
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	DWORD timeOutMs = 2000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeOutMs, sizeof DWORD);

	// connect socket
	connect(s, (sockaddr*)&addr, sizeof(sockaddr_in));

	// send request
	b = send(s, req, strlen(req), 0);

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


/*
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
*/

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
	// for testing...

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

	tinyxml2::XMLError ok = xmlDoc.Parse(pXml);

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

		SonosDevice d;
		if (_pClient && GetDeviceByUdn(pUdn, d) && d._isCoordinator)
		{
			_pClient->OnNewDevice(d);
		}
	}
}

void SonosInterface::UpnpDeviceRemoved(const char* pUdn)
{
	SonosDevice d;

	if (_pClient && GetDeviceByUdn(pUdn, d) && d._isCoordinator)
	{
		_pClient->OnDeviceRemoved(d);
	}
}

void SonosInterface::UpnpSearchComplete()
{
	_searchCompleted = true;
}

bool SonosInterface::Play(const char* pUdn)
{
	SonosDevice dev;

	if (!GetDeviceByUdn(pUdn, dev))
		return false;

	std::string req = CreateSoapRequest(AvTransportEndPoint,
		dev._address.c_str(), dev._port,
		R"(<u:Play xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID><Speed>1</Speed></u:Play>)",
		"urn:schemas-upnp-org:service:AVTransport:1#Play");

	std::string resp;

	return NetworkRequest(dev._address.c_str(), dev._port, "/MediaRenderer/AVTransport/Control", resp, req.c_str());
}

bool SonosInterface::Pause(const char* pUdn)
{
	SonosDevice dev;

	if (!GetDeviceByUdn(pUdn, dev))
		return false;

	std::string req = CreateSoapRequest(AvTransportEndPoint,
		dev._address.c_str(), dev._port,
		R"(<u:Pause xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID></u:Pause>)",
		"urn:schemas-upnp-org:service:AVTransport:1#Pause");

	std::string resp;

	return NetworkRequest(dev._address.c_str(), dev._port, "/MediaRenderer/AVTransport/Control", resp, req.c_str());
}

bool SonosInterface::SetVolume(const char* pUdn, int volume)
{
	SonosDevice dev;

	if (!GetDeviceByUdn(pUdn, dev))
		return false;

	std::ostringstream body;
	body << R"(<u:SetVolume xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1"><InstanceID>0</InstanceID><Channel>Master</Channel><DesiredVolume>)" <<
		volume << "</DesiredVolume></u:SetVolume>";

	std::string req = CreateSoapRequest(RenderingEndPoint,
		dev._address.c_str(), dev._port,
		body.str().c_str(),
		"urn:schemas-upnp-org:service:RenderingControl:1#SetVolume");

	std::string resp;

	return NetworkRequest(dev._address.c_str(), dev._port, "/MediaRenderer/AVTransport/Control", resp, req.c_str());
}

/*POST /MediaRenderer/AVTransport/Control HTTP/1.1
CONNECTION: close
ACCEPT-ENCODING: gzip
HOST: 192.168.1.67:1400
USER-AGENT: Linux UPnP/1.0 Sonos/31.8-24090 (WDCR:Microsoft Windows NT 10.0.10586)
CONTENT-LENGTH: 1021
CONTENT-TYPE: text/xml; charset="utf-8"
SOAPACTION: "urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI"

<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body><u:SetAVTransportURI xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID><CurrentURI>x-rincon-mp3radio://streams.greenhost.nl:8080/hardbop.m3u</CurrentURI><CurrentURIMetaData>&lt;DIDL-Lite xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot; xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot; xmlns:r=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot; xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot;&gt;&lt;item id=&quot;R:0/0/13&quot; parentID=&quot;R:0/0&quot; restricted=&quot;true&quot;&gt;&lt;dc:title&gt;Test&lt;/dc:title&gt;&lt;upnp:class&gt;object.item.audioItem.audioBroadcast&lt;/upnp:class&gt;&lt;desc id=&quot;cdudn&quot; nameSpace=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot;&gt;SA_RINCON65031_&lt;/desc&gt;&lt;/item&gt;&lt;/DIDL-Lite&gt;</CurrentURIMetaData></u:SetAVTransportURI></s:Body></s:Envelope>*/

bool SonosInterface::SetAvTransportUri(const char* pUdn, const char* pUri, const char* pTitle)
{
	SonosDevice dev;

	if (!GetDeviceByUdn(pUdn, dev))
		return false;



	std::ostringstream body;
	body << R"(<u:SetAVTransportURI xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID><CurrentURI>)"
		<< pUri << "</CurrentURI><CurrentURIMetaData>" <<  "" << /*FormatMetaData(pTitle) <<*/ "</CurrentURIMetaData></u:SetAVTransportURI>";

	std::string req = CreateSoapRequest(AvTransportEndPoint,
		dev._address.c_str(), dev._port, body.str().c_str(),
		"urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI");

	std::string resp;

	return NetworkRequest(dev._address.c_str(), dev._port, "/MediaRenderer/AVTransport/Control", resp, req.c_str());
}

bool SonosInterface::MustEscape(char ch, std::string& escaped)
{
	bool mustEscape = true;

	switch (ch)
	{
	case '<': escaped = "&lt;"; break;
	case '>': escaped = "&gt;"; break;
	case '"': escaped = "&quot;"; break;
	case '&': escaped = "&amp;"; break;
	case '\'': escaped = "&apos;"; break;
	default: mustEscape = false;
	}

	return mustEscape;
}

std::string SonosInterface::FormatMetaData(const char* pTitle)
{
	//std::string meta;
	std::ostringstream meta;

	meta << R"(<?xml version="1.0"?>)"
		R"(<DIDL-Lite xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/" xmlns:r="urn:schemas-rinconnetworks-com:metadata-1-0/" xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/">)"
		R"(<item id="R:0/0/49" parentID="R:0/0" restricted="true">)"
		"<dc:title>"
		<< pTitle <<
		"</dc:title>"
		"<upnp:class>object.item.audioItem.audioBroadcast</upnp:class>"
		R"(<desc id="cdudn" nameSpace="urn:schemas-rinconnetworks-com:metadata-1-0/">SA_RINCON65031_</desc>)"
		"</item>"
		"</DIDL-Lite>";

	// now escape it as it's got to be embedded in the XML

	std::string m = meta.str();
	std::string escaped("");

	meta.str("");

	for (int i = 0; i < (int)m.length(); i++)
	{
		
		if (MustEscape(m[i], escaped))
			meta << escaped;
		else
			meta << m[i];

	}

	return meta.str();
}

std::string SonosInterface::CreateSoapRequest(const char* endPoint, const char* host, int port, const char* body, const char* action)
{

	std::ostringstream envelope;
	std::ostringstream req;

	envelope << R"(<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">)"
		"<s:Body>" << body << "</s:Body>"
		"</s:Envelope>";

	req << "POST " << endPoint << " HTTP/1.0\r\n"
		"CONNECTION: close\r\n"
		"HOST: " << host << ":" << port << "\r\n"
		"CONTENT-LENGTH: " << envelope.str().length() << "\r\n"
		"CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n"
		"SOAPACTION: " << "\"" << action << "\"\r\n\r\n" <<
		envelope.str();

	return req.str();
}
/*

---------------------------------------------------------
Actual exchange from WireShark:

POST /MediaRenderer/AVTransport/Control HTTP/1.1
CONNECTION: close
ACCEPT-ENCODING: gzip
HOST: 192.168.1.67:1400
USER-AGENT: Linux UPnP/1.0 Sonos/31.8-24090 (WDCR:Microsoft Windows NT 10.0.10586)
CONTENT-LENGTH: 266
CONTENT-TYPE: text/xml; charset="utf-8"
SOAPACTION: "urn:schemas-upnp-org:service:AVTransport:1#Play"

<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
<s:Body>
<u:Play xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID><Speed>1</Speed></u:Play>
</s:Body>
</s:Envelope>


Response:

HTTP/1.1 200 OK
CONTENT-LENGTH: 240
CONTENT-TYPE: text/xml; charset="utf-8"
EXT:
Server: Linux UPnP/1.0 Sonos/31.8-24090 (ZPS1)
Connection: close

<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body><u:PlayResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"></u:PlayResponse></s:Body></s:Envelope>

-------------------------------------------

To send a SOAP request connect to <device ip>:<port><endpoint> e.g. TRANSPORT_ENDPOINT = '/MediaRenderer/AVTransport/Control' and do

POST <endpoint?> HTTP/1.0(or 1.1?) 

_headers_:

SOAPAction: <action name> [e.g. "urn:schemas-upnp-org:service:AVTransport:1#Play"] - needs quotes?
Content-Type: text/xml; charset=utf-8
Content-Length: <number>

_body_: [has soap envelope around actual body]

>>>envelope

<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
<s:Body>$$$actual body$$$</s:Body>'
</s:Envelope>

>>>actual "play" body

<u:Play xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID><Speed>1</Speed></u:Play>


*/