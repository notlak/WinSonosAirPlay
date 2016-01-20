
// WinAirSonos.h : main header file for the PROJECT_NAME application
//

#pragma once

#include "SonosInterface.h"
#include <map>
#include <string>

// mdns/bonjour stuff
#include "c:\program files\Bonjour SDK\include\dns_sd.h"

class RtspServer;
class SonosInterface;

// CWinAirSonosApp:
// See WinAirSonos.cpp for the implementation of this class
//

class CWinAirSonos: public SonosInterfaceClient
{
public:
	CWinAirSonos();
	~CWinAirSonos();

	bool Initialise();
	void Shutdown();

	virtual void OnNewDevice(const SonosDevice& dev);
	virtual void OnDeviceRemoved(const SonosDevice& dev);

protected:

	void InitmDNS();
	void AdvertiseServer(std::string name, int port);

	std::map<std::string, RtspServer*> _airplayServerMap;
	std::map<std::string, DNSServiceRef> _sdRefMap;

	TXTRecordRef _txtRef; //  for mDNS advertiser
	static const int TXTBuffLen = 1024;
	char _txtBuff[TXTBuffLen];
};
