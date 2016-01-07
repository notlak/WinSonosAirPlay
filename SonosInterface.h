#pragma once

#include <string>
#include <list>

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

class SonosInterface
{
public:
	SonosInterface();
	virtual ~SonosInterface();

	bool Init();
	bool FindSpeakers();
	bool HttpRequest(const char* ip, int port, const char* path, std::string& document);

	void UpnpDeviceAdded(const char* pUdn, const char* pUrl);
	void UpnpDeviceRemoved(const char* pUdn);
	void UpnpSearchComplete();

	bool Play(const char* pUdn);

protected:

	bool ParseUrl(const char* url, std::string& host, int& port, std::string& path);
	bool ParseZoneTopology(const char* pXml);

	bool IsDeviceInList(const char* pUdn);
	bool GetDeviceByUdn(const char* pUdn, SonosDevice& device);

	std::list<SonosDevice> _deviceList;
	std::list<SonosGroup> _groupList;
};

