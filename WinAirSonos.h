
// WinAirSonos.h : main header file for the PROJECT_NAME application
//

#pragma once

#include "SonosInterface.h"
#include <map>
#include <string>
#include <mutex>

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

	void ReadvertiseServers();

	virtual void OnNewDevice(const SonosDevice& dev);
	virtual void OnDeviceRemoved(const SonosDevice& dev);
	virtual void OnDeviceAddressChanged(const SonosDevice& dev);
	virtual void OnDeviceNameChanged(const SonosDevice& dev, const std::string& oldName);
	virtual void OnDeviceCoordinatorStatusChanged(const SonosDevice& dev);

protected:

	void InitmDNS();
	void AdvertiseServer(std::string name, int port, unsigned char* pMac);
	void GenerateMac(const std::string& name, unsigned char* pMac);

	std::map<std::string, RtspServer*> _airplayServerMap;
	std::map<std::string, DNSServiceRef> _sdRefMap;

	std::mutex _sdRefMapMutex;
	std::mutex _airplayServerMapMutex;

	TXTRecordRef _txtRef; //  for mDNS advertiser
	static const int TXTBuffLen = 1024;
	char _txtBuff[TXTBuffLen];
};
