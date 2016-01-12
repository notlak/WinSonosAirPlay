
// WinAirSonos.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "WinAirSonos.h"
#include "WinAirSonosDlg.h"
#include "AirPlayRTSPServer.h"
#include "SonosInterface.h"
#include "RtspServer.h"

// live555 (RTSP/RTP server) includes
#include <BasicUsageEnvironment.hh>
#include <liveMedia.hh>
#include <RTSPCommon.hh> // for dateheader(
#include <Base64.hh>

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


/* Now in AirPlayRTSPServer.h|cpp
class AirPlayRtspServer : public RTSPServer
{
public:

	virtual ~AirPlayRtspServer() {}

	static AirPlayRtspServer* createNew(UsageEnvironment& env, Port ourPort,
		UserAuthenticationDatabase* authDatabase = nullptr,
		unsigned reclamationSeconds = 65) {
		int ourSocket = setUpOurSocket(env, ourPort);
		if (ourSocket == -1) return NULL;

		return new AirPlayRtspServer(env, ourSocket, ourPort, authDatabase, reclamationSeconds);
	}

	virtual char const* allowedCommandNames() { return "ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER, POST, GET"; }
	
	class AirPlayRtspClientConnection : public RTSPClientConnection
	{
	public:

		AirPlayRtspClientConnection(AirPlayRtspServer& ourServer, int clientSocket, struct sockaddr_in clientAddr)
			: RTSPClientConnection(ourServer, clientSocket, clientAddr) {}

		virtual void handleCmd_OPTIONS()
		{
			std::string request((char*)fRequestBuffer);

			unsigned char rawAppleResp[32];
			int nRawAppleResp = 0;
			memset(rawAppleResp, 0, 32);

			size_t challengePos = request.find("Apple-Challenge:", 0);

			if (challengePos != std::string::npos)
			{
				challengePos += 17; // "Apple-Challenge: "

				size_t length = request.find('\r', challengePos);

				length -= challengePos;

				std::string challengeB64 = request.substr(challengePos, length);
				
				size_t chLen = 0;
				unsigned char* pChallenge = nullptr;

				Base64Decode(challengeB64.c_str(), &pChallenge, &chLen);

				memcpy(rawAppleResp, pChallenge, chLen);
				delete[] pChallenge;
				nRawAppleResp = chLen;

				SOCKADDR_IN sockName;
				int sockNameLen = sizeof(sockName);
				getsockname(fOurSocket, (SOCKADDR*)&sockName, &sockNameLen);

				// copy IP address into apple response
				rawAppleResp[nRawAppleResp++] = sockName.sin_addr.S_un.S_un_b.s_b1;
				rawAppleResp[nRawAppleResp++] = sockName.sin_addr.S_un.S_un_b.s_b2;
				rawAppleResp[nRawAppleResp++] = sockName.sin_addr.S_un.S_un_b.s_b3;
				rawAppleResp[nRawAppleResp++] = sockName.sin_addr.S_un.S_un_b.s_b4;

				// copy mac address in, think this can be same random one used in mDNS advertisement

				rawAppleResp[nRawAppleResp++] = 0x11;
				rawAppleResp[nRawAppleResp++] = 0x22;
				rawAppleResp[nRawAppleResp++] = 0x33;
				rawAppleResp[nRawAppleResp++] = 0x44;
				rawAppleResp[nRawAppleResp++] = 0x55;
				rawAppleResp[nRawAppleResp++] = 0x66;
				rawAppleResp[nRawAppleResp++] = 0x77;
				rawAppleResp[nRawAppleResp++] = 0x88;

				// load the AirPort Express Private key

				FILE* fKey = fopen("private.key", "rb");

				//EVP_PKEY* privateKey = nullptr;
				//PEM_read_PrivateKey(fKey, &privateKey, nullptr, nullptr);

				RSA* privateKey = nullptr;
				PEM_read_RSAPrivateKey(fKey, &privateKey, nullptr, nullptr);

				fclose(fKey);

				// encrypt the apple response
				unsigned char encAppleResp[1024];

				int encLen = RSA_private_encrypt(32, rawAppleResp, encAppleResp, privateKey, RSA_PKCS1_PADDING);

				free(privateKey);

				char* b64AppleResp = new char[2048];

				Base64Encode(encAppleResp, encLen, b64AppleResp);

				TRACE("%s\n", b64AppleResp);

				snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
					"RTSP/1.0 200 OK\r\nCSeq: %s\r\nApple-Response: %s\r\n\r\n",
					//fCurrentCSeq, dateHeader(), fOurRTSPServer.allowedCommandNames());
					fCurrentCSeq, b64AppleResp);

				delete[] b64AppleResp;
			}
			else
			{
				snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
					"RTSP/1.0 200 OK\r\nCSeq: %s\r\n\r\n",
					//fCurrentCSeq, dateHeader(), fOurRTSPServer.allowedCommandNames());
					fCurrentCSeq);
			}

		
		}




	};

protected:

	// called only by createNew();
	AirPlayRtspServer(UsageEnvironment& env,
		int ourSocket, Port ourPort,
		UserAuthenticationDatabase* authDatabase,
		unsigned reclamationSeconds) :
		RTSPServer(env, ourSocket, ourPort, authDatabase, reclamationSeconds) {}

	GenericMediaServer::ClientConnection*
		createNewClientConnection(int clientSocket, struct sockaddr_in clientAddr) {
		return new AirPlayRtspClientConnection(*this, clientSocket, clientAddr);
	}
};


static void announceStream(RTSPServer* rtspServer, ServerMediaSession* sms,
	char const* streamName, char const* inputFileName) {
	char* url = rtspServer->rtspURL(sms);
	UsageEnvironment& env = rtspServer->envir();
	env << "\n\"" << streamName << "\" stream, from the file \""
		<< inputFileName << "\"\n";
	env << "Play this stream using the URL \"" << url << "\"\n";
	delete[] url;
}

*/


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

	

	const int RTSP_PORT = 50001;
	/* WAS THIS
	// start an RTSP server
	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* rtspEnv = BasicUsageEnvironment::createNew(*scheduler);

	AirPlayRTSPServer* rtspServer = AirPlayRTSPServer::createNew(*rtspEnv, RTSP_PORT); //default port to 50,001 for now AuthDB = nullptr
	if (rtspServer == nullptr) {
		TRACE("Failed to create RTSP server: %s\n", rtspEnv->getResultMsg());
		// ### graceful exit
	}
	END */

	//----------------------------- MP3 stream test --------------------------------

//	{
//		char const* descriptionString = "Session streamed by WinAirSonos";
//		char const* streamName = "mp3AudioTest";
//		char const* inputFileName = "test.mp3";
//
//		Boolean reuseFirstSource = False;
//
//		ServerMediaSession* sms
//			= ServerMediaSession::createNew(*rtspEnv, streamName, streamName,
//				descriptionString);
//		Boolean useADUs = False;
//
//#ifdef STREAM_USING_ADUS
//		useADUs = True;
//#ifdef INTERLEAVE_ADUS
//		unsigned char interleaveCycle[] = { 0,2,1,3 }; // or choose your own...
//		unsigned const interleaveCycleSize
//			= (sizeof interleaveCycle) / (sizeof(unsigned char));
//		interleaving = new Interleaving(interleaveCycleSize, interleaveCycle);
//#endif
//#endif
//		sms->addSubsession(MP3AudioFileServerMediaSubsession
//			::createNew(*rtspEnv, inputFileName, reuseFirstSource,
//				useADUs, nullptr));
//		rtspServer->addServerMediaSession(sms);
//
//		announceStream(rtspServer, sms, streamName, inputFileName);
//	}


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

	DNSServiceErrorType err = DNSServiceRegister(&sdRef, 0 /*flags*/, 0 /*interface index*/, "1122334455667788@Room", "_raop._tcp", nullptr, nullptr, htons(RTSP_PORT), TXTRecordGetLength(&txtRef), TXTRecordGetBytesPtr(&txtRef),
		DNSServiceRegisterCallback, nullptr);

	if (err != kDNSServiceErr_NoError)
		TRACE("Error: unable to register service with mdns\n");
	
	TXTRecordDeallocate(&txtRef);

	RtspServer airPlayServer;

	airPlayServer.StartListening(nullptr, RTSP_PORT);

	// test SonosInterface
	//SonosInterface sonos;
	//sonos.Init();

	//sonos.FindSpeakers();

	//sonos.Test();

	// test HttpRequest
	/*std::string doc;
	bool ok = sonos.HttpRequest("95.211.70.200", 80, "/tools/website-speed-test", doc);*/


	//>>>>>>>>>>>> rtspEnv->taskScheduler().doEventLoop();

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

