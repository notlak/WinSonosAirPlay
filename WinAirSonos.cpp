
// WinAirSonos.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "WinAirSonos.h"
#include "RtspServer.h"
#include "StreamingServer.h"
#include "Log.h"

#include <string>
#include <map>
#include <sstream>

//#ifdef _DEBUG
//#define new DEBUG_NEW
//#endif


CWinAirSonos::CWinAirSonos()
{

}


CWinAirSonos::~CWinAirSonos()
{
	// stop advertising
	for (auto it = _sdRefMap.begin(); it != _sdRefMap.end(); ++it)
	{
		DNSServiceRefDeallocate(it->second);
	}

	_sdRefMap.clear();

	// stop the airplay servers
	for (auto it = _airplayServerMap.begin(); it != _airplayServerMap.end(); ++it)
	{
		delete it->second;
	}

	_sdRefMap.clear();

	SonosInterface::Delete();

	StreamingServer::Delete();

	WSACleanup();
}

// DNSServiceRegister callback
static void DNSSD_API DNSServiceRegisterCallback(DNSServiceRef sdref, DNSServiceFlags flags, DNSServiceErrorType errorCode,
	const char *name, const char *regtype, const char *domain, void *context)
{
	(void)sdref;    // Unused
	(void)flags;    // Unused
	(void)context;  // Unused

	LOG("Got a reply for service %s.%s%s: ", name, regtype, domain);

	if (errorCode == kDNSServiceErr_NoError)
	{
		if (flags & kDNSServiceFlagsAdd)
			LOG("Name now registered and active\n");
		else
			LOG("Name registration removed\n");
	}
	else if (errorCode == kDNSServiceErr_NameConflict)
	{
		LOG("Name in use, please choose another\n");
	}
	else
		LOG("Error %d\n", errorCode);

	//if (!(flags & kDNSServiceFlagsMoreComing)) fflush(stdout);
}

void CWinAirSonos::InitmDNS()
{
	std::map<std::string, std::string> txtValuesMap = {
		{ "txtvers", "1" },
		{ "ch","2" },
		{ "cn","0,1" },
		{ "et","0,1" },
		{ "md","0" },
		{ "pw","false" },
		{ "sr","44100" },
		{ "ss","16" },
		{ "tp","TCP,UDP" },
		{ "vs","105.1" },
		{ "am","AirPort4,107" },
		{ "ek","1" },
		{ "sv","false" },
		{ "da","true" },
		{ "vn","65537" },
		{ "fv","76400.10" },
		{ "sf","0x5" }
	};

	TXTRecordCreate(&_txtRef, TXTBuffLen, _txtBuff);

	//for_each(txtValuesMap.begin, txtValuesMap.end(), []() {});

	for (const auto& kv : txtValuesMap) // C++11 niftyness
	{
		TXTRecordSetValue(&_txtRef, kv.first.c_str(), (uint8_t)kv.second.length(), kv.second.c_str());
	}

}

void CWinAirSonos::GenerateMac(const std::string& name, unsigned char* pMac)
{
	pMac[0] = 0x11;
	pMac[1] = 0x22;
	pMac[2] = 0x33;
	pMac[3] = 0x44;
	pMac[4] = 0x55;
	pMac[5] = 0x66;
	pMac[6] = 0x77;
	pMac[7] = 0x88;

	int len = name.length();
	if (len > 8) len = 8;

	for (int i = 0; i < len; i++)
		pMac[i] ^= name[i];
}

void CWinAirSonos::AdvertiseServer(std::string name, int port, unsigned char* pMac)
{
	DNSServiceRef sdRef;

	std::ostringstream s;
	char hex[3];

	for (int i = 0; i < 8; i++)
	{
		sprintf(hex, "%02X", pMac[i]);
		s << hex;
	}
	s << "@" << name;

	std::string identifier = s.str();//= "1122334455667788@" + name;

	DNSServiceErrorType err = DNSServiceRegister(&sdRef, 0 /*flags*/, 0 /*interface index*/, identifier.c_str(), "_raop._tcp", nullptr, nullptr, htons(port), TXTRecordGetLength(&_txtRef), TXTRecordGetBytesPtr(&_txtRef),
		DNSServiceRegisterCallback, nullptr);

	if (err != kDNSServiceErr_NoError)
		LOG("Error: unable to register service with mdns\n");
	else
		_sdRefMap[name] = sdRef;

	LOG("Advertising %s on port %d\n", identifier.c_str(), port);

}

void CWinAirSonos::OnNewDevice(const SonosDevice& dev)
{
	// create RtspServer

	LOG("CWinAirSonos::OnNewDevice()\n");

	unsigned char pMac[8];

	GenerateMac(dev._name, pMac);

	RtspServer* pAirPlayServer = new RtspServer(dev._udn, pMac);

	const int minPort = 50002;
	const int maxPort = 65535;

	srand(GetTickCount());

	int port = minPort + (rand() * (maxPort - minPort + 1)) / RAND_MAX;

	bool isListening = false;

	for (int i = 0; i < 5 && !isListening; i++)
	{
		if (pAirPlayServer->StartListening(nullptr, port))
		{
			isListening = true;
			LOG("Starting RtspServer on port: %d\n", port);
		}
		else
		{
			port += 1;

			if (port > maxPort)
				port = minPort;
		}
	}

	if (isListening)
	{
		_airplayServerMap[dev._name] = pAirPlayServer;

		// advertise it via mDNS

		AdvertiseServer(dev._name, port, pMac);

	}
	else
	{
		LOG("Failed to start RtspServer for %s\n", dev._name.c_str());
	}
}

void CWinAirSonos::OnDeviceRemoved(const SonosDevice& dev)
{

}

void CWinAirSonos::ReadvertiseServers()
{
	LOG("CWinAirSonos::ReadvertiseServers()\n");

	// unregister everything
	for (auto it = _sdRefMap.begin(); it != _sdRefMap.end(); ++it)
		DNSServiceRefDeallocate(it->second);

	_sdRefMap.clear();

	//### need to protect the map with a mutex if this is going to be used 
	
	// now re-advertise all the RTSP servers
	unsigned char mac[8];
	for (auto it = _airplayServerMap.begin(); it != _airplayServerMap.end(); ++it)
	{
		GenerateMac(it->first, mac);
		AdvertiseServer(it->first, it->second->GetPort(), mac);
	}
}

bool CWinAirSonos::Initialise()
{
	// advertise a particular service via mdns

	/*
	txtvers: '1',     // txt record version?
    ch: '2',          // # channels
    cn: '0,1',          // codec; 0=pcm, 1=alac, 2=aac, 3=aac elc; fwiw Sonos supports aac; pcm required for iPad+Spotify; OS X works with pcm
    et: '0,1',        // encryption; 0=none, 1=rsa, 3=fairplay, 4=mfisap, 5=fairplay2.5; need rsa for os x
    md: '0',          // metadata; 0=text, 1=artwork, 2=progress
    pw: (options.password ? 'true' : 'false'),    // password enabled
    sr: '44100',      // sampling rate (e.g. 44.1KHz)
    ss: '16',         // sample size (e.g. 16 bit?)
    tp: 'TCP,UDP',    // transport protocol
    vs: '105.1',     // server version?
    am: 'AirPort4,107',   // device model
    ek: '1',          // ? from ApEx; setting to 1 enables iTunes; seems to use ALAC regardless of 'cn' setting
    sv: 'false',    // ? from ApEx
    da: 'true',     // ? from ApEx
    vn: '65537',    // ? from ApEx; maybe rsa key modulus? happens to be the same value
    fv: '76400.10', // ? from ApEx; maybe AirPort software version (7.6.4)
    sf: '0x5'       // ? from ApEx
	*/
/*
	std::map<std::string, std::string> txtValuesMap = { 
		{"txtvers", "1"},
		{"ch","2"},
		{"cn","0,1" },
		{"et","0,1" },
		{"md","0" },
		{"pw","false" },
		{"sr","44100" },
		{"ss","16" },
		{"tp","TCP,UDP" },
		{"vs","105.1" },
		{"am","AirPort4,107" },
		{"ek","1" },
		{"sv","false" },
		{"da","true" },
		{"vn","65537" },
		{"fv","76400.10" },
		{"sf","0x5" }
	};

	TXTRecordRef txtRef;
	const int TXTBufferLen = 1024;
	char txtBuffer[TXTBufferLen];

	TXTRecordCreate(&txtRef, TXTBufferLen, txtBuffer);

	//for_each(txtValuesMap.begin, txtValuesMap.end(), []() {});

	for (const auto& kv : txtValuesMap) // C++11 niftyness
	{
		TXTRecordSetValue(&txtRef, kv.first.c_str(), (uint8_t)kv.second.length(), kv.second.c_str());
	}

	DNSServiceRef sdRef;

	DNSServiceErrorType err = DNSServiceRegister(&sdRef, 0, 0, "1122334455667788@Room", "_raop._tcp", nullptr, nullptr, htons(RTSP_PORT), TXTRecordGetLength(&txtRef), TXTRecordGetBytesPtr(&txtRef),
		DNSServiceRegisterCallback, nullptr);

	if (err != kDNSServiceErr_NoError)
		LOG("Error: unable to register service with mdns\n");
	
	TXTRecordDeallocate(&txtRef);

*/
	const int MP3_PORT = 50001;
	const int RTSP_PORT = 50002;

	WSADATA wsaData;
	WORD wVersionRequested;
	wVersionRequested = MAKEWORD(2, 2);
	WSAStartup(wVersionRequested, &wsaData);

	InitmDNS();

	SonosInterface* pSonosInterface = SonosInterface::GetInstance();
	pSonosInterface->RegisterClient(this);
	pSonosInterface->Init();

	// need to initialise WSA stuff in this thread

	StreamingServer* pStreamingServer = StreamingServer::GetStreamingServer();
	bool isListening = false;
	int port = MP3_PORT;

	for (int i = 0; i < 5 && !isListening; i++)
	{
		isListening = pStreamingServer->StartListening(nullptr, port);

		if (!isListening)
			port++;
	}

	if (isListening)
		LOG("Started StreamingServer on port: %d\n", port);
	else
		LOG("Error: unable to start stream server\n");

	return true;
}

void CWinAirSonos::Shutdown()
{

}

#include <conio.h>

int main()
{
	CWinAirSonos winAirSonos;

	winAirSonos.Initialise();

	// main loop

	bool quit = false;

	while (!quit)
	{
		Sleep(200);
		if (_kbhit())
		{
			int c = _getch();
			if (c == 'r' || c == 'R')
				winAirSonos.ReadvertiseServers();
			else if (c == 'q' || c == 'Q')
				quit = true;
		}
		
	}

	winAirSonos.Shutdown();

	return 0;
}

