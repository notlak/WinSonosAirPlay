#include "stdafx.h"
#include "NetworkServer.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <sstream>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

/*
template<class ConnectionType>
NetworkServer<ConnectionType>::NetworkServer()
	:_pListeningThread(nullptr), _stopServer(false), _listeningSocket(INVALID_SOCKET)
{
}
*/

/*
template<class ConnectionType>
NetworkServer<ConnectionType>::~NetworkServer()
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


template<class ConnectionType>
bool NetworkServer<ConnectionType>::StartListening(const char* ip, int port)
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

template<class ConnectionType>
void NetworkServer<ConnectionType>::ListeningThread()
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
*/

///////////////////////////////////////////////////////////////////////////////
// NetworkServerConnection implementation
///////////////////////////////////////////////////////////////////////////////

NetworkServerConnection::NetworkServerConnection(NetworkServerInterface* pServerInterface, SOCKET socket, SOCKADDR_IN& remoteAddr)
	: _pServer(pServerInterface),
	_id(-1),
	_socket(socket),
	_remoteAddr(remoteAddr),
	_closeConnection(false),
	_pReadThread(nullptr),
	_pTransmitThread(nullptr),
	_nRxBytes(0),
	_rxBuffSize(32768)
{
	//_pRxBuff = new char[RxBuffSize];
	_pRxBuff = new char[_rxBuffSize];

}

NetworkServerConnection::~NetworkServerConnection()
{
	// Close() should already have been called, if not - do it now
	if (!_closeConnection)
		Close();

	while (_txQueue.size() > 0)
	{
		delete _txQueue.front();
		_txQueue.pop_front();
	}

	delete[] _pRxBuff;

	delete _pReadThread;
	delete _pTransmitThread;
}

bool NetworkServerConnection::Initialise(int connectionId)
{
	_id = connectionId;

	// start the threads

	_pReadThread = new std::thread(&NetworkServerConnection::ReadThread, this);
	_pTransmitThread = new std::thread(&NetworkServerConnection::TransmitThread, this);

	return true;
}

void NetworkServerConnection::GetIpAddress(unsigned char* pIpAddress)
{
	SOCKADDR_IN sockName;
	int sockNameLen = sizeof(sockName);
	getsockname(_socket, (SOCKADDR*)&sockName, &sockNameLen);

	// copy IP address into apple response
	pIpAddress[0] = sockName.sin_addr.S_un.S_un_b.s_b1;
	pIpAddress[1] = sockName.sin_addr.S_un.S_un_b.s_b2;
	pIpAddress[2] = sockName.sin_addr.S_un.S_un_b.s_b3;
	pIpAddress[3] = sockName.sin_addr.S_un.S_un_b.s_b4;
}

std::string NetworkServerConnection::GetIpAddress()
{
	std::string ip("");

	SOCKADDR_IN sockName;
	int sockNameLen = sizeof(sockName);
	getsockname(_socket, (SOCKADDR*)&sockName, &sockNameLen);

	char buff[INET_ADDRSTRLEN];

	ip = inet_ntop(AF_INET, &sockName.sin_addr, buff, INET_ADDRSTRLEN);

	return ip;
}

bool NetworkServerConnection::Close()
{
	_closeConnection = true;

	// must wake the Tx thread so it can exit
	_transmitCv.notify_all();

	if (INVALID_SOCKET != _socket)
		closesocket(_socket);

	// wait for threads to terminate

	//if (_pReadThread)
		//_pReadThread->join();

	if (_pTransmitThread)
		_pTransmitThread->join();

	return true;
}

void NetworkServerConnection::ReadThread()
{
	//const int BuffLen = 8192;
	//char buff[BuffLen];

	int nBytes = 0;

	while (!_closeConnection)
	{
		int headerLen = -1;
		bool requestComplete = false;

		int buffAvail = _rxBuffSize - _nRxBytes;

		//if ((nBytes = recv(_socket, buff, BuffLen, 0)) > 0)
		if ((nBytes = recv(_socket, _pRxBuff + _nRxBytes, buffAvail, 0)) > 0)
		{
			_nRxBytes += nBytes;

			if (nBytes == buffAvail)
			{
				// we may overrun

				char* pOldBuff = _pRxBuff;

				_rxBuffSize <<= 1;

				TRACE("Rx buffer increased: %d\n", _rxBuffSize);

				_pRxBuff = new char[_rxBuffSize];
				memcpy(_pRxBuff, pOldBuff, _nRxBytes);

				delete[] pOldBuff;
			}

			//memcpy(_pRxBuff + _nRxBytes, buff, nBytes);
			//_nRxBytes += nBytes;

			// check for \r\n\r\n signifying end of header
			/*
			for (int i = 0; i <= nBytes - 4 && headerLen < 0; i++)
			{
				if (buff[i] == '\r' && buff[i+1] == '\n' && buff[i+2] == '\r' && buff[i+3] == '\n')
					headerLen = i;
			}
			*/
			for (int i = 0; i <= _nRxBytes - 4 && headerLen < 0; i++)
			{
				if (_pRxBuff[i] == '\r' && _pRxBuff[i + 1] == '\n' && _pRxBuff[i + 2] == '\r' && _pRxBuff[i + 3] == '\n')
					headerLen = i;
			}


			// parse the header and check if we're expecting a body

			if (headerLen > 0)
			{
				char* pHeader = new char[headerLen + 1];
				memcpy(pHeader, _pRxBuff, headerLen);
				pHeader[headerLen] = '\0';

				_networkRequest.ParseHeader(pHeader);

				delete[] pHeader;

				if (_networkRequest.contentLength > 0)
				{
					if (_nRxBytes - (headerLen + 4) >= _networkRequest.contentLength)
					{
						delete[] _networkRequest.pContent;
						_networkRequest.pContent = nullptr;

						// add an extra byte to the content buffer so we can null
						// terminate it to make it easier if it's a string

						_networkRequest.pContent = new char[_networkRequest.contentLength + 1];
						memcpy(_networkRequest.pContent,
							_pRxBuff + headerLen + 4,
							_networkRequest.contentLength);

						_networkRequest.pContent[_networkRequest.contentLength] = '\0';

						requestComplete = true;
					}
				}
				else
				{
					requestComplete = true;
				}
			}

			if (requestComplete)
			{
				_pServer->OnRequest(*this, _networkRequest);
				_nRxBytes = 0;
			}
			
		}
		else // socket has probably closed
		{
			int err = WSAGetLastError();

			//_closeConnection = true;
			//_socket = INVALID_SOCKET;

			_pReadThread->detach();

			_pServer->RemoveConnection(_id);

			break; // member vars will no longer be valid

			// wake the transmit thread so it can end
			//_transmitCv.notify_all();

			//### call OnDisconnect()/OnSocketClosed() ???
		}

	}

	TRACE("Read thread complete\n");
}

void NetworkServerConnection::Transmit(const char* pBuff, int len)
{
	TransmitBuffer* pTxBuff = new TransmitBuffer(pBuff, len);

	std::unique_lock<std::mutex> lock(_transmitMutex);

	_txQueue.push_back(pTxBuff);

	_transmitCv.notify_all();
}

void NetworkServerConnection::SendResponse(const NetworkResponse& response)
{
	std::string header = response.GetHeader();

	int len = header.length() + response.contentLength;

	char* pBuff = new char[len];

	memcpy(pBuff, header.c_str(), header.length());

	if (response.contentLength)
		memcpy(pBuff + header.length(), response.pContent, response.contentLength);

	Transmit(pBuff, len);

	delete[] pBuff;
}

void NetworkServerConnection::TransmitThread()
{
	while (!_closeConnection)
	{
		std::unique_lock<std::mutex> lock(_transmitMutex);
		_transmitCv.wait(lock);
		
		while (!_closeConnection && _txQueue.size() > 0)
		{
			TransmitBuffer* pTxBuff = _txQueue.front();
tx:
			char* ptr = pTxBuff->pData;
			int nBytes = send(_socket, ptr, pTxBuff->len, 0);

			if (SOCKET_ERROR == nBytes || 0 == nBytes)
			{
				TRACE("Error: unable to transmit data\n");
			}
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

	TRACE("Transmit thread complete\n");
}


///////////////////////////////////////////////////////////////////////////////
// NetworkRequest implementation
///////////////////////////////////////////////////////////////////////////////

NetworkRequest::NetworkRequest(const char* pHeader)
	: pContent(nullptr), contentLength(0)
{

	if (pHeader)
	{
		if (!ParseHeader(pHeader))
			TRACE("Error: failed to parse header");
	}
	
}

NetworkRequest::~NetworkRequest()
{
	delete[] pContent;
}

// example request:
//GET / HTTP/1.1
//Host: localhost
//User-Agent: Mozilla/5.0 (Windows NT 10.0; WOW64; rv:43.0) Gecko/20100101 Firefox/43.0
//Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8
//Accept-Language: en-GB,en;q=0.5
//Accept-Encoding: gzip, deflate
//Connection: keep-alive

bool NetworkRequest::ParseHeader(const char* pHeader)
{
	// initialise these values while we're at it in case
	// this object is reused
	type = "";
	path = "";
	protocol = "";
	contentLength = 0;
	//delete[] pContent;
	//pContent = nullptr;
	headerFieldMap.clear();

	// split into lines
	std::string hdr(pHeader);
	std::string line;
	std::list<std::string> lineList;

	int startPos = 0, endPos = 0;

	while (endPos != std::string::npos)
	{
		endPos = hdr.find("\r\n", startPos);

		if (endPos == std::string::npos)
			line = hdr.substr(startPos);
		else
			line = hdr.substr(startPos, endPos - startPos);

		lineList.push_back(line);

		startPos = endPos + 2; // skip the \r\n
	}

	// now process the first line e.g. "GET /some/path HTTP/1.0"

	line = lineList.front();
	lineList.pop_front();

	endPos = line.find(' ');

	if (endPos == std::string::npos)
		return false;

	type = hdr.substr(0, endPos);
	std::transform(type.begin(), type.end(), type.begin(), ::toupper);

	// get path

	startPos = line.find_first_not_of(' ', endPos);
	endPos = line.find(' ', startPos);

	path = line.substr(startPos, endPos - startPos);

	// protocol

	startPos = line.find_first_not_of(' ', endPos);
	protocol = line.substr(startPos);

	// now process each of the following lines as <field name>: <field value>

	std::string name, value;

	while (lineList.size() > 0)
	{
		line = lineList.front();
		lineList.pop_front();

		startPos = line.find_first_not_of(" \t");
		endPos = line.find(':', startPos);
		
		if (endPos == std::string::npos)
			continue;

		name = line.substr(startPos, endPos - startPos);

		startPos = line.find_first_not_of(" \t", endPos + 1);
		
		value = line.substr(startPos);

		// capitalise the name to make comparison reliable

		std::transform(name.begin(), name.end(), name.begin(), ::toupper);

		headerFieldMap[name] = value;

	}

	if (headerFieldMap.find("CONTENT-LENGTH") != headerFieldMap.end())
		contentLength = atoi(headerFieldMap["CONTENT-LENGTH"].c_str());

	return true;
}

///////////////////////////////////////////////////////////////////////////////
// NetworkResponse implementation
///////////////////////////////////////////////////////////////////////////////

NetworkResponse::NetworkResponse(const char* proto, int statusCode, const char* reasonPhrase)
	: contentLength(0), pContent(nullptr), protocol(proto), responseCode(statusCode), reason(reasonPhrase)
{

}

NetworkResponse::~NetworkResponse()
{
	delete[] pContent;
}

void NetworkResponse::AddHeaderField(const char* name, const char* value)
{
	headerFieldMap[name] = value;
}

void NetworkResponse::AddContent(const char* pData, int len)
{
	pContent = new char[len];
	memcpy(pContent, pData, len);
	contentLength = len;
}

std::string NetworkResponse::GetHeader() const
{
	std::ostringstream header;

	header << protocol << " " << responseCode << " " << reason << "\r\n";
	typedef std::map<std::string, std::string>::const_iterator iteratorType;

	for (iteratorType it = headerFieldMap.begin(); it != headerFieldMap.end(); ++it)
	{
		header << it->first << ": " << it->second << "\r\n";
	}

	if (contentLength > 0)
	{
		header << "Content-Length: " << contentLength << "\r\n";
	}

	header << "\r\n";

	return header.str();
}
