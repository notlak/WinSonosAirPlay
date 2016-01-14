
// WinAirSonos.h : main header file for the PROJECT_NAME application
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'stdafx.h' before including this file for PCH"
#endif

#include "resource.h"		// main symbols

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

class CWinAirSonosApp : public CWinApp, public SonosInterfaceClient
{
public:
	CWinAirSonosApp();
	~CWinAirSonosApp();

// Overrides
public:
	virtual BOOL InitInstance();

	virtual void OnNewDevice(const SonosDevice& dev);
	virtual void OnDeviceRemoved(const SonosDevice& dev);

// Implementation

	DECLARE_MESSAGE_MAP()

protected:

	void InitmDNS();
	void AdvertiseServer(std::string name, int port);

	std::map<std::string, RtspServer*> _airplayServerMap;
	std::map<std::string, DNSServiceRef> _sdRefMap;

	TXTRecordRef _txtRef; //  for mDNS advertiser
	static const int TXTBuffLen = 1024;
	char _txtBuff[TXTBuffLen];
};

extern CWinAirSonosApp theApp;