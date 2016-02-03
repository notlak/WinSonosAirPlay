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

	struct Stats
	{
		Stats() { Reset(); }
		void Reset() { rxPackets = 0; rxBytes = 0; missedPackets = 0; }
		int rxPackets;
		int rxBytes;
		int missedPackets;
	};

	Stats GetandReset();

protected:

	static StatsCollector* pInstance;

	std::mutex _statsMutex;
	Stats _stats;
};

