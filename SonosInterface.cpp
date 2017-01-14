#include "stdafx.h"
#include "SonosInterface.h"
#include "tinyxml2.h"
#include "Log.h"
#include "UdpSocket.h"

#include <comutil.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <sstream>
#include <algorithm>
#include <thread>
#include <regex>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "user32.lib")

//#ifdef _DEBUG
//#define new DEBUG_NEW
//#endif

//#define MS_UPNP

const char* AvTransportEndPoint = "/MediaRenderer/AVTransport/Control";
const char* RenderingEndPoint = "/MediaRenderer/RenderingControl/Control";
const char* ContentDirectoryEndPoint = "/MediaServer/ContentDirectory/Control";


//#ifdef MS_UPNP
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

		_bstr_t udn;
		hr = pDevice->get_UniqueDeviceName(&udn.GetBSTR());

		if (SUCCEEDED(hr))
		{
			devUdn = udn;

			gotUdn = true;

			_bstr_t friendlyName;
			hr = pDevice->get_FriendlyName(&friendlyName.GetBSTR());

			if (SUCCEEDED(hr))
			{
				LOG("Device Added: udn: %s, name: %s\n", (const char*)udn, (const char*)friendlyName);
				//::SysFreeString(friendlyName);
			}
			//::SysFreeString(udn.GetBSTR());
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
				//SysFreeString(url.GetBSTR());

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
		LOG("Device Removed: udn: %S", bstrUDN);

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

//#endif // MS_UPNP

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
	_searching(false), _searchCompleted(false), _lastFavUpdateTime(0)
{
}

SonosInterface::~SonosInterface()
{
	_shutdown = true;

	if (_searching)
		CancelAsyncSearch();

	if (_pSearchThread)
	{
		_pSearchThread->join();
		delete _pSearchThread;
	}

	//CoUninitialize();
}


bool SonosInterface::Init()
{
	// start the search thread

#ifdef NO_SONOS
	SonosDevice d(std::string("uuid:none"), std::string("TestSonos"), std::string("127.0.0.1"), 1400, std::string("TestSonos"), true);
	_pClient->OnNewDevice(d);
#else
	_pSearchThread = new std::thread(&SonosInterface::SearchThread, this);
#endif

	return true;
}

#ifdef MS_UPNP

void SonosInterface::SearchThread()
{
	DWORD lastSearchTime = 0;
	const DWORD SearchInterval = 1 * 60 * 1000; //5 * 60 * 1000;
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
				LOG("Error: StartAsyncSearch() failed\n");
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

#else // using our UPNP SSDP implementation

// Using the original MS UPNP COM api for speaker detection
// stopped working on some machines. It may be a change to the
// firmware or it could be due to installing VPN and/or VM network
// interfaces. SSDP multicast packets were sent to 239.255.255.250
// port 1900. These were observed in Wireshark but no responses were
// seen. Broadcasting the same packets to 255.255.255.255:1900 does
// seem to still work and respnses will be received directed to the src
// address and port. This is what has now been implemented.

void SonosInterface::SearchThread()
{
	const DWORD SearchInterval = 1 * 60 * 1000; //5 * 60 * 1000;

	char* SsdpSearchPacket =
		"M-SEARCH * HTTP/1.1\r\n"
		"Host: 239.255.255.250:1900\r\n"
		"ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
		"Man: \"ssdp:discover\"\r\n"
		"MX: 1\r\n\r\n";

	const int SsdpPort = 1900;

	DWORD lastSearchTime = 0;
	CUdpSocket sock;
	int bytesRead = 0;
	const int MaxRxBytes = 2048;
	char rxBuff[MaxRxBytes];

	if (!sock.InitialiseBroadcastRx(-1))
		LOG("SonosInterface::SearchThread() unable to initialise socket\n");

	while (!_shutdown)
	{
		DWORD now = GetTickCount();

		if (now - lastSearchTime > SearchInterval)
		{
			if (sock.Broadcast(SsdpPort, SsdpSearchPacket, strlen(SsdpSearchPacket)))
				LOG("SonosInterface::SearchThread() sent SSDP search packet\n");
			else
				LOG("SonosInterface::SearchThread() send SSDP packet failed\n");

			if (sock.Broadcast(SsdpPort, SsdpSearchPacket, strlen(SsdpSearchPacket)))
				LOG("SonosInterface::SearchThread() sent SSDP search packet\n");
			else
				LOG("SonosInterface::SearchThread() send SSDP packet failed\n");

			lastSearchTime = now;
		}

		bytesRead = sock.Read(rxBuff, MaxRxBytes);

		if (bytesRead > 0)
		{
			rxBuff[bytesRead] = '\0';

			HandleSsdpResponse(rxBuff);

			//LOG(rxBuff);
		}

		Sleep(500);
	}

	CoUninitialize();
}
#endif

void SonosInterface::HandleSsdpResponse(const char* resp)
{
	std::string s = resp;

	if (s.find("device:ZonePlayer:1") == std::string::npos)
		return;

	// get the device details URL

	size_t start = s.find("LOCATION: ");

	if (start == std::string::npos)
	{
		LOG("SonosInterface::HandleSsdpResponse() no LOCATION field\n");
		return;
	}

	start += 10;

	size_t end = s.find('\r', start);

	if (end == std::string::npos)
	{
		LOG("SonosInterface::HandleSsdpResponse() no LOCATION field end\n");
		return;
	}
	
	std::string url = s.substr(start, end - start);

	// get the unique device name

	start = s.find("USN:");

	if (start == std::string::npos)
	{
		LOG("SonosInterface::HandleSsdpResponse() no USN field\n");
		return;
	}

	start += 4;

	end = s.find("::", start);

	if (end == std::string::npos)
	{
		LOG("SonosInterface::HandleSsdpResponse() no USN field end\n");
		return;
	}

	std::string udn = s.substr(start, end - start);

	UpnpDeviceAdded(udn.c_str(), url.c_str());
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
				else
					LOG("SonosInterface: StartAsyncFind() failed");
			}
			else
			{
				LOG("SonosInterface: CreateAsyncFind() failed");
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
							LOG("%S\n", bstrName);
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
#ifdef NO_SONOS
	return true;
#endif

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

	DWORD startTime = GetTickCount();

	// create TCP socket
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	DWORD timeOutMs = 5000;
	//setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeOutMs, sizeof DWORD);

	// connect socket
	if (connect(s, (sockaddr*)&addr, sizeof(sockaddr_in)) == SOCKET_ERROR)
	{
		int err = WSAGetLastError();

		LOG("SonosInterface::NetworkRequest() connect failed: %d\n", err);
	}

	// send request
	b = send(s, req, strlen(req), 0);

	if (b == SOCKET_ERROR || b == 0)
	{
		int err = WSAGetLastError();
		LOG("Error: %d\n", err);
	}

	// receive response data - document
	while ((b = recv(s, rbuff + rbshift, rbsize - rbshift, 0)) != SOCKET_ERROR)
	{
		//LOG("SonosInterface::NetworkRequest() read %d bytes\n", b);
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

	if (b == SOCKET_ERROR)
	{
		DWORD now = GetTickCount();
		int err = WSAGetLastError();
		LOG("SonosInterface() recv error:%d read:%d delay:%dms\n", err, tb, now-startTime);
	}

	// close connection gracefully
	shutdown(s, SD_SEND);
	//while (int bc = recv(s, rbuff, rbsize, 0))
	//if (bc == SOCKET_ERROR) break;
	closesocket(s);

//LOG("tb: %d\n", tb);

	// analyse received data
	if (tb > 0)
	{
		// copy any remaining data to response buffer
		if (rbshift > 0)
			respbuff.append(rbuff, rbshift);

		// check response code in header
		if (respbuff.substr(0, respbuff.find("\r\n")).find("200 OK") != std::string::npos)
		{
LOG("Got 200 OK\n");
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
		else
		{
			std::string resp(respbuff.c_str(), 20);
			resp += "\n";
LOG("SonosInterface::NetworkRequest() bad response: %s\n", resp.c_str());
		}
	}

//LOG("Success: %d\n", success);

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

/*CONTENT-TYPE: text/xml
Server: Linux UPnP/1.0 Sonos/31.8-24090 (ZPS1)
Connection: close

<?xml version="1.0" ?>
<?xml-stylesheet type="text/xsl" href="/xml/review.xsl"?>
<ZPSupportInfo>
<ZonePlayers>
<ZonePlayer group='RINCON_5CAAFD2A5F9601400:1' coordinator='true' wirelessmode='1' hasconfiguredssid='1' channelfreq='2437' behindwifiext='0' wifienabled='1' location='http://192.168.1.67:1400/xml/device_description.xml' version='31.8-24090' mincompatibleversion='29.0-00000' legacycompatibleversion='24.0-00000' bootseq='10' uuid='RINCON_5CAAFD2A5F9601400'>Bedroom</ZonePlayer>
<ZonePlayer group='RINCON_5CAAFD976C7001400:1' coordinator='true' wirelessmode='1' hasconfiguredssid='1' channelfreq='2437' behindwifiext='0' wifienabled='1' location='http://192.168.1.77:1400/xml/device_description.xml' version='31.8-24090' mincompatibleversion='29.0-00000' legacycompatibleversion='24.0-00000' bootseq='3' uuid='RINCON_5CAAFD976C7001400'>Kitchen</ZonePlayer>
</ZonePlayers>
<MediaServers>
<MediaServer location='192.168.1.64:3401' uuid='mobile-iPhone-3CA83CB2-36D0-4E58-AA2E-228617907F1C' version='' canbedisplayed='false' unavailable='false' type='0' ext=''>rdok-iPhone</MediaServer>
</MediaServers></ZPSupportInfo>*/

	
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

		// ignore none-speaker devices
		if (dev._name != "BOOST" && dev._name != "BRIDGE")
		{
			//if (!IsDeviceInList(dev._udn.c_str()))
			//{
			dev._group = pElem->Attribute("group");
			dev._isCoordinator = strcmp(pElem->Attribute("coordinator"), "true") == 0;

			ParseUrl(pElem->Attribute("location"), dev._address, dev._port, path);

			// get favourites
			const DWORD FavUpdatePeriod = 5 * 60 * 1000;
			DWORD now = GetTickCount();
			if (now - _lastFavUpdateTime >= FavUpdatePeriod)
			{
				GetFavouritesBlocking(dev);
				_lastFavUpdateTime = now;
			}

			if (!IsDeviceInList(dev._udn.c_str()))
			{

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

				if (_pClient)
				{
					// let the client know about a new device
					if (dev._isCoordinator)
						_pClient->OnNewDevice(dev);
				}
			}
			else // device in list - update our record and notify client if necessary
			{
				UpdateDeviceRecord(dev);
			}

		}

		LOG("group:%s name:%s coordinator:%s\n", pElem->Attribute("group"), pElem->GetText(), pElem->Attribute("coordinator"));
		pElem = pElem->NextSiblingElement("ZonePlayer");
	}

	LOG("Done\n");

	return true;
}

// copies the current list of devices to list
void SonosInterface::GetListOfDevices(std::list<SonosDevice>& list)
{
	// lock the list
	std::lock_guard<std::mutex> lock(_listMutex);

	list = _deviceList;
}

void SonosInterface::UpdateDeviceRecord(const SonosDevice& dev)
{
	for (auto it = _deviceList.begin(); it != _deviceList.end(); ++it)
	{
if (dev._udn == it->_udn)
{
	if (dev._address != it->_address)
	{
		it->_address = dev._address;
		if (_pClient)
			_pClient->OnDeviceAddressChanged(dev);
	}

	if (dev._name != it->_name)
	{
		it->_name = dev._name;
		if (_pClient)
			_pClient->OnDeviceNameChanged(dev, it->_name);
	}

	if (dev._isCoordinator != it->_isCoordinator)
	{
		it->_isCoordinator = dev._isCoordinator;
		if (_pClient)
			_pClient->OnDeviceCoordinatorStatusChanged(dev);
	}

	break;
}
	}

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

	std::lock_guard<std::mutex> lock(_listMutex);

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

bool SonosInterface::GetDeviceByName(const char* pName, SonosDevice& device)
{
	bool found = false;

	std::lock_guard<std::mutex> lock(_listMutex);

	for (std::list<SonosDevice>::iterator it = _deviceList.begin(); it != _deviceList.end() && !found; it++)
	{
		if (it->_name == pName)
		{
			device = *it;
			found = true;
		}
	}

	return found;
}

bool SonosInterface::GetDeviceByNameOrUdn(const std::string& id, SonosDevice& device)
{
	bool found = false;
	bool isUdn = false;

	if (id.size() > 5 && id.substr(0, 5) == "uuid:")
		isUdn = true;

	std::string name = id;

	// ignore case of the name
	if (!isUdn)
		std::transform(name.begin(), name.end(), name.begin(), ::toupper);

	std::lock_guard<std::mutex> lock(_listMutex);

	for (std::list<SonosDevice>::iterator it = _deviceList.begin(); it != _deviceList.end() && !found; it++)
	{

		if (isUdn)
		{
			if (it->_udn == id)
			{
				device = *it;
				found = true;
			}
		}
		else
		{
			std::string recName = it->_name;
			transform(recName.begin(), recName.end(), recName.begin(), ::toupper);
			
			if (recName == name)
			{
				device = *it;
				found = true;
			}
		}
	}

	return found;
}

void SonosInterface::UpnpDeviceAdded(const char* pUdn, const char* pUrl)
{
	//if (!IsDeviceInList(pUdn))
	//{
		std::string host, path, xml;
		int port;

		if (ParseUrl(pUrl, host, port, path))
		{
			if (HttpRequest(host.c_str(), port, "/status/topology", xml))
			{
				ParseZoneTopology(xml.c_str());
			}
		}

		// Now done in ParseZoneTopology
		//SonosDevice d;
		//if (_pClient && GetDeviceByUdn(pUdn, d) && d._isCoordinator)
		//{
		//	_pClient->OnNewDevice(d);
		//}
	//}
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

bool SonosInterface::PlayUri(std::string udn, std::string uri, std::string title)
{
	std::thread thread(&SonosInterface::PlayUriBlocking, this, udn, uri, title);
	thread.detach();

	return true;
}

bool SonosInterface::Stop(std::string udn)
{
	std::thread thread(&SonosInterface::StopBlocking, this, udn);
	thread.detach();

	return true;
}

bool SonosInterface::SetVolume(std::string udn, int volume)
{
	std::thread thread(&SonosInterface::SetVolumeBlocking, this, udn, volume);
	thread.detach();

	return true;
}

bool SonosInterface::PlayFileFromServer(std::string room, std::string uri, std::string title)
{
	std::thread thread(&SonosInterface::PlayFileFromServerBlocking, this, room, uri, title);
	thread.detach();

	return true;
}

bool SonosInterface::PlayUriBlocking(std::string udn, std::string uri, std::string title)
{
	bool ok = false;

	if (SetAvTransportUriBlocking(udn, uri, title))
		if (PlayBlocking(udn))
			ok = true;
		else
			LOG("SonosInterface::Play() failed\n");
	else
		LOG("SonosInterface::SetAvTransportUri() failed\n");

	return ok;
}

bool SonosInterface::PlayBlocking(std::string udn)
{
	LOG("Sonos: PLAY -> %s\n", udn.c_str());

	SonosDevice dev;

	if (!GetDeviceByNameOrUdn(udn.c_str(), dev))
		return false;

	std::string req = CreateSoapRequest(AvTransportEndPoint,
		dev._address.c_str(), dev._port,
		R"(<u:Play xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID><Speed>1</Speed></u:Play>)",
		"urn:schemas-upnp-org:service:AVTransport:1#Play");

	std::string resp;

	return NetworkRequest(dev._address.c_str(), dev._port, "/MediaRenderer/AVTransport/Control", resp, req.c_str());
}

bool SonosInterface::PlayFavouriteBlocking(std::string id, std::string fav)
{
	LOG("Sonos: PLAY FAVOURITE [%s] -> %s\n", fav.c_str(), id.c_str());

	// look up the favourite but first make uppercase to do case insensitive match

	std::transform(fav.begin(), fav.end(), fav.begin(), ::toupper);

	SonosFavourite favRec;

	// scope to lock the _favMap favourites container
	{
		std::lock_guard<std::mutex> lock(_listMutex);

		auto it = _favMap.find(fav);
		if (it == _favMap.end())
			return false;

		favRec = (*it).second;
	}

	// TODO: move the following EscapeXml() call to PlayUriBlocking() if this works
	// ok

	return PlayUriBlocking(id, EscapeXml(favRec._url), favRec._name);
}

bool SonosInterface::PauseBlocking(std::string id)
{
	LOG("Sonos: PAUSE -> %s\n", id.c_str());

	if (id.empty() || id == "ALL")
	{
		DoForEachDevice([this](std::string u) {return StopBlocking(u); });

		//DoForEachDevice(std::bind(&SonosInterface::StopBlocking, this));

		return true;
	}

	SonosDevice dev;

	if (!GetDeviceByNameOrUdn(id, dev))
		return false;

	std::string req = CreateSoapRequest(AvTransportEndPoint,
		dev._address.c_str(), dev._port,
		R"(<u:Pause xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID></u:Pause>)",
		"urn:schemas-upnp-org:service:AVTransport:1#Pause");

	std::string resp;

	return NetworkRequest(dev._address.c_str(), dev._port, "/MediaRenderer/AVTransport/Control", resp, req.c_str());
}

bool SonosInterface::StopBlocking(std::string id)
{
	LOG("Sonos: STOP -> %s\n", id.c_str());

	if (id.empty() || id == "ALL")
	{
		DoForEachDevice( [this](std::string u) {return StopBlocking(u);} );

		//DoForEachDevice(std::bind(&SonosInterface::StopBlocking, this));

		return true;
	}
	
	SonosDevice dev;

	if (!GetDeviceByNameOrUdn(id, dev))
		return false;

	std::string req = CreateSoapRequest(AvTransportEndPoint,
		dev._address.c_str(), dev._port,
		R"(<u:Stop xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID><Speed>1</Speed></u:Stop>)",
		"urn:schemas-upnp-org:service:AVTransport:1#Stop");

	std::string resp;

	return NetworkRequest(dev._address.c_str(), dev._port, "/MediaRenderer/AVTransport/Control", resp, req.c_str());
}

bool SonosInterface::SetVolumeBlocking(std::string udn, int volume)
{
	LOG("Sonos: VOLUME %d -> %s\n", volume, udn.c_str());

	SonosDevice dev;

	if (!GetDeviceByUdn(udn.c_str(), dev))
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

bool SonosInterface::PlayFileFromServerBlocking(std::string speaker, std::string uri, std::string title)
{
	LOG("Sonos: PLAYFILE %s -> %s\n", uri.c_str(), speaker.c_str());
	SonosDevice dev;

	bool success = false;

	if (speaker == "ALL") // play on every speaker
	{
		std::vector<std::string> deviceList;
		{
			std::lock_guard<std::mutex> lock(_listMutex);

			for (SonosDevice& d : _deviceList)
			{
				if (!d._udn.empty() && d._udn != "ALL" && d._isCoordinator)
					deviceList.push_back(d._udn);
			}
		}

		for (std::string& u : deviceList)
			PlayUriBlocking(u, uri, title);
	}
	else
	{
		if (!GetDeviceByName(speaker.c_str(), dev))
		{
			LOG("SonosInterface::PlayFileFromServerBlocking() couldn't find %s\n", speaker.c_str());
			return false;
		}

		success = PlayUriBlocking(dev._udn, uri, title);
	}

	return success;
}

/* Example from Sonos Play:1
POST /MediaRenderer/AVTransport/Control HTTP/1.1
CONNECTION: close
ACCEPT-ENCODING: gzip
HOST: 192.168.1.67:1400
USER-AGENT: Linux UPnP/1.0 Sonos/31.8-24090 (WDCR:Microsoft Windows NT 10.0.10586)
CONTENT-LENGTH: 1021
CONTENT-TYPE: text/xml; charset="utf-8"
SOAPACTION: "urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI"

<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
<s:Body>
  <u:SetAVTransportURI xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
    <InstanceID>0</InstanceID>
    <CurrentURI>x-rincon-mp3radio://streams.greenhost.nl:8080/hardbop.m3u</CurrentURI>
    <CurrentURIMetaData>&lt;DIDL-Lite xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot; xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot; xmlns:r=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot; xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot;&gt;&lt;item id=&quot;R:0/0/13&quot; parentID=&quot;R:0/0&quot; restricted=&quot;true&quot;&gt;&lt;dc:title&gt;Test&lt;/dc:title&gt;&lt;upnp:class&gt;object.item.audioItem.audioBroadcast&lt;/upnp:class&gt;&lt;desc id=&quot;cdudn&quot; nameSpace=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot;&gt;SA_RINCON65031_&lt;/desc&gt;&lt;/item&gt;&lt;/DIDL-Lite&gt;</CurrentURIMetaData>
  </u:SetAVTransportURI>
</s:Body></s:Envelope>
*/

bool SonosInterface::SetAvTransportUriBlocking(std::string udn, std::string uri, std::string title)
{
	LOG("Sonos: URI %s -> %s\n", uri.c_str(), udn.c_str());
	SonosDevice dev;

	if (!GetDeviceByNameOrUdn(udn.c_str(), dev))
		return false;

	std::ostringstream body;
	body << R"(<u:SetAVTransportURI xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID><CurrentURI>)"
		<< uri << "</CurrentURI><CurrentURIMetaData>" << FormatMetaData(title.c_str()) << "</CurrentURIMetaData></u:SetAVTransportURI>";

	std::string req = CreateSoapRequest(AvTransportEndPoint,
		dev._address.c_str(), dev._port, body.str().c_str(),
		"urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI");

	std::string resp;

	return NetworkRequest(dev._address.c_str(), dev._port, "/MediaRenderer/AVTransport/Control", resp, req.c_str());
}

bool SonosInterface::GetVolumeBlocking(std::string id, int& volume)
{
	LOG("Sonos: GET VOLUME from %s\n", id.c_str());

	SonosDevice dev;

	if (!GetDeviceByNameOrUdn(id, dev))
		return false;

	std::ostringstream body;
	body << R"(<u:GetVolume xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1"><InstanceID>0</InstanceID><Channel>Master</Channel></u:GetVolume>)";

	std::string req = CreateSoapRequest(RenderingEndPoint,
		dev._address.c_str(), dev._port,
		body.str().c_str(),
		"urn:schemas-upnp-org:service:RenderingControl:1#GetVolume");

	std::string resp;

	bool success = NetworkRequest(dev._address.c_str(), dev._port, "/MediaRenderer/RenderingControl/Control", resp, req.c_str());

	// ### now parse the result - look for <CurrentVolume>

	// regex version

	std::regex regex("<CurrentVolume>([0-9]+)</CurrentVolume>"); // note the parentheses that create a capture group
	std::smatch matches;

	bool found = std::regex_search(resp, matches, regex);

	if (found && matches.size() > 1)
	{
		volume = std::stoi(matches[1]);
		success = true;
	}


	/* XML version
	int pos = resp.find("\r\n\r\n");

	if (pos != std::string::npos)
	{
		tinyxml2::XMLDocument xmlDoc;

		tinyxml2::XMLError ok = xmlDoc.Parse(resp.substr(pos+4).c_str());

		if (ok != tinyxml2::XML_NO_ERROR)
			return false;

		tinyxml2::XMLElement* pElem = xmlDoc.FirstChildElement("s:Envelope");

		if (!pElem)
			return false;

		pElem = pElem->FirstChildElement("s:Body");

		if (!pElem)
			return false;
		
		pElem = pElem->FirstChildElement("u:GetVolumeResponse");

		if (!pElem)
			return false;

		pElem = pElem->FirstChildElement("CurrentVolume");

		if (!pElem)
			return false;

		volume = atoi(pElem->GetText());
		success = true;

	}
	*/

	return success;
}

bool SonosInterface::GetMuteBlocking(std::string id, bool& isMuted)
{
	LOG("Sonos: GET MUTE from %s\n", id.c_str());

	SonosDevice dev;

		if (!GetDeviceByNameOrUdn(id, dev))
			return false;

	std::ostringstream body;
	body << R"(<u:GetMute xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1"><InstanceID>0</InstanceID><Channel>Master</Channel></u:GetMute>)";

	std::string req = CreateSoapRequest(RenderingEndPoint,
		dev._address.c_str(), dev._port,
		body.str().c_str(),
		"urn:schemas-upnp-org:service:RenderingControl:1#GetMute");

	std::string resp;

	bool success = NetworkRequest(dev._address.c_str(), dev._port, "/MediaRenderer/RenderingControl/Control", resp, req.c_str());

	// ### now parse the result - look for <CurrentMute>

	// regex version

	std::regex regex("<CurrentMute>([0-9]+)</CurrentMute>"); // note the parentheses that create a capture group
	std::smatch matches;

	bool found = std::regex_search(resp, matches, regex);

	if (found && matches.size() > 1)
	{
		isMuted = std::stoi(matches[1]) != 0;
		success = true;
	}

	return success;
}

bool SonosInterface::GetTransportInfoBlocking(std::string id, TransportState& state)
{
	LOG("Sonos: GET TransportInfo from %s\n", id.c_str());

	SonosDevice dev;
	
	if (!GetDeviceByNameOrUdn(id.c_str(), dev))
		return false;

	std::ostringstream body;
	body << R"(<u:GetTransportInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID></u:GetTransportInfo>)";

	std::string req = CreateSoapRequest(AvTransportEndPoint,
		dev._address.c_str(), dev._port,
		body.str().c_str(),
		"urn:schemas-upnp-org:service:AVTransport:1#GetTransportInfo");

	std::string resp;

	bool success = NetworkRequest(dev._address.c_str(), dev._port, AvTransportEndPoint, resp, req.c_str());

	// ### now parse the result - look for <CurrentTransportState>
	// options are believed to be:
	// STOPPED
	// PLAYING
	// PAUSED_PLAYBACK
	// TRANSITIONINGp

	std::regex regex("<CurrentTransportState>(.*)</CurrentTransportState>"); // note the parentheses that create a capture group
	std::smatch matches;

	bool found = std::regex_search(resp, matches, regex);

	if (found && matches.size() > 1)
	{
		state = TransportState::Stopped;

		if (matches[1] == "STOPPED")
			state = TransportState::Stopped;
		else if (matches[1] == "PLAYING")
			state = TransportState::Playing;
		else if (matches[1] == "PAUSED_PLAYBACK")
			state = TransportState::Paused;
		else if (matches[1] == "TRANSITIONING")
			state = TransportState::Transitioning;
		else
			LOG("SonosInterface::GetTransportInfoBloacking() error: unknown TransportState %s\n", matches[1].str().c_str());
		
		success = true;
	}

	return success;
}

bool SonosInterface::GetMediaInfoBlocking(std::string id, std::string& uri)
{
	LOG("Sonos: GET MediaInfo from %s\n", id.c_str());

	SonosDevice dev;

	if (!GetDeviceByNameOrUdn(id, dev))
		return false;

	std::ostringstream body;
	body << R"(<u:GetMediaInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID></u:GetMediaInfo>)";

	std::string req = CreateSoapRequest(AvTransportEndPoint,
		dev._address.c_str(), dev._port,
		body.str().c_str(),
		"urn:schemas-upnp-org:service:AVTransport:1#GetMediaInfo");

	std::string resp;

	bool success = NetworkRequest(dev._address.c_str(), dev._port, AvTransportEndPoint, resp, req.c_str());

	// ### now parse the result - look for <CurrentURI>

	// regex version

	std::regex regex("<CurrentURI>(.*)</CurrentURI>"); // note the parentheses that create a capture group
	std::smatch matches;

	bool found = std::regex_search(resp, matches, regex);

	if (found && matches.size() > 1)
	{
		uri = std::stoi(matches[1]) != 0;
		success = true;
	}

	return success;
}

bool SonosInterface::GetPositionInfoBlocking(std::string id, bool& tbd)
{
	LOG("Sonos: GET PositionInfo from %s\n", id.c_str());

	SonosDevice dev;

	if (!GetDeviceByNameOrUdn(id, dev))
		return false;

	std::ostringstream body;
	body << R"(<u:GetPositionInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID></u:GetPositionInfo>)";

	std::string req = CreateSoapRequest(AvTransportEndPoint,
		dev._address.c_str(), dev._port,
		body.str().c_str(),
		"urn:schemas-upnp-org:service:AVTransport:1#GetPositionInfo");

	std::string resp;

	bool success = NetworkRequest(dev._address.c_str(), dev._port, AvTransportEndPoint, resp, req.c_str());

	// ### now parse the result - look for lots of elements! esp TrackURI

	return success;
}

bool SonosInterface::GetFavouritesBlocking(std::string id)
{
	SonosDevice dev;

	if (!GetDeviceByNameOrUdn(id, dev))
		return false;

	return GetFavouritesBlocking(dev);
}

bool SonosInterface::GetFavouritesBlocking(const SonosDevice& dev)
{
	LOG("Sonos: GET Favourites from %s\n", dev._name.c_str());

	/* Need to send the following:
	< ? xml version = "1.0" encoding = "utf-8" ? >
		<s:Envelope s : encodingStyle = "http://schemas.xmlsoap.org/soap/encoding/" xmlns : s = "http://schemas.xmlsoap.org/soap/envelope/">
		<s:Body>
		<u:Browse xmlns : u = "urn:schemas-upnp-org:service:ContentDirectory:1">
		<ObjectID>FV:2< / ObjectID>
		<BrowseFlag>BrowseDirectChildren< / BrowseFlag>
		<Filter / >
		<StartingIndex>0< / StartingIndex>
		<RequestedCount>0< / RequestedCount>
		<SortCriteria / >
		< / u:Browse>
		< / s:Body>
		< / s:Envelope>
		*/

	std::ostringstream body;
	body << R"(<u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1"><ObjectID>FV:2</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag><Filter/><StartingIndex>0</StartingIndex><RequestedCount>0</RequestedCount><SortCriteria/></u:Browse>)";

	std::string req = CreateSoapRequest(ContentDirectoryEndPoint,
		dev._address.c_str(), dev._port,
		body.str().c_str(),
		"urn:schemas-upnp-org:service:ContentDirectory:1#Browse");

	std::string resp;

	bool success = NetworkRequest(dev._address.c_str(), dev._port, ContentDirectoryEndPoint, resp, req.c_str());

	if (!success)
		return false;

	resp = UnChunkEncode(resp);

	// now parse the xml result

	tinyxml2::XMLDocument xmlDoc;

	int xmlStart = resp.find("<Result>");

	if (xmlStart == std::string::npos)
		return false;

	std::string result = resp.substr(xmlStart);
	result = UnescapeXml(result);

	tinyxml2::XMLError ok = xmlDoc.Parse(result.c_str());

	if (ok != tinyxml2::XML_NO_ERROR)
		return false;

	// the Browse result is included in a <Result> tag. The contents are html escaped
	//tinyxml2::XMLElement* pElem = xmlDoc.FirstChildElement("Result");

	tinyxml2::XMLElement* pElem = nullptr;

	pElem = xmlDoc.FirstChildElement("Result");

	if (!pElem)
		return false;

	pElem = pElem->FirstChildElement("DIDL-Lite");

	if (!pElem)
		return false;

	pElem = pElem->FirstChildElement("item");

	// clear the favourites map
	{
		std::lock_guard<std::mutex> lock(_listMutex);

		_favMap.clear();

		while (pElem)
		{
			tinyxml2::XMLElement* pTitle = pElem->FirstChildElement("dc:title");
			tinyxml2::XMLElement* pRes = pElem->FirstChildElement("res");
			tinyxml2::XMLElement* pMeta = pElem->FirstChildElement("r:resMD");
			
			SonosFavourite fav(pTitle->GetText(), pRes->GetText(), pMeta->GetText());

			LOG("Favorite: %s [%s]\n", fav._name.c_str(), fav._url.c_str());

			std::string upperFav = fav._name;
			std::transform(upperFav.begin(), upperFav.end(), upperFav.begin(), ::toupper);

			_favMap[upperFav] = fav; // make the map key uppercase for case insensitive searchesp

			pElem = pElem->NextSiblingElement("item");
		}

	}
	return success;
}

std::string SonosInterface::UnChunkEncode(std::string s)
{
	std::ostringstream stream;

	// case insensitive find

	const std::string field = "transfer-encoding";

	auto it = std::search(
		s.begin(), s.end(),
		field.begin(), field.end(),
		[](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
	);

	if (it == s.end())
		return s;

	int startIndex = std::distance(s.begin(), it);

	startIndex += 18; // string + ':'

	int endIndex = s.find('\r', startIndex);

	if (startIndex == std::string::npos || endIndex == std::string::npos)
		return s;

	std::string value = s.substr(startIndex, endIndex-startIndex);

	if (value.find("chunked") == std::string::npos)
		return s;

	startIndex = s.find("\r\n\r\n"); // end of header

	startIndex += 4;

	while (s.find("\r\n\r\n", startIndex) != std::string::npos)
	{
		endIndex = s.find("\r\n", startIndex);

		if (endIndex == std::string::npos)
			break;

		std::string lenStr = s.substr(startIndex, endIndex - startIndex);

		std::stringstream hexStream;
		int len;

		hexStream << std::hex << lenStr;
		hexStream >> len;

		std::string chunk = s.substr(endIndex + 2, len);

		stream << chunk;

		startIndex = endIndex + 2 + len + 2;
	}

	return stream.str();
}


bool SonosInterface::DoForEachDevice(std::function<bool(std::string)> f)
{
	// we can't multiply lock the mutex so create a list of devices
	// then call the function on each one in turn

	std::vector<std::string> deviceList;
	{
		std::lock_guard<std::mutex> lock(_listMutex);

		for (SonosDevice& d : _deviceList)
		{
			if (!d._udn.empty() && d._udn != "ALL" && d._isCoordinator)
				deviceList.push_back(d._udn);
		}
	}

	for (std::string& u : deviceList)
		f(u);

	return true;
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

std::string SonosInterface::EscapeXml(std::string& s)
{
	std::ostringstream esc;
	std::string escaped;

	for (int i = 0; i < (int)s.length(); i++)
	{

		if (MustEscape(s[i], escaped))
			esc << escaped;
		else
			 esc << s[i];
	}

	return esc.str();
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

std::string SonosInterface::UnescapeXml(const std::string& s)
{
	std::ostringstream unescaped;

	for (size_t i = 0; i < s.length(); i++)
	{
		if (s[i] == '&')
		{
			// look for ';'
			std::ostringstream charName;
			while (i < s.length() && s[i] != ';')
			{
				charName << s[i];
				i++;
			}
			unescaped << UnescapeChar(charName.str());
		}
		else
		{
			unescaped << s[i];
		}
	}

	return unescaped.str();
}

char SonosInterface::UnescapeChar(const std::string& s)
{
	char c = '?';

	if (s == "&euro")
		c = '€';
	else if (s == "&nbsp")
		c = ' ';
	else if (s == "&quot")
		c = '\"';
	else if (s == "&amp")
		c = '&';
	else if (s == "&lt")
		c = '<';
	else if (s == "&gt")
		c = '>';
	else if (s == "&iexcl")
		c = '¡';
	else if (s == "&cent")
		c = '¢';
	else if (s == "&pound")
		c = '£';
	else if (s == "&curren")
		c = '¤';
	else if (s == "&yen")
		c = '¥';
	else if (s == "&brvbar")
		c = '¦';
	else if (s == "&sect")
		c = '§';
	else if (s == "&copy")
		c = '©';
	else if (s == "&reg")
		c = '®';
	else if (s == "&deg")
		c = '°';
	else if (s == "&plusm")
		c = '±';
	else if (s == "&acute")
		c = '´';
	else if (s == "&micro")
		c = 'µ';
	else
		LOG("Unexpected escaped char");

	return c;
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