#pragma once

#include <string>
#include <vector>

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

	bool NetworkRequest(const std::string& hostname, int port, const std::string& req, std::vector<unsigned char>& content);
	void EscapeString(const std::string& in, std::string& out);

	std::string _apiKey;
	std::string _dirPath;
};

