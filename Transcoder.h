#pragma once

#include <ALACDecoder.h>
#include <lame.h>

// Transcode from ALAC -> PCM -> MP3 
// may need to implement rate throttling?


class CTranscoder
{
public:
	CTranscoder();
	virtual ~CTranscoder();

	bool Init(ALACSpecificConfig* alacConfig, int streamId);

	bool Write(unsigned char* pAlac, int len);
	//bool Read(unsigned char* pMp3, int maxLen);

protected:

	ALACDecoder* _pAlacDecoder;
	BitBuffer* _pAlacBitBuffer;
	uint8_t*    _pAlacInputBuffer;
	uint8_t*    _pAlacOutputBuffer;
	ALACSpecificConfig _alacConfig;
	lame_global_flags* _pLameGlobalFlags;
	uint8_t* _pMp3Buffer;
	int _nMp3Buffer;
	int _streamId;

};

