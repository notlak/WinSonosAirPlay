#pragma once
#include "NetworkServer.h"
#include <string>

class HttpControlServerConnection : public NetworkServerConnection
{
public:

	HttpControlServerConnection(NetworkServerInterface* pServerInterface, SOCKET socket, SOCKADDR_IN& remoteAddr);
	~HttpControlServerConnection();

protected:

};

class HttpControlServer : public NetworkServer<HttpControlServerConnection>
{
public:
	HttpControlServer();
	virtual ~HttpControlServer();

	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request);

protected:

	void OnSayCommand(NetworkServerConnection& connection, NetworkRequest& request);
	std::string UnescapeText(const std::string& text);
	std::string GetTextHashFilename(const std::string& text);
};

