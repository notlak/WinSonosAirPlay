
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

class RtspServer;

// CWinAirSonosApp:
// See WinAirSonos.cpp for the implementation of this class
//

class CWinAirSonosApp : public CWinApp, public SonosInterfaceClient
{
public:
	CWinAirSonosApp();

// Overrides
public:
	virtual BOOL InitInstance();

	virtual void OnNewDevice(const SonosDevice& dev);
	virtual void OnDeviceRemoved(const SonosDevice& dev);

// Implementation

	DECLARE_MESSAGE_MAP()

protected:

	std::map<std::string, RtspServer*> _airplayServerMap;
};

extern CWinAirSonosApp theApp;