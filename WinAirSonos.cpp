
// WinAirSonos.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "WinAirSonos.h"
#include "WinAirSonosDlg.h"
#include "RtspServer.h"
#include "StreamingServer.h"

//#include "AirPlayRTSPServer.h"
// live555 (RTSP/RTP server) includes
//#include <BasicUsageEnvironment.hh>
//#include <liveMedia.hh>
//#include <RTSPCommon.hh> // for dateheader(
//#include <Base64.hh>

#include <string>
#include <map>


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CWinAirSonosApp

BEGIN_MESSAGE_MAP(CWinAirSonosApp, CWinApp)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()


// CWinAirSonosApp construction

CWinAirSonosApp::CWinAirSonosApp()
{
	// TODO: add construction code here, 
	// Place all significant initialization in InitInstance
}


CWinAirSonosApp::~CWinAirSonosApp()
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

	WSACleanup();
}


// The one and only CWinAirSonosApp object

CWinAirSonosApp theApp;


// DNSServiceRegister callback
static void DNSSD_API DNSServiceRegisterCallback(DNSServiceRef sdref, const DNSServiceFlags flags, DNSServiceErrorType errorCode,
	const char *name, const char *regtype, const char *domain, void *context)
{
	(void)sdref;    // Unused
	(void)flags;    // Unused
	(void)context;  // Unused

	TRACE("Got a reply for service %s.%s%s: ", name, regtype, domain);

	if (errorCode == kDNSServiceErr_NoError)
	{
		if (flags & kDNSServiceFlagsAdd)
			TRACE("Name now registered and active\n");
		else
			TRACE("Name registration removed\n");
	}
	else if (errorCode == kDNSServiceErr_NameConflict)
	{
		TRACE("Name in use, please choose another\n");
	}
	else
		TRACE("Error %d\n", errorCode);

	//if (!(flags & kDNSServiceFlagsMoreComing)) fflush(stdout);
}

void CWinAirSonosApp::InitmDNS()
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

void CWinAirSonosApp::AdvertiseServer(std::string name, int port)
{
	DNSServiceRef sdRef;

	std::string identifier = "1122334455667788@" + name;

	DNSServiceErrorType err = DNSServiceRegister(&sdRef, 0 /*flags*/, 0 /*interface index*/, identifier.c_str(), "_raop._tcp", nullptr, nullptr, htons(port), TXTRecordGetLength(&_txtRef), TXTRecordGetBytesPtr(&_txtRef),
		DNSServiceRegisterCallback, nullptr);

	if (err != kDNSServiceErr_NoError)
		TRACE("Error: unable to register service with mdns\n");
	else
		_sdRefMap[name] = sdRef;

}

void CWinAirSonosApp::OnNewDevice(const SonosDevice& dev)
{
	// create RtspServer

	RtspServer* pAirPlayServer = new RtspServer(dev._udn);

	const int minPort = 50002;
	const int maxPort = 65535;

	int port = minPort + (rand() * (maxPort - minPort + 1)) / RAND_MAX;

	bool isListening = false;

	for (int i = 0; i < 5 && !isListening; i++)
	{
		if (pAirPlayServer->StartListening(nullptr, port))
		{
			isListening = true;
			TRACE("Starting RtspServer on port: %d\n", port);
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

		AdvertiseServer(dev._name, port);

	}
	else
	{
		TRACE("Failed to start RtspServer for %s\n", dev._name.c_str());
	}
}

void CWinAirSonosApp::OnDeviceRemoved(const SonosDevice& dev)
{

}


// CWinAirSonosApp initialization

BOOL CWinAirSonosApp::InitInstance()
{
	// InitCommonControlsEx() is required on Windows XP if an application
	// manifest specifies use of ComCtl32.dll version 6 or later to enable
	// visual styles.  Otherwise, any window creation will fail.
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// Set this to include all the common control classes you want to use
	// in your application.
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();

	AfxEnableControlContainer();

	// Create the shell manager, in case the dialog contains
	// any shell tree view or shell list view controls.
	CShellManager *pShellManager = new CShellManager;

	// Activate "Windows Native" visual manager for enabling themes in MFC controls
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	// of your final executable, you should remove from the following
	// the specific initialization routines you do not need FILE_ATTRIBUTE
	// Change the registry key under which our settings are stored
	// TODO: You should modify this string to be something appropriate
	// such as the name of your company or organization
	SetRegistryKey(_T("KODR"));

	//------------------------------------------------------------------------------


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
		TRACE("Error: unable to register service with mdns\n");
	
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
		TRACE("Started StreamingServer on port: %d\n", port);
	else
		TRACE("Error: unable to start stream server\n");

	//RtspServer* pAirPlayServer = new RtspServer();
	//pAirPlayServer->StartListening(nullptr, RTSP_PORT);

	// test SonosInterface
	//SonosInterface sonos;
	//sonos.Init();
	//sonos.FindSpeakers();
	//sonos.Test();

	CWinAirSonosDlg dlg;
	m_pMainWnd = &dlg;
	INT_PTR nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with OK
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with Cancel
	}
	else if (nResponse == -1)
	{
		TRACE(traceAppMsg, 0, "Warning: dialog creation failed, so application is terminating unexpectedly.\n");
		TRACE(traceAppMsg, 0, "Warning: if you are using MFC controls on the dialog, you cannot #define _AFX_NO_MFC_CONTROLS_IN_DIALOGS.\n");
	}

	// unregister mdns stuff

	//DNSServiceRefDeallocate(sdRef);

	// kill the AirPlay (music producing end first)

	//delete pAirPlayServer;

	// kill the streaming server

	StreamingServer::Delete();

	// kill RTSP server
	// ### can't: delete rtspServer;

	// Delete the shell manager created above.
	if (pShellManager != NULL)
	{
		delete pShellManager;
	}


	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}

