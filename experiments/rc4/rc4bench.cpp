// This file is part of the aMule Project.
// Copyright (c) 2026 aMule Team. GPL-2.0-or-later.
// Standalone benchmark; no application dependencies. See README.md.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

#if defined(_MSC_VER)
#define RC4_NOINLINE __declspec(noinline)
#else
#define RC4_NOINLINE __attribute__((noinline))
#endif

struct ByteState
{
	uint8_t s[256];
	uint8_t x, y;
};
struct WordState
{
	uint32_t s[256];
	uint32_t x, y;
};
template <class State> void setup(State &k)
{
	for (unsigned i = 0; i < 256; ++i)
		k.s[i] = i;
	k.x = k.y = 0;
	unsigned j = 0;
	for (unsigned i = 0; i < 256; ++i) {
		j = (j + k.s[i] + (i % 16)) & 255;
		std::swap(k.s[i], k.s[j]);
	}
}
// Baseline is the production byte loop, including its input-null branch.
RC4_NOINLINE void baseline(ByteState &k, const uint8_t *in, uint8_t *out, uint32_t n)
{
	uint8_t x = k.x, y = k.y;
	auto *s = k.s;
	for (uint32_t i = 0; i < n; ++i) {
		uint8_t index;
		x = (x + 1) % 256;
		y = (s[x] + y) % 256;
		std::swap(s[x], s[y]);
		index = (s[x] + s[y]) % 256;
		if (in)
			out[i] = in[i] ^ s[index];
	}
	k.x = x;
	k.y = y;
}
// Each template adds one change: wider locals, cached swap, split discard,
// unrolling. WordState then measures the table-width cost independently.
template <class State, int Variant>
RC4_NOINLINE void candidate(State &k, const uint8_t *in, uint8_t *out, uint32_t n)
{
	uint32_t x = k.x, y = k.y;
	auto *s = k.s;
	auto step = [&]() {
		x = (x + 1) & 255;
		uint32_t a = s[x];
		y = (y + a) & 255;
		if constexpr (Variant >= 2) {
			uint32_t b = s[y];
			s[x] = b;
			s[y] = a;
			return (a + b) & 255;
		} else {
			std::swap(s[x], s[y]);
			return (s[x] + s[y]) & 255;
		}
	};
	if constexpr (Variant >= 3) {
		if (!in) {
			for (uint32_t i = 0; i < n; ++i)
				step();
			k.x = x;
			k.y = y;
			return;
		}
	}
	uint32_t i = 0;
	if constexpr (Variant == 4) {
		for (; n - i >= 4; i += 4) {
			out[i] = in[i] ^ s[step()];
			out[i + 1] = in[i + 1] ^ s[step()];
			out[i + 2] = in[i + 2] ^ s[step()];
			out[i + 3] = in[i + 3] ^ s[step()];
		}
	}
	for (; i < n; ++i) {
		auto index = step();
		if (in)
			out[i] = in[i] ^ s[index];
	}
	k.x = x;
	k.y = y;
}
volatile uint64_t checksum = 0;
template <class State, class Function> void measure(const char *name, Function crypt)
{
	for (unsigned size : { 180u * 1024, 10u * 1024, 1300u }) {
		std::vector<uint8_t> data(size, 0x5a), reference(size, 0x5a);
		State k;
		setup(k);
		ByteState base;
		setup(base);
		crypt(k, data.data(), data.data(), size);
		baseline(base, reference.data(), reference.data(), size);
		if (data != reference)
			std::abort();
		crypt(k, nullptr, nullptr, 1024);
		baseline(base, nullptr, nullptr, 1024);
		crypt(k, data.data(), data.data(), size);
		baseline(base, reference.data(), reference.data(), size);
		if (data != reference)
			std::abort();
		std::vector<double> runs;
		for (unsigned run = 0; run < 7; ++run) {
			auto start = std::clock();
			unsigned iterations = (32u * 1024 * 1024) / size;
			for (unsigned i = 0; i < iterations; ++i)
				crypt(k, data.data(), data.data(), size);
			const auto finish = std::clock();
			if (start == std::clock_t(-1) || finish == std::clock_t(-1) || finish <= start) {
				std::fputs("CPU clock unavailable or sample too short\n", stderr);
				std::exit(EXIT_FAILURE);
			}
			auto seconds = double(finish - start) / CLOCKS_PER_SEC;
			checksum = checksum + data[run];
			runs.push_back(double(size) * iterations / seconds / 1e6);
		}
		std::sort(runs.begin(), runs.end());
		std::printf("%s,%u,%.1f\n", name, size, runs[3]);
	}
	for (bool tcp : { false, true }) {
		std::vector<double> runs;
		for (unsigned run = 0; run < 7; ++run) {
			State k;
			uint8_t data[100] = {};
			auto start = std::clock();
			for (unsigned i = 0; i < 30000; ++i) {
				setup(k);
				if (tcp)
					crypt(k, nullptr, nullptr, 1024);
				else {
					crypt(k, data, data, 4);
					crypt(k, data, data, 1);
					crypt(k, nullptr, nullptr, 8);
					crypt(k, data, data, 100);
				}
				checksum = checksum + k.s[i & 255] + data[0];
			}
			const auto finish = std::clock();
			if (start == std::clock_t(-1) || finish == std::clock_t(-1) || finish <= start) {
				std::fputs("CPU clock unavailable or sample too short\n", stderr);
				std::exit(EXIT_FAILURE);
			}
			auto seconds = double(finish - start) / CLOCKS_PER_SEC;
			runs.push_back(30 / seconds);
		}
		std::sort(runs.begin(), runs.end());
		std::printf("%s,%s,%.1f\n", name, tcp ? "TCP-kkeys/s" : "UDP-kdatagrams/s", runs[3]);
	}
}
int main()
{
	std::puts("variant,bytes-or-workload,MB/s-or-kops/s");
	measure<ByteState>("baseline", baseline);
	measure<ByteState>("wide-locals", candidate<ByteState, 1>);
	measure<ByteState>("cached", candidate<ByteState, 2>);
	measure<ByteState>("split", candidate<ByteState, 3>);
	measure<ByteState>("unroll4", candidate<ByteState, 4>);
	measure<WordState>("wide-state", candidate<WordState, 3>);
}
