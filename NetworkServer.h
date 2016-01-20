#pragma once

#include <WS2tcpip.h>

#include <thread>
#include <list>
#include <map>
#include <mutex>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


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

	bool Initialise(int connectionId);

	virtual bool Close();

	void GetIpAddress(unsigned char* pIpAddress); // ip address of this server
	std::string GetIpAddress();

	int GetId() { return _id; }

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
	int _rxBuffSize;
	char* _pRxBuff;
	int _nRxBytes;
	int _id;

	NetworkRequest _networkRequest;
};

///////////////////////////////////////////////////////////////////////////////
// NetworkServerInterface
///////////////////////////////////////////////////////////////////////////////

class NetworkServerInterface // this is used to get over the problem of having a templated NetworkServer called from the Connection class
{
public:
	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request) = 0;
	virtual void RemoveConnection(int connectionId) = 0;

};

///////////////////////////////////////////////////////////////////////////////
// NetworkServer
///////////////////////////////////////////////////////////////////////////////

template<class ConnectionType>
class NetworkServer : public NetworkServerInterface
{
public:
	NetworkServer() :_pListeningThread(nullptr), _stopServer(false), _listeningSocket(INVALID_SOCKET), _nextConnectionId(0) {}

	virtual ~NetworkServer()
	{
		_stopServer = true;

		if (_listeningSocket != INVALID_SOCKET)
			closesocket(_listeningSocket);

		_pListeningThread->join();
		delete _pListeningThread;

		// close all the connections

		std::lock_guard<std::mutex> lock(_connectionListMutex);

		while (_connectionList.size() > 0)
		{
			ConnectionType* pConn = dynamic_cast<ConnectionType*>(_connectionList.front());
			_connectionList.pop_front();

			pConn->Close();
			delete pConn;
		}
	}

	bool StartListening(const char* ip, int port)
	{
		// store the port number in case we need it
		_port = port;

		sockaddr_in addr;

		_listeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (INVALID_SOCKET == _listeningSocket)
		{
			int err = WSAGetLastError();
			TRACE("Error: unable to start listening, error %d\n", err);
			return false;
		}

		addr.sin_family = AF_INET;
		addr.sin_port = htons((unsigned short)port);

		if (ip == nullptr || ip[0] == '\0')
			addr.sin_addr.S_un.S_addr = INADDR_ANY;
		else
			inet_pton(AF_INET, ip, &addr.sin_addr); // ### todo IPv6 support assume IPv4 for now

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

	// from NetworkServerInterface
	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request) {}
	virtual void RemoveConnection(int connectionId)
	{
		// lock the list first
		std::lock_guard<std::mutex> lock(_connectionListMutex);

		// find the right connection

		for (auto it = _connectionList.begin(); it != _connectionList.end(); ++it)
		{
			if ((*it)->GetId() == connectionId)
			{
				NetworkServerConnection* pConn = *it;
				_connectionList.erase(it);
				pConn->Close();
				delete pConn;
				break;
			}
		}
	}

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

				ConnectionType* pConnection =
					new ConnectionType(this, connectionSocket, remoteSockAddr);

				pConnection->Initialise(_nextConnectionId);
				_nextConnectionId++;

				// lock the list first
				std::lock_guard<std::mutex> lock(_connectionListMutex);

				_connectionList.push_back(pConnection);
			}
		}
	}

	int _nextConnectionId;

	std::list<NetworkServerConnection*> _connectionList;
	std::mutex _connectionListMutex;

	SOCKET _listeningSocket;
	std::thread* _pListeningThread;
	bool _stopServer;
	int _port;
};

