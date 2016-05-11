#pragma once
#include "NetworkServer.h"
#include <string>
#include <vector>

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

	void Initialise(const std::string& ttsPath);

	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request);

protected:
	bool OnSayGui(NetworkServerConnection& connection, NetworkRequest& request);
	bool OnSayCommand(NetworkServerConnection& connection, NetworkRequest& request);
	bool OnServeTts(NetworkServerConnection& connection, NetworkRequest& request);
	std::vector<std::string> Split(const std::string& str, char delimiter);

	std::string UnescapeText(const std::string& text);
	std::string GetTextHashFilename(const std::string& text);

	std::string _ttsPath;
};

