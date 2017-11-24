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
	bool OnPauseCommand(NetworkServerConnection& connection, NetworkRequest& request);
	bool OnStopCommand(NetworkServerConnection& connection, NetworkRequest& request);
	bool OnTestCommand(NetworkServerConnection& connection, NetworkRequest& request);
	bool OnPlayFavCommand(NetworkServerConnection& connection, NetworkRequest& request);
	bool OnPlayCommand(NetworkServerConnection& connection, NetworkRequest& request);
	bool OnGetDevicesCommand(NetworkServerConnection& connection, NetworkRequest& request);
	bool OnVolumeUpCommand(NetworkServerConnection& connection, NetworkRequest& request);
	bool OnVolumeDownCommand(NetworkServerConnection& connection, NetworkRequest& request);
	bool OnSetVolumeCommand(NetworkServerConnection& connection, NetworkRequest& request);
	bool OnGroupCommand(NetworkServerConnection& connection, NetworkRequest& request);
	bool OnUngroupCommand(NetworkServerConnection& connection, NetworkRequest& request);

	// method to handle request from sonos
	bool OnServeTts(NetworkServerConnection& connection, NetworkRequest& request);
	
	void SendGoodResponse(NetworkServerConnection& connection, const std::string& body);
	void SendBadResponse(NetworkServerConnection& connection, const std::string& body);

	std::vector<std::string> Split(const std::string& str, char delimiter);

	std::string UnescapeText(const std::string& text);
	std::string GetTextHashFilename(const std::string& text);

	std::string _ttsPath;
};

