// This file is part of the aMule Project.
// Copyright (c) 2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#ifndef UPLOADINTEGRITY_H
#define UPLOADINTEGRITY_H

#include "SHAHashSet.h"
#include <protocol/ed2k/Constants.h>
#include <algorithm>
#include <memory>
#include <vector>

enum class UploadIntegrityResult
{
	Verified,
	InvalidRange,
	MissingHash,
	Mismatch
};

// AICH leaves restart at each ED2K part boundary, including its short final leaf.
inline uint64 UploadLeafStart(uint64 offset)
{
	return (offset / PARTSIZE) * PARTSIZE + ((offset % PARTSIZE) / EMBLOCKSIZE) * EMBLOCKSIZE;
}

inline uint64 UploadLeafEnd(uint64 start, uint64 size)
{
	return std::min(size, std::min(start + EMBLOCKSIZE, (start / PARTSIZE + 1) * PARTSIZE));
}

// Reader must fill the entire range or throw. Keep a private copy: mmap-backed bytes
// could change between verification and packet creation. No bytes escape on failure.
template <typename Reader>
UploadIntegrityResult ReadVerifiedUpload(
	CAICHHashTree &tree, uint64 size, uint64 start, uint64 end, Reader read, std::vector<uint8_t> &output)
{
	output.clear();
	if (start >= end || end > size || end - start > 3 * EMBLOCKSIZE || tree.GetNDataSize() != size) {
		return UploadIntegrityResult::InvalidRange;
	}
	const uint64 first = UploadLeafStart(start);
	const uint64 last = UploadLeafEnd(UploadLeafStart(end - 1), size);
	std::vector<uint8_t> bytes(last - first);
	read(bytes.data(), first, bytes.size());
	std::unique_ptr<CAICHHashAlgo> algorithm(CAICHHashSet::GetNewHashAlgo());
	for (uint64 pos = first; pos < last;) {
		const uint64 next = UploadLeafEnd(pos, size);
		CAICHHashTree *leaf = tree.FindHash(pos, next - pos);
		if (!leaf || !leaf->GetHashValid()) {
			return UploadIntegrityResult::MissingHash;
		}
		algorithm->Reset();
		algorithm->Add(bytes.data() + (pos - first), static_cast<uint32>(next - pos));
		CAICHHash hash;
		algorithm->Finish(hash);
		if (hash != leaf->GetHash()) {
			return UploadIntegrityResult::Mismatch;
		}
		pos = next;
	}
	output.assign(bytes.begin() + (start - first), bytes.begin() + (end - first));
	return UploadIntegrityResult::Verified;
}

#endif
