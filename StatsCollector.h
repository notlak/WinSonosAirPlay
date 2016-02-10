#pragma once
#include <mutex>

class StatsCollector
{
public:
	StatsCollector();
	virtual ~StatsCollector();

	static StatsCollector* GetInstance();
	static void Close();

	void AddRxPacket();
	void AddRxBytes(int nBytes);
	void AddMissedPackets(int nPackets);

	void AddTxBytes(int nBytes);
	void TxQueueLen(int len);

	void AddRetransmissionRequests(int count);
	void AddRetransmissionResponse();
	void AddRetransmissionTimeout();

	struct Stats
	{
		Stats() { Reset(); }
		void Reset()
		{
			rxPackets = 0; rxBytes = 0; missedPackets = 0; 
			txBytes = 0; txQueueLen = 0; retxReqs = 0; retxResps = 0;
			retxTimeouts = 0;
		}
		int rxPackets;
		int rxBytes;
		int missedPackets;
		int txBytes;
		int txQueueLen;
		int retxReqs;
		int retxResps;
		int retxTimeouts;
	};

	Stats GetandReset();

protected:

	static StatsCollector* pInstance;

	std::mutex _statsMutex;
	Stats _stats;
};

