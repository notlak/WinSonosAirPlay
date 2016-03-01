#pragma once

#include <WinSock2.h>

#include <UPnP.h>

#include <string>
#include <list>
#include <mutex>

//#define NO_SONOS

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
	virtual void OnDeviceAddressChanged(const SonosDevice& dev) {}
	virtual void OnDeviceNameChanged(const SonosDevice& dev, const std::string& oldName) {}
	virtual void OnDeviceCoordinatorStatusChanged(const SonosDevice& dev) {}
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

	// these non-blocking calls start a new thread then call the blocking
	// versions
	bool PlayUri(std::string udn, std::string uri, std::string title);
	bool Stop(std::string udn);
	bool SetVolume(std::string udn, int volume);

	// synchronous calls that wait for a response
	// all the arguments are passed by value in case they are called as
	// new thread functions (in which case the original values may go out
	// of scope/be deleted before the methods are complete)
	bool SetVolumeBlocking(std::string udn, int volume);
	bool PlayUriBlocking(std::string udn, std::string uri, std::string title);
	bool SetAvTransportUriBlocking(std::string udn, std::string uri, std::string title);
	bool PlayBlocking(std::string udn);
	bool PauseBlocking(std::string udn);
	bool StopBlocking(std::string udn);

protected:

	std::string FormatMetaData(const char* pTitle);
	bool MustEscape(char ch, std::string& escaped);

	bool ParseUrl(const char* url, std::string& host, int& port, std::string& path);
	bool ParseZoneTopology(const char* pXml);

	bool IsDeviceInList(const char* pUdn);
	bool GetDeviceByUdn(const char* pUdn, SonosDevice& device);
	void UpdateDeviceRecord(const SonosDevice& dev);

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

