#include "stdafx.h"
#include "UdpSocket.h"


CUdpSocket::CUdpSocket()
	:_socketListening(false), _socket(-1), _port(-1)
{
}


CUdpSocket::~CUdpSocket()
{
	if (_socketListening)
		closesocket(_socket);
}


bool CUdpSocket::Initialise(int port)
{
	if (port == -1) // generate random port
	{
		const int MinPort = 49152;
		const int MaxPort = 65535;

		port = MinPort + (rand() * (MaxPort - MinPort)) / RAND_MAX;
	}

	// create the socket
	_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (_socket < 0)
		return false;

	// now bind it

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	for (int i = 0; i < 3; i++)
	{
		addr.sin_port = htons(port);

		if (bind(_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
		{
			port = (port + 1) % 65536;
		}
		else
		{
			_port = port;
			_socketListening = true;

			unsigned long arg = 1;
			ioctlsocket(_socket, FIONBIO, &arg);

			break;
		}
	}

	return true;
}

int CUdpSocket::Read(char* pBuffer, int nBuff)
{
	return recvfrom(_socket, pBuffer, nBuff, 0, nullptr, nullptr);
}
