#pragma once

#include <thread>
#include <list>
#include <map>
#include <mutex>

class NetworkServer;

class NetworkRequest
{
public:
	NetworkRequest(const char* pHeader = nullptr);
	~NetworkRequest();

	bool ParseHeader(const char* pHeader);

	std::string type; // "GET" "POST" etc (uppercase)
	std::string path;
	std::string protocol; // "HTTP/1.0" "RTSP/1.0" etc
	std::map<std::string, std::string> headerFieldMap; // field names lowercase
	int contentLength;
	char* pContent;
};

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
			pData = new char[txLen];
			memcpy(pData, pTxData, txLen);
			len = txLen;
		}

		~TransmitBuffer() { delete[] pData; }

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
	NetworkRequest _networkRequest;
};

class NetworkServer
{
public:
	NetworkServer();
	virtual ~NetworkServer();

	bool StartListening(const char* ip, int port);

	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request) {}

protected:

	void ListeningThread();

	std::list<NetworkServerConnection*> _connectionList;
	SOCKET _listeningSocket;
	std::thread* _pListeningThread;
	bool _stopServer;
};

