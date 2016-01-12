#pragma once

#include <thread>
#include <list>
#include <map>
#include <mutex>

///////////////////////////////////////////////////////////////////////////////
// NetworkRequest
///////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////
// NetworkResponse
///////////////////////////////////////////////////////////////////////////////

class NetworkResponse
{
public:
	NetworkResponse(const char* proto, int statusCode, const char* reasonPhrase);
	virtual ~NetworkResponse();

	void AddHeaderField(const char* name, const char* value);
	void AddContent(const char* pData, int len);

	std::string GetHeader() const;

	std::string protocol;
	int responseCode;
	std::string reason;

	std::map<std::string, std::string> headerFieldMap;
	int contentLength;
	char* pContent;
};

///////////////////////////////////////////////////////////////////////////////
// NetworkServerConnection
///////////////////////////////////////////////////////////////////////////////
class NetworkServerInterface;

class NetworkServerConnection
{
public:
	NetworkServerConnection(NetworkServerInterface* pServerInterface, SOCKET socket, SOCKADDR_IN& remoteAddr);

	virtual ~NetworkServerConnection();

	bool Initialise();

	bool Close();

	void GetIpAddress(unsigned char* pIpAddress); // ip address of this server

	virtual void Transmit(const char* buff, int len);
	virtual void SendResponse(const NetworkResponse& response);

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

	NetworkServerInterface* _pServer;
	SOCKET _socket;
	SOCKADDR_IN _remoteAddr;
	bool _closeConnection;
	std::thread* _pReadThread;
	std::thread* _pTransmitThread;
	std::mutex _transmitMutex;
	std::condition_variable _transmitCv;
	std::list<TransmitBuffer*> _txQueue;
	//static const int RxBuffSize = 8192;
	int _rxBuffSize;
	char* _pRxBuff;
	int _nRxBytes;
	NetworkRequest _networkRequest;
};

///////////////////////////////////////////////////////////////////////////////
// NetworkServerInterface
///////////////////////////////////////////////////////////////////////////////

class NetworkServerInterface // this is used to get over the problem of having a templated NetworkServer called from the Connection class
{
public:
	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request) = 0;

};

///////////////////////////////////////////////////////////////////////////////
// NetworkServer
///////////////////////////////////////////////////////////////////////////////

template<class ConnectionType>
class NetworkServer : public NetworkServerInterface
{
public:
	NetworkServer() :_pListeningThread(nullptr), _stopServer(false), _listeningSocket(INVALID_SOCKET) {}

	virtual ~NetworkServer()
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

	bool StartListening(const char* ip, int port)
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

	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request) {}

protected:

	void ListeningThread()
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

				_connectionList.push_back(pConnection);
			}
		}
	}

	std::list<NetworkServerConnection*> _connectionList;
	SOCKET _listeningSocket;
	std::thread* _pListeningThread;
	bool _stopServer;
};

