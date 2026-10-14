// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#include <muleunit/test.h>
#include "ServerUDPPacketQueue.h"
#include <protocol/Protocols.h>

using namespace muleunit;

namespace
{
ServerUDPPacket Pending(uint32 bytes, uint32 ip = 0)
{
	ServerUDPPacket item;
	item.packet = std::make_unique<CPacket>(int8{ 0 }, bytes, OP_EDONKEYPROT);
	item.ip = ip;
	item.port = 4665;
	if (!ip) {
		item.addr = "unresolved.example";
	}
	return item;
}
} // namespace

DECLARE_SIMPLE(ServerUDPPacketQueue)

TEST(ServerUDPPacketQueue, StalledDnsCannotRetainUnlimitedEmptyPackets)
{
	CServerUDPPacketQueue queue;
	for (size_t i = 0; i < CServerUDPPacketQueue::MaxPackets; ++i) {
		ASSERT_TRUE(queue.Push(Pending(0)));
	}
	CPacket *first = queue.Front().packet.get();
	ASSERT_FALSE(queue.Push(Pending(0)));
	ASSERT_TRUE(queue.Front().packet.get() == first);
	ASSERT_TRUE(queue.Front().addr == "unresolved.example");
	ASSERT_TRUE(queue.Size() == CServerUDPPacketQueue::MaxPackets);
	ASSERT_TRUE(queue.Bytes() == 2 * CServerUDPPacketQueue::MaxPackets);

	queue.Pop();
	ASSERT_TRUE(queue.Push(Pending(0)));
}

TEST(ServerUDPPacketQueue, WireByteBudgetIncludesHeadersAndRecoversAfterDrop)
{
	CServerUDPPacketQueue queue;
	constexpr uint32 wireBytes = 32768;
	for (size_t i = 0; i < CServerUDPPacketQueue::MaxBytes / wireBytes; ++i) {
		ASSERT_TRUE(queue.Push(Pending(wireBytes - 2)));
	}
	ASSERT_TRUE(queue.Bytes() == CServerUDPPacketQueue::MaxBytes);
	CPacket *first = queue.Front().packet.get();
	ASSERT_FALSE(queue.Push(Pending(0)));
	ASSERT_TRUE(queue.Front().packet.get() == first);

	queue.Pop();
	ASSERT_TRUE(queue.Bytes() == CServerUDPPacketQueue::MaxBytes - wireBytes);
	ASSERT_TRUE(queue.Push(Pending(wireBytes - 2)));
	ASSERT_FALSE(queue.Push(Pending(0)));
}

TEST(ServerUDPPacketQueue, OversizedPacketCannotEnterAnEmptyQueue)
{
	CServerUDPPacketQueue queue;
	ASSERT_FALSE(queue.Push(Pending(CServerUDPPacketQueue::MaxBytes - 1)));
	ASSERT_TRUE(queue.Empty());
	ASSERT_TRUE(queue.Bytes() == 0);
	ASSERT_TRUE(queue.Push(Pending(0)));
}

TEST(ServerUDPPacketQueue, DnsRotationPreservesOwnershipOrderAndBudget)
{
	CServerUDPPacketQueue queue;
	auto unresolved = Pending(100);
	CPacket *first = unresolved.packet.get();
	ASSERT_TRUE(queue.Push(std::move(unresolved)));
	ASSERT_TRUE(queue.Push(Pending(200, 0x01020304)));
	queue.RotateFront();
	ASSERT_TRUE(queue.Size() == 2);
	ASSERT_TRUE(queue.Bytes() == 304);
	ASSERT_TRUE(queue.Front().ip == 0x01020304);

	// Delivery transfers the same owned packet to the sending socket.
	auto outgoing = queue.Pop();
	ASSERT_TRUE(outgoing.packet != nullptr);
	ASSERT_TRUE(outgoing.port == 4665);
	ASSERT_TRUE(queue.Bytes() == 102);
	ASSERT_TRUE(queue.Front().packet.get() == first);
	queue.Pop();
	ASSERT_TRUE(queue.Empty());
	ASSERT_TRUE(queue.Bytes() == 0);
}

TEST(ServerUDPPacketQueue, RejectsMissingPacketWithoutConsumingBudget)
{
	CServerUDPPacketQueue queue;
	ASSERT_FALSE(queue.Push(ServerUDPPacket{}));
	ASSERT_TRUE(queue.Empty());
	ASSERT_TRUE(queue.Bytes() == 0);
}
