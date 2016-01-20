#pragma once

class CUdpSocket
{
public:
	CUdpSocket();
	virtual ~CUdpSocket();

	virtual bool Initialise(int port = -1);
	virtual int Read(char* buffer, int nBuff);

	virtual int GetPort() { return _port; }

protected:
	int _socket;
	int _port;
	bool _socketListening;
};

