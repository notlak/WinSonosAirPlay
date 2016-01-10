#include "stdafx.h"
#include "NetworkServer.h"

#include <WinSock2.h>
#include <WS2tcpip.h>


NetworkServer::NetworkServer()
	:_pListeningThread(nullptr), _stopServer(false), _listeningSocket(INVALID_SOCKET)
{
}

NetworkServer::~NetworkServer()
{
	_stopServer = true;

	if (_listeningSocket != INVALID_SOCKET)
		closesocket(_listeningSocket);

	_pListeningThread->join();
	delete _pListeningThread;

	while (_connectionList.size() > 0)
	{
		NetworkServerConnection* pCon = _connectionList.front();
		_connectionList.pop_front();

		pCon->Close();
		delete pCon;
	}
}

bool NetworkServer::StartListening(const char* ip, int port)
{
	sockaddr_in addr;
	
	_listeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (INVALID_SOCKET == _listeningSocket)
		return false;

	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)port);

	if (ip == nullptr || ip[0] == '\0')
		addr.sin_addr.S_un.S_addr = INADDR_ANY;
	else
		inet_pton(AF_INET, ip, &addr); // ### todo IPv6 support assume IPv4 for now
		
	if (bind(_listeningSocket, (sockaddr*)&addr, sizeof addr))
	{
		closesocket(_listeningSocket);
		_listeningSocket = INVALID_SOCKET;
		return false;
	}

	if (listen(_listeningSocket, SOMAXCONN))
	{
		closesocket(_listeningSocket);
		_listeningSocket = INVALID_SOCKET;
		return false;
	}


	// start a thread to accept connections

	_pListeningThread = new std::thread(&NetworkServer::ListeningThread, this);

	return true;
}

void NetworkServer::ListeningThread()
{
	SOCKADDR_IN remoteSockAddr;
	int remoteSockAddrLen = sizeof remoteSockAddr;

	while (!_stopServer)
	{
		SOCKET connectionSocket = accept(_listeningSocket, (sockaddr*)&remoteSockAddr, &remoteSockAddrLen);

		if (INVALID_SOCKET != connectionSocket)
		{
			// create new NetworkServerConnection

			NetworkServerConnection* pConnection = 
				new NetworkServerConnection(this, connectionSocket, remoteSockAddr);

			pConnection->Initialise();
		}
	}
}


///////////////////////////////////////////////////////////////////////////////
// NetworkServerConnection implementation
///////////////////////////////////////////////////////////////////////////////

NetworkServerConnection::NetworkServerConnection(NetworkServer* pServer, SOCKET socket, SOCKADDR_IN& remoteAddr)
	: _pServer(pServer),
	_socket(socket),
	_remoteAddr(remoteAddr),
	_closeConnection(false),
	_pReadThread(nullptr),
	_pTransmitThread(nullptr),
	_nRxBytes(0)
{
	_pRxBuff = new char[RxBuffSize];
}

NetworkServerConnection::~NetworkServerConnection()
{
	while (_txQueue.size() > 0)
	{
		delete _txQueue.front();
		_txQueue.pop_front();
	}

	delete[] _pRxBuff;
}

bool NetworkServerConnection::Initialise()
{
	// start the threads

	_pReadThread = new std::thread(&NetworkServerConnection::ReadThread, this);
	_pTransmitThread = new std::thread(&NetworkServerConnection::TransmitThread, this);

	return true;
}

bool NetworkServerConnection::Close()
{
	_closeConnection = true;

	if (INVALID_SOCKET != _socket)
		closesocket(_socket);

	// wait for threads to terminate

	if (_pReadThread)
		_pReadThread->join();

	if (_pTransmitThread)
		_pTransmitThread->join();

	return true;
}

void NetworkServerConnection::ReadThread()
{
	const int BuffLen = 8192;
	char buff[BuffLen];

	int nBytes = 0;

	while (!_closeConnection)
	{
		if ((nBytes = recv(_socket, buff, BuffLen, 0) > 0))
		{
			if (_nRxBytes == 0)
			{
				for (int i = nBytes - 4 - 1; i >= 0; i++)
				{
					if (buff[i] == '\r' && buff[i] == '\n' && buff[i] == '\r' && buff[i] == '\n')
					{
						// we should have the whole header

					}
				}
			}


			_pServer->OnReceive(*this, buff, nBytes);
		}
	}
}

void NetworkServerConnection::Transmit(const char* pBuff, int len)
{
	TransmitBuffer* pTxBuff = new TransmitBuffer(pBuff, len);

	std::unique_lock<std::mutex> lock(_transmitMutex);

	_txQueue.push_back(pTxBuff);

	_transmitCv.notify_all();
}

void NetworkServerConnection::TransmitThread()
{
	std::unique_lock<std::mutex> lock(_transmitMutex);

	lock.unlock();

	while (!_closeConnection)
	{
		lock.lock();
		_transmitCv.wait(lock);
		
		while (_txQueue.size() > 0)
		{
			TransmitBuffer* pTxBuff = _txQueue.front();
tx:
			char* ptr = pTxBuff->pData;
			int nBytes = send(_socket, ptr, pTxBuff->len, 0);

			if (SOCKET_ERROR == nBytes)
				TRACE("Error: unable to transmit data\n");
			else if (nBytes < pTxBuff->len)
			{
				pTxBuff->len -= nBytes;
				ptr += nBytes;
				goto tx;
			}

			_txQueue.pop_front();
			delete pTxBuff;
		}
	}
}