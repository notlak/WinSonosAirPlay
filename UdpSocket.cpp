#include "stdafx.h"
#include "UdpSocket.h"
#include <WinSock2.h>
#include <comutil.h>
#include <ws2ipdef.h>

#include "Log.h"

//#ifdef _DEBUG
//#define new DEBUG_NEW
//#endif

CUdpSocket::CUdpSocket()
	:_socketListening(false), _socket(-1), _port(-1)
{
}


CUdpSocket::~CUdpSocket()
{
	if (_socket > -1)
	{
		LOG("Closing UDP port %d\n", _port);
		shutdown(_socket, SD_BOTH);
		closesocket(_socket);
	}
}


bool CUdpSocket::Initialise(int port)
{
	/*
	const int MinPort = 49152;
	const int MaxPort = 65535;

	if (port == -1) // generate random port
	{
		LARGE_INTEGER perfCount;
		QueryPerformanceCounter(&perfCount);

		srand(perfCount.LowPart + rand());

		port = MinPort + (rand() * (MaxPort - MinPort)) / RAND_MAX;
	}
	*/

	if (port == -1)
		port = GenerateRandomPort();

	// create the socket
	_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (_socket < 0)
		return false;

	// now bind it

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	for (int i = 0; i < 5; i++)
	{
		addr.sin_port = htons(port);

		if (bind(_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
		{
			LOG("Failed to create UDP socket on port: %d\n", port);
			port = port + 1;
			if (port > MaxPort) 
				port = MinPort;
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

bool CUdpSocket::InitialiseBroadcastRx(int port)
{
	bool ok = false;

	// create the socket
	_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (_socket < 0)
		return false;

	if (port == -1)
		port = GenerateRandomPort();

	// now bind it

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	BOOL optVal = TRUE;

	if (setsockopt(_socket, SOL_SOCKET, SO_BROADCAST, (char*)&optVal, sizeof(BOOL)) < 0)
	{
		LOG("Failed to set UDP socket options on port: %d\n", port);
		closesocket(_socket);
		_socket = -1;
		return false;
	}

	for (int i = 0; i < 5; i++)
	{
		addr.sin_port = htons(port);

		if (bind(_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
		{
			port++;

			if (port > MaxPort)
				port = MinPort;

		}
		else
		{
			_port = port;
			_socketListening = true;

			unsigned long arg = 1;
			ioctlsocket(_socket, FIONBIO, &arg);
			ok = true;
			break;
		}

	}

	if (!ok)
	{
		LOG("Failed to bind BROADCAST UDP socket after 5 attempts");
		closesocket(_socket);
		_socket = -1;
		return false;
	}

	return ok;
}

bool CUdpSocket::InitialiseBroadcastTx()
{
	// create the socket
	_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (_socket < 0)
		return false;

	BOOL optVal = TRUE;

	if (setsockopt(_socket, SOL_SOCKET, SO_BROADCAST, (char*)&optVal, sizeof(BOOL)) < 0)
	{
		LOG("Failed to set UDP socket options\n");
		closesocket(_socket);
		_socket = -1;
		return false;
	}

	return true;
}

bool CUdpSocket::InitialiseMulticastRx(int port)
{
	// create the socket
	_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (_socket < 0)
		return false;

	// now bind it

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	addr.sin_port = htons(port);

	BOOL optVal = TRUE;

	IP_MREQ mreq;
	mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq)) < 0) {
		LOG("Failed to set socket options on for MULTICAST UDP socket on port: %d\n", port);
		closesocket(_socket);
		_socket = -1;
		return false;
	}

	if (bind(_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
	{
		LOG("Failed to create BROADCAST UDP socket on port: %d\n", port);
		closesocket(_socket);
		_socket = -1;
		return false;
	}
	else
	{
		_port = port;
		_socketListening = true;

		unsigned long arg = 1;
		ioctlsocket(_socket, FIONBIO, &arg);
	}

	return true;
}

int CUdpSocket::Read(char* pBuffer, int nBuff)
{
	return recvfrom(_socket, pBuffer, nBuff, 0, nullptr, nullptr);
}

bool CUdpSocket::Broadcast(int port, char* pBuffer, int nBuff)
{
	if (_socket == -1)
		return false;

	struct sockaddr_in s;
	memset(&s, 0, sizeof(struct sockaddr_in));
	s.sin_family = AF_INET;
	s.sin_port = htons(1900);
	s.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	if (sendto(_socket, pBuffer, nBuff, 0, (struct sockaddr *)&s, sizeof(struct sockaddr_in)) < 0)
	{
		LOG("CUdpSocket::Broadcast() failed\n");
		return false;
	}

	return true;
}

bool CUdpSocket::Multicast(int port, char* pBuffer, int nBuff)
{
	if (_socket == -1)
		return false;

	struct sockaddr_in s;
	memset(&s, 0, sizeof(struct sockaddr_in));
	s.sin_family = AF_INET;
	s.sin_port = htons(1900);
	s.sin_addr.s_addr = inet_addr("239.255.255.250");

	if (sendto(_socket, pBuffer, nBuff, 0, (struct sockaddr *)&s, sizeof(struct sockaddr_in)) < 0)
	{
		LOG("CUdpSocket::Multicast() failed\n");
		return false;
	}

	return true;
}

int CUdpSocket::GenerateRandomPort()
{
	int port = 0;

	LARGE_INTEGER perfCount;
	QueryPerformanceCounter(&perfCount);

	srand(perfCount.LowPart + rand());

	port = MinPort + (rand() * (MaxPort - MinPort)) / RAND_MAX;

	return port;
}