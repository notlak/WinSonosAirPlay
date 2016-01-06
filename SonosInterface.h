#pragma once

#include <string>

class SonosInterface
{
public:
	SonosInterface();
	virtual ~SonosInterface();

	bool Init();
	bool FindSpeakers();

	// this is synchronous
	bool HttpRequest(const char* ip, int port, const char* path, std::string& document);

protected:

	bool ParseUrl(const char* url, std::string& host, int& port, std::string& path);

};

