#pragma once
#include "NetworkServer.h"
#include "UdpSocket.h"
#include "Transcoder.h"

#include <ALACAudioTypes.h>

#include <openssl\rsa.h>

#include <thread>

struct MetaData;

class RtspServerConnection : public NetworkServerConnection
{
public:

	RtspServerConnection(NetworkServerInterface* pServerInterface, SOCKET socket, SOCKADDR_IN& remoteAddr);
	~RtspServerConnection();

	virtual bool Close();

	void AudioThread();
	bool DecryptAudio(unsigned char* pEncBytes, int len, unsigned short& seq);
	bool RequestRetransmit(unsigned short seq, unsigned short missedSeq, unsigned short nMissed);
	bool SendUdpPacket(IN_ADDR* pAddr, int port, unsigned char* pData, int len);

	unsigned char _aesKey[16]; // 128 bit AES CBC key for audio decrypt
	unsigned char _aesIv[16];

	std::string _airplayDevice; // from sdp 'i=' field

	ALACSpecificConfig _alacConfig;

	CUdpSocket* _pAudioSocket;
	CUdpSocket* _pControlSocket;
	CUdpSocket* _pTimingSocket;

	std::thread* _pAudioThread;
	bool _stopAudioThread;

	CTranscoder _transcoder;

	int _streamId;
	int _txControlPort; // UDP port on apple device to send retransmit requests
};

class RtspServer : public NetworkServer<RtspServerConnection>
{
public:
	RtspServer(const std::string& sonosUdn, unsigned char* pMac);
	virtual ~RtspServer();

	std::string GetAssociatedSonos() { return _sonosUdn; }

	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request);

protected:

	bool LoadAirPortExpressKey();

	void HandleOptions(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleAnnounce(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleSetup(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleRecord(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleFlush(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleGetParameter(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleSetParameter(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleTeardown(NetworkServerConnection& connection, NetworkRequest& request);
	
	void ParseDmap(unsigned char* pData, int len, MetaData& meta);
	unsigned char* FindContentCode(const char* pCode, unsigned char* pData, int len);

	RSA* _airPortExpressKey;
	std::string _sonosUdn;
	int _sonosStreamId; // there is one RTSP server per Sonos device (actually group co-ordinator)
	                  // a stream id is assigned here to that Sonos device which will be used
	                  // in the shared StreamingServer
	unsigned char _mac[8];
};

