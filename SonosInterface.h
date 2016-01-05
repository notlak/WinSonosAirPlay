#pragma once

class SonosInterface
{
public:
	SonosInterface();
	virtual ~SonosInterface();

	bool Init();
	bool FindSpeakers();
};

