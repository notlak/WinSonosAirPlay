#pragma once
#include "NetworkServer.h"
#include "UdpSocket.h"
#include "Transcoder.h"

#include <ALACAudioTypes.h>

#include <openssl\rsa.h>

#include <thread>


class RtspServerConnection : public NetworkServerConnection
{
public:

	RtspServerConnection(NetworkServerInterface* pServerInterface, SOCKET socket, SOCKADDR_IN& remoteAddr);
	~RtspServerConnection();

	void AudioThread();
	bool DecryptAudio(unsigned char* pEncBytes, int len, unsigned short& seq);

	unsigned char _aesKey[16]; // 128 bit AES CBC key for audio decrypt
	unsigned char _aesIv[16];

	ALACSpecificConfig _alacConfig;

	CUdpSocket* _pAudioSocket;
	CUdpSocket* _pControlSocket;
	CUdpSocket* _pTimingSocket;

	std::thread* _pAudioThread;
	bool _stopAudioThread;

	CTranscoder _transcoder;

	int _streamId;
};

class RtspServer :
	public NetworkServer<RtspServerConnection>
{
public:
	RtspServer(const std::string& sonosUdn);
	virtual ~RtspServer();

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
	
	void ParseDmap(unsigned char* pData, int len);

	RSA* _airPortExpressKey;
	std::string _sonosUdn;
};

