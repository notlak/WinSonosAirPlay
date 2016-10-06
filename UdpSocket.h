#pragma once

class CUdpSocket
{
public:
	CUdpSocket();
	virtual ~CUdpSocket();

	virtual bool Initialise(int port = -1);
	virtual bool InitialiseBroadcastRx(int port);
	virtual bool InitialiseBroadcastTx();
	virtual bool InitialiseMulticastRx(int port);

	virtual int Read(char* buffer, int nBuff);
	virtual bool Broadcast(int port, char* pBuffer, int nBuff);
	virtual bool Multicast(int port, char* pBuffer, int nBuff);

	virtual int GetPort() { return _port; }

protected:

	int GenerateRandomPort();

	static const int MinPort = 49152;
	static const int MaxPort = 65535;

	int _socket;
	int _port;
	bool _socketListening;
};

