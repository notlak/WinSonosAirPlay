#pragma once

#include <thread>
#include <list>
#include <mutex>

class NetworkServer;

class NetworkServerConnection
{
public:
	NetworkServerConnection(NetworkServer* pServer, SOCKET socket, SOCKADDR_IN& remoteAddr);

	virtual ~NetworkServerConnection();

	bool Initialise();

	bool Close();

	virtual void Transmit(const char* buff, int len);

	class TransmitBuffer
	{
	public:
		TransmitBuffer() : pData(nullptr), len(0) {}
		TransmitBuffer(const char* pTxData, int txLen)
		{
			pData = new char[len];
			memcpy(pData, pTxData, len);
			len = txLen;
		}

		char* pData;
		int len;
	};

protected:

	void ReadThread();
	void TransmitThread();

	NetworkServer* _pServer;
	SOCKET _socket;
	SOCKADDR_IN _remoteAddr;
	bool _closeConnection;
	std::thread* _pReadThread;
	std::thread* _pTransmitThread;
	std::mutex _transmitMutex;
	std::condition_variable _transmitCv;
	std::list<TransmitBuffer*> _txQueue;
	static const int RxBuffSize = 4096;
	char* _pRxBuff;
	int _nRxBytes;
};

class NetworkServer
{
public:
	NetworkServer();
	virtual ~NetworkServer();

	bool StartListening(const char* ip, int port);

	virtual void OnReceive(NetworkServerConnection& connection, char* buff, int len) {}

protected:

	void ListeningThread();

	std::list<NetworkServerConnection*> _connectionList;
	SOCKET _listeningSocket;
	std::thread* _pListeningThread;
	bool _stopServer;
};

