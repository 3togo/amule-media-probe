// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#ifndef KAD_BUDDY_LIVENESS_H
#define KAD_BUDDY_LIVENESS_H

#include <cstdint>

namespace Kademlia
{

// Keep received activity independent of outgoing traffic, using a monotonic millisecond clock.
class CBuddyLiveness
{
public:
	void Reset(uint64_t now)
	{
		m_lastReceived = now;
		m_nextPing = now + PingInterval;
		m_lastIncomingPing = 0;
		m_hasIncomingPing = false;
	}

	void ReceivedPacket(uint64_t now) { m_lastReceived = now; }
	bool PingDue(uint64_t now) const { return now >= m_nextPing; }
	void PingSent(uint64_t now) { m_nextPing = now + PingInterval; }
	void PongReceived(uint64_t now)
	{
		ReceivedPacket(now);
		m_nextPing = now + PingInterval;
	}
	bool AllowIncomingPing(uint64_t now) const
	{
		return !m_hasIncomingPing || now - m_lastIncomingPing >= IncomingPingInterval;
	}
	void PingReceived(uint64_t now)
	{
		m_lastIncomingPing = now;
		m_hasIncomingPing = true;
		PongReceived(now);
	}
	// A peer without buddy ping support cannot distinguish healthy idleness from silence.
	bool Expired(uint64_t now, bool supportsPing = true) const
	{
		return supportsPing && now - m_lastReceived >= ReceiveTimeout;
	}

	static constexpr uint64_t PingInterval = 10 * 60 * 1000;
	static constexpr uint64_t IncomingPingInterval = 3 * 60 * 1000;
	// Stock peers can answer only every second ten-minute ping because their incoming
	// throttle shares the next-ping timestamp. Allow twenty minutes plus a grace period.
	static constexpr uint64_t ReceiveTimeout = 25 * 60 * 1000;

private:
	uint64_t m_lastReceived = 0;
	uint64_t m_nextPing = 0;
	uint64_t m_lastIncomingPing = 0;
	bool m_hasIncomingPing = false;
};

} // namespace Kademlia

#endif // KAD_BUDDY_LIVENESS_H
