#pragma once

#include <WinSock2.h>

#include <UPnP.h>

#include <string>
#include <list>
#include <mutex>

#define NO_SONOS

class SonosDevice
{
public:
	
	SonosDevice() {}

	SonosDevice(std::string& udn, std::string& name, std::string& address, int port, std::string& group, bool isCoordinator) 
		: _udn(udn), _name(name), _address(address), _port(port), _group(group), _isCoordinator(isCoordinator) {}

	SonosDevice(const SonosDevice& device) { *this = device; }

	SonosDevice& operator=(const SonosDevice& d) 
	{
		_udn = d._udn;  _name = d._name; _address = d._address; _port = d._port; _group = d._group; _isCoordinator = d._isCoordinator; return *this;
	}

	std::string _udn;
	std::string _name;
	std::string _address;
	int _port;
	std::string _group;
	bool _isCoordinator;
};

class SonosGroup
{
public:
	std::string _name;
	std::list<std::string> _members; // list of the device UDNs
	std::string _coordinator; // device UDN
};

class SonosInterfaceClient
{
public:
	virtual void OnNewDevice(const SonosDevice& dev) {}
	virtual void OnDeviceRemoved(const SonosDevice& dev) {}
};

class SonosInterface
{
public:
	SonosInterface();
	virtual ~SonosInterface();

	static SonosInterface* GetInstance();
	static void Delete();

	void RegisterClient(SonosInterfaceClient* pClient) { _pClient = pClient; }

	bool Init();
	bool FindSpeakers();
	bool HttpRequest(const char* ip, int port, const char* path, std::string& document);

	void UpnpDeviceAdded(const char* pUdn, const char* pUrl);
	void UpnpDeviceRemoved(const char* pUdn);
	void UpnpSearchComplete();

	bool Play(const char* pUdn);
	bool Pause(const char* pUdn);
	bool Stop(const char* pUdn);
	bool SetAvTransportUri(const char* pUdn, const char* pUri, const char* pTitle);
	bool SetVolume(const char* pUdn, int volume);

	bool Test()
	{
		if (_groupList.size() > 0)
		{
			std::string udn(_groupList.front()._coordinator);
			if (!udn.empty())
			{	
				SetAvTransportUri(udn.c_str(), "x-rincon-mp3radio://www.bbc.co.uk/radio/listen/live/r3.asx", "rdok testing");
				Play(udn.c_str());
			}
		}

		return true;
	}

protected:

	std::string FormatMetaData(const char* pTitle);
	bool MustEscape(char ch, std::string& escaped);

	bool ParseUrl(const char* url, std::string& host, int& port, std::string& path);
	bool ParseZoneTopology(const char* pXml);

	bool IsDeviceInList(const char* pUdn);
	bool GetDeviceByUdn(const char* pUdn, SonosDevice& device);

	std::string CreateSoapRequest(const char* endPoint, const char* host, int port, const char* body, const char* action);
	bool NetworkRequest(const char* ip, int port, const char* path, std::string& document, const char* req);

	bool StartAsyncSearch();
	void CancelAsyncSearch();

	void SearchThread();

	SonosInterfaceClient* _pClient;

	std::thread* _pSearchThread;

	std::list<SonosDevice> _deviceList;
	std::list<SonosGroup> _groupList;
	std::mutex _listMutex;

	IUPnPDeviceFinderCallback* _pUPnPDeviceFinderCallback;
	IUPnPDeviceFinder* _pUPnPDeviceFinder;

	LONG _lFindData;

	bool _searching;
	bool _searchCompleted;
	bool _shutdown;

	static SonosInterface* InstancePtr;

};

