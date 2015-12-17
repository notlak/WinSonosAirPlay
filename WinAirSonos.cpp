
// WinAirSonos.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "WinAirSonos.h"
#include "WinAirSonosDlg.h"

#include <string>
#include <map>

// mdns/bonjour stuff
#include "c:\program files\Bonjour SDK\include\dns_sd.h"

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
	// the specific initialization routines you do not need
	// Change the registry key under which our settings are stored
	// TODO: You should modify this string to be something appropriate
	// such as the name of your company or organization
	SetRegistryKey(_T("KODR"));

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

	DNSServiceErrorType err = DNSServiceRegister(&sdRef, 0 /*flags*/, 0 /*interface index*/, "F122334455667788@Room", "_raop._tcp", nullptr, nullptr, htons(50000), TXTRecordGetLength(&txtRef), TXTRecordGetBytesPtr(&txtRef),
		DNSServiceRegisterCallback, nullptr);

	if (err != kDNSServiceErr_NoError)
		TRACE("Error: unable to register service with mdns\n");
	
	TXTRecordDeallocate(&txtRef);

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

	DNSServiceRefDeallocate(sdRef);

	// Delete the shell manager created above.
	if (pShellManager != NULL)
	{
		delete pShellManager;
	}


	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}

