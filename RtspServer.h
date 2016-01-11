#pragma once
#include "NetworkServer.h"
class RtspServer :
	public NetworkServer
{
public:
	RtspServer();
	virtual ~RtspServer();

	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request);

protected:

	bool LoadAirPortExpressKey();

	void HandleOptions(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleAnnounce(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleSetup(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleRecord(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleFlush(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleGetParameter(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleSetParameter(NetworkServerConnection& connection, NetworkRequest& request);
	void HandleTeardown(NetworkServerConnection& connection, NetworkRequest& request);

	RSA* _airPortExpressKey;
};

