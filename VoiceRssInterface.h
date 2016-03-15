#pragma once

#include <string>

//
// This class is designed to integrate with the voicerss.org website to obtain
// voice samples
//

class VoiceRssInterface
{
public:
	VoiceRssInterface();
	virtual ~VoiceRssInterface();

	void Initialise(const std::string& apiKey, const std::string& dirPath);

	bool Convert(const std::string& text, std::string& filename);

protected:

	bool NetworkRequest(const sockaddr_in* addr, const char* path, std::string& document, const char* req);
	void EscapeString(const std::string& in, std::string& out);

	std::string _apiKey;
	std::string _dirPath;
};

