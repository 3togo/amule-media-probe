// This file is part of the aMule Project.
// Copyright (c) 2026 aMule Team. GPL-2.0-or-later.
// Compile this identical harness with either the production or baseline source.
#include <RC4Encrypt.h>
#include <common/MD5Sum.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

// Required by the standalone build's debug diagnostics.
wxString GetFullMuleVersion()
{
	return wxT("RC4Benchmark");
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::fputs("Expected workload: 180K, 10K, 1300, TCP, or UDP\n", stderr);
		return 1;
	}
	uint32 size = 0;
	if (std::strcmp(argv[1], "180K") == 0)
		size = 180 * 1024;
	else if (std::strcmp(argv[1], "10K") == 0)
		size = 10 * 1024;
	else if (std::strcmp(argv[1], "1300") == 0)
		size = 1300;
	const bool tcp = std::strcmp(argv[1], "TCP") == 0;
	const bool udp = std::strcmp(argv[1], "UDP") == 0;
	if (!size && !tcp && !udp) {
		std::fputs("Unknown workload\n", stderr);
		return 1;
	}
	const MD5Sum key(wxT("aMule RC4 production benchmark"));
	std::vector<uint8> data(size ? size : 100, 0x5a);
	CRC4EncryptableBuffer cipher;
	cipher.SetKey(key);
	const unsigned iterations = size ? (64 * 1024 * 1024) / size : 50000;
	const auto start = std::chrono::steady_clock::now();
	const auto cpuStart = std::clock();
	unsigned checksum = 0;
	for (unsigned i = 0; i < iterations; ++i) {
		if (size) {
			cipher.RC4Crypt(data.data(), data.data(), size);
		} else {
			cipher.FullReset();
			cipher.SetKey(key, udp);
			if (udp) {
				cipher.RC4Crypt(data.data(), data.data(), 4);
				cipher.RC4Crypt(data.data(), data.data(), 1);
				cipher.RC4Crypt(nullptr, nullptr, 8);
				cipher.RC4Crypt(data.data(), data.data(), 100);
			}
		}
		checksum += data[i % data.size()];
	}
	const auto cpuFinish = std::clock();
	const double elapsed =
		std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	// Observe the state left by TCP setup/discard too, outside the timing.
	cipher.RC4Crypt(data.data(), data.data(), static_cast<uint32>(data.size()));
	for (uint8 byte : data) {
		checksum += byte;
	}
	if (elapsed <= 0 || cpuStart == std::clock_t(-1) || cpuFinish == std::clock_t(-1) ||
		cpuFinish <= cpuStart)
		return 1;
	const double cpuElapsed = double(cpuFinish - cpuStart) / CLOCKS_PER_SEC;
	const double cpuRate =
		size ? double(size) * iterations / cpuElapsed / 1e6 : iterations / cpuElapsed / 1e3;
	const double rate = size ? double(size) * iterations / elapsed / 1e6 : iterations / elapsed / 1e3;
	// A checksum lets the runner sanity-check stream consistency alongside timing.
	std::printf("%.6f,%.6f,%u\n", rate, cpuRate, checksum);
}
