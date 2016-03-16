#pragma once
#include "NetworkServer.h"


class HttpServerConnection : public NetworkServerConnection
{
public:

	HttpServerConnection(NetworkServerInterface* pServerInterface, SOCKET socket, SOCKADDR_IN& remoteAddr);
	~HttpServerConnection();

protected:

};

class HttpServer : public NetworkServer<HttpServerConnection>
{
public:
	HttpServer();
	virtual ~HttpServer();

	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request);
};

