#pragma once
#include "NetworkServer.h"
#include "UdpSocket.h"
#include "Transcoder.h"

#include <ALACAudioTypes.h>

#include <openssl\rsa.h>

#include <thread>

struct MetaData;

class AudioPacket
{
public:
	AudioPacket(unsigned short seq, unsigned char* pData, int len) : _seq(seq), _pData(nullptr), _len(len) 
	{
		_timeStamp = GetTickCount();

		if (pData && len)
		{
			_pData = new unsigned char[len];
			memcpy(_pData, pData, len);
		}
	}
	~AudioPacket()
	{
		_len = 0;
		delete[] _pData;
		_pData = nullptr;
	}

	unsigned short _seq;
	unsigned char* _pData; // if null then missed packet
	int _len;
	unsigned int _timeStamp; // time at which packet (should have been) received
};

class AudioPacketBuffer
{
public:
	AudioPacketBuffer() {}
	~AudioPacketBuffer() 
	{
		while (_packetList.size() > 0)
		{
			delete _packetList.front();
			_packetList.pop_front();
		}
	}

	void AddPacket(unsigned short seq, unsigned char* pData, int len)
	{
		_packetList.push_back(new AudioPacket(seq, pData, len));
	}

	void AddEmptyPackets(unsigned short seq, int count)
	{
		for (int i = 0; i < count; i++)
			AddPacket(seq+i, nullptr, 0);
	}

	bool AddMissedPacket(unsigned short seq, unsigned char* pData, int len)
	{
		bool done = false;
		int firstSeq = -1;

		for (auto it = _packetList.begin(); it != _packetList.end(); ++it)
		{
			AudioPacket* pPacket = *it;

			if (firstSeq < 0)
				firstSeq = pPacket->_seq;

			if (pPacket->_seq == seq)
			{
				if (pPacket->_pData != nullptr)
				{
					LOG("AudioPacketBuffer::AddMissedPacket() error: body already exists");
				}
				else
				{
					pPacket->_pData = new unsigned char[len];
					memcpy(pPacket->_pData, pData, len);
					pPacket->_len = len;
					done = true;
				}
				break;
			}
		}

		//if (!done)
			//LOG("Seq not found seq:%d first:%d\n", seq, firstSeq);

		return done;
	}

	AudioPacket* GetNextPacket()
	{		
		if (_packetList.empty())
			return nullptr;

		AudioPacket* pPacket = _packetList.front();

		if (pPacket->_pData)
		{
			pPacket = _packetList.front();
			_packetList.pop_front();
		}
		else
		{
			if (GetTickCount() - pPacket->_timeStamp < 1000) // 1000ms timeout for now
			{
				pPacket = nullptr; // still waiting for missed packet retransmission
			}
			else // timed out, will have to pad with silence
			{
				_packetList.pop_front();
			}
		}

		return pPacket;
	}

	bool NextPacketMissed()
	{
		return nullptr == _packetList.front()->_pData;
	}

	std::list<AudioPacket*> _packetList;
};

class RtspServerConnection : public NetworkServerConnection
{
public:

	RtspServerConnection(NetworkServerInterface* pServerInterface, SOCKET socket, SOCKADDR_IN& remoteAddr);
	~RtspServerConnection();

	virtual bool Close();

	void AudioThread();
	bool DecryptAudio(unsigned char* pEncBytes, int len, int& seq);
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
	AudioPacketBuffer _audioPacketBuffer;
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

