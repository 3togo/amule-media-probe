// This file is part of the aMule Project.
// Copyright (c) 2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#include <muleunit/test.h>
#include "UploadIntegrity.h"
#include "CFile.h"
#include "Preferences.h"
#include <wx/filename.h>
#include <cstring>
#include <stdexcept>

using namespace muleunit;
DECLARE_SIMPLE(UploadIntegrity)

namespace
{
struct Fixture
{
	std::vector<uint8_t> disk;
	CAICHHashSet hashes{ nullptr };
	explicit Fixture(size_t size)
	: disk(size)
	{
		for (size_t i = 0; i < size; ++i) {
			disk[i] = static_cast<uint8_t>(i * 37 + i / 251);
		}
		hashes.SetFileSize(size);
		std::unique_ptr<CAICHHashAlgo> algorithm(CAICHHashSet::GetNewHashAlgo());
		// Build the reference tree independently, restarting leaves for each part.
		for (uint64 part = 0; part < size; part += PARTSIZE) {
			for (uint64 offset = part; offset < std::min<uint64>(size, part + PARTSIZE);
				offset += EMBLOCKSIZE) {
				const uint64 length = std::min<uint64>(
					EMBLOCKSIZE, std::min<uint64>(size, part + PARTSIZE) - offset);
				algorithm->Reset();
				algorithm->Add(disk.data() + offset, length);
				hashes.m_pHashTree.SetBlockHash(length, offset, algorithm.get());
			}
		}
		ASSERT_TRUE(hashes.ReCalculateHash(false));
	}

	UploadIntegrityResult Read(uint64 start, uint64 end, std::vector<uint8_t> &output)
	{
		return ReadVerifiedUpload(
			hashes.m_pHashTree,
			disk.size(),
			start,
			end,
			[this](void *buffer, uint64 offset, size_t size) {
				ASSERT_TRUE(offset + size <= disk.size());
				std::memcpy(buffer, disk.data() + offset, size);
			},
			output);
	}
};
} // namespace

TEST(UploadIntegrity, UnalignedAndMultipleLeavesReturnExactlyRequestedBytes)
{
	Fixture file(4 * EMBLOCKSIZE);
	std::vector<uint8_t> output;
	const uint64 start = 123;
	const uint64 end = start + 3 * EMBLOCKSIZE;
	ASSERT_TRUE(file.Read(start, end, output) == UploadIntegrityResult::Verified);
	ASSERT_TRUE(output == std::vector<uint8_t>(file.disk.begin() + start, file.disk.begin() + end));
}

TEST(UploadIntegrity, DetectsCorruptionOutsideRequestedSliceInTouchedLeaf)
{
	Fixture file(2 * EMBLOCKSIZE);
	file.disk[1] ^= 1;
	std::vector<uint8_t> output{ 42 };
	ASSERT_TRUE(file.Read(100, 200, output) == UploadIntegrityResult::Mismatch);
	ASSERT_TRUE(output.empty());
}

TEST(UploadIntegrity, DetectsCorruptionInLaterLeafWithoutReturningEarlierBytes)
{
	Fixture file(3 * EMBLOCKSIZE);
	file.disk[EMBLOCKSIZE + 10] ^= 1;
	std::vector<uint8_t> output;
	ASSERT_TRUE(file.Read(100, 2 * EMBLOCKSIZE, output) == UploadIntegrityResult::Mismatch);
	ASSERT_TRUE(output.empty());
}

TEST(UploadIntegrity, HandlesShortLeafAtEd2kPartBoundaryAndFinalFileLeaf)
{
	Fixture file(PARTSIZE + EMBLOCKSIZE + 17);
	std::vector<uint8_t> output;
	for (const auto &range : { std::make_pair(PARTSIZE - 100, PARTSIZE + 100),
		     std::make_pair(PARTSIZE + EMBLOCKSIZE - 5, static_cast<uint64>(file.disk.size())) }) {
		ASSERT_TRUE(file.Read(range.first, range.second, output) == UploadIntegrityResult::Verified);
		ASSERT_TRUE(output == std::vector<uint8_t>(file.disk.begin() + range.first,
					      file.disk.begin() + range.second));
	}
	file.disk[PARTSIZE - 1] ^= 1;
	ASSERT_TRUE(file.Read(PARTSIZE - 10, PARTSIZE + 10, output) == UploadIntegrityResult::Mismatch);
	ASSERT_TRUE(output.empty());
}

TEST(UploadIntegrity, SingleLeafNeedsOnlyMasterHash)
{
	Fixture reference(17);
	CAICHHashSet snapshot(nullptr);
	snapshot.SetFileSize(17);
	snapshot.SetMasterHash(reference.hashes.m_pHashTree.GetHash(), AICH_HASHSETCOMPLETE);
	std::vector<uint8_t> output;
	ASSERT_TRUE(ReadVerifiedUpload(
			    snapshot.m_pHashTree,
			    17,
			    1,
			    17,
			    [&reference](void *buffer, uint64 offset, size_t length) {
				    std::memcpy(buffer, reference.disk.data() + offset, length);
			    },
			    output) == UploadIntegrityResult::Verified);
}

TEST(UploadIntegrity, MissingLeafHashesFailClosed)
{
	CAICHHashSet hashes(nullptr);
	hashes.SetFileSize(2 * EMBLOCKSIZE);
	std::vector<uint8_t> output{ 42 };
	ASSERT_TRUE(ReadVerifiedUpload(
			    hashes.m_pHashTree,
			    2 * EMBLOCKSIZE,
			    1,
			    10,
			    [](void *buffer, uint64, size_t length) { std::memset(buffer, 0, length); },
			    output) == UploadIntegrityResult::MissingHash);
	ASSERT_TRUE(output.empty());
}

TEST(UploadIntegrity, RejectsInvalidRangesBeforeReading)
{
	Fixture file(4 * EMBLOCKSIZE);
	std::vector<uint8_t> output{ 42 };
	for (const auto &range : { std::make_pair(0ull, 0ull),
		     std::make_pair(20ull, 10ull),
		     std::make_pair(0ull, 3ull * EMBLOCKSIZE + 1),
		     std::make_pair(4ull * EMBLOCKSIZE, 4ull * EMBLOCKSIZE + 1) }) {
		ASSERT_TRUE(
			ReadVerifiedUpload(
				file.hashes.m_pHashTree,
				file.disk.size(),
				range.first,
				range.second,
				[](void *, uint64, size_t) { FAIL_M("Invalid range must not read disk"); },
				output) == UploadIntegrityResult::InvalidRange);
		ASSERT_TRUE(output.empty());
	}
}

TEST(UploadIntegrity, ReadFailureReturnsNoBytesAndOwnedCopySurvivesDiskChange)
{
	Fixture file(100);
	std::vector<uint8_t> output{ 42 };
	bool threw = false;
	try {
		ReadVerifiedUpload(
			file.hashes.m_pHashTree,
			file.disk.size(),
			0,
			10,
			[](void *, uint64, size_t) { throw std::runtime_error("short read"); },
			output);
	} catch (const std::runtime_error &) {
		threw = true;
	}
	ASSERT_TRUE(threw);
	ASSERT_TRUE(output.empty());
	const auto original = file.disk;
	ASSERT_TRUE(file.Read(0, file.disk.size(), output) == UploadIntegrityResult::Verified);
	file.disk.assign(file.disk.size(), 0);
	ASSERT_TRUE(output == original);
}

TEST(UploadIntegrity, DetectsOnDiskBitFlipWithUnchangedSizeAndMtime)
{
	Fixture reference(2 * EMBLOCKSIZE);
	struct TemporaryFile
	{
		wxString path = wxFileName::CreateTempFileName("amule-upload-integrity-");
		~TemporaryFile() { wxRemoveFile(path); }
	} temporary;
	{
		CFile file(temporary.path, CFile::write);
		ASSERT_TRUE(file.IsOpened());
		file.Write(reference.disk.data(), reference.disk.size());
	}
	wxFileName name(temporary.path);
	wxDateTime access, modified, created;
	ASSERT_TRUE(name.GetTimes(&access, &modified, &created));
	std::vector<uint8_t> output;
	auto read = [&](void *buffer, uint64 offset, size_t length) {
		CFile file(temporary.path, CFile::read);
		ASSERT_TRUE(file.IsOpened());
		file.Seek(offset);
		file.Read(buffer, length);
	};
	ASSERT_TRUE(ReadVerifiedUpload(reference.hashes.m_pHashTree,
			    reference.disk.size(),
			    10,
			    EMBLOCKSIZE + 10,
			    read,
			    output) == UploadIntegrityResult::Verified);
	{
		CFile file(temporary.path, CFile::read_write);
		file.Seek(EMBLOCKSIZE + 1);
		const uint8_t corrupt = reference.disk[EMBLOCKSIZE + 1] ^ 1;
		file.Write(&corrupt, 1);
	}
	ASSERT_TRUE(name.SetTimes(&access, &modified, nullptr));
	ASSERT_TRUE(name.GetModificationTime() == modified);
	ASSERT_TRUE(name.GetSize().GetValue() == reference.disk.size());
	ASSERT_TRUE(ReadVerifiedUpload(reference.hashes.m_pHashTree,
			    reference.disk.size(),
			    10,
			    EMBLOCKSIZE + 10,
			    read,
			    output) == UploadIntegrityResult::Mismatch);
	ASSERT_TRUE(output.empty());
}

TEST(UploadIntegrity, LoadsOwnerlessSnapshotAndRejectsCorruptStoredHashes)
{
	struct TemporaryConfig
	{
		wxString previous = CPreferences::GetConfigDir();
		wxString directory = wxFileName::CreateTempFileName("amule-upload-hashes-");
		TemporaryConfig()
		{
			wxRemoveFile(directory);
			ASSERT_TRUE(wxFileName::Mkdir(directory));
			CPreferences::SetConfigDir(directory + wxFileName::GetPathSeparator());
			CAICHHashSet::InvalidateRootHashCache();
		}
		~TemporaryConfig()
		{
			wxRemoveFile(CPreferences::GetConfigDir() + KNOWN2_MET_FILENAME);
			CPreferences::SetConfigDir(previous);
			CAICHHashSet::InvalidateRootHashCache();
			wxFileName::Rmdir(directory);
		}
	} config;
	Fixture reference(2 * EMBLOCKSIZE + 17);
	const auto root = reference.hashes.GetMasterHash();
	const wxString path = CPreferences::GetConfigDir() + KNOWN2_MET_FILENAME;
	{
		CFile stored(path, CFile::write);
		stored.WriteUInt8(KNOWN2_MET_VERSION);
		root.Write(&stored);
		stored.WriteUInt32(3);
		for (uint64 offset = 0; offset < reference.disk.size(); offset += EMBLOCKSIZE) {
			const uint64 length = std::min<uint64>(EMBLOCKSIZE, reference.disk.size() - offset);
			reference.hashes.m_pHashTree.FindHash(offset, length)->GetHash().Write(&stored);
		}
	}
	CAICHHashSet snapshot(nullptr);
	snapshot.SetFileSize(reference.disk.size());
	snapshot.SetMasterHash(root, AICH_HASHSETCOMPLETE);
	ASSERT_TRUE(snapshot.LoadHashSet(reference.disk.size()));
	std::vector<uint8_t> output;
	ASSERT_TRUE(ReadVerifiedUpload(
			    snapshot.m_pHashTree,
			    reference.disk.size(),
			    10,
			    EMBLOCKSIZE + 10,
			    [&reference](void *buffer, uint64 offset, size_t length) {
				    std::memcpy(buffer, reference.disk.data() + offset, length);
			    },
			    output) == UploadIntegrityResult::Verified);
	{
		CFile stored(path, CFile::read_write);
		stored.Seek(1 + CAICHHash::GetHashSize() + 4);
		const uint8_t corrupt =
			reference.hashes.m_pHashTree.FindHash(0, EMBLOCKSIZE)->GetHash().GetRawHash()[0] ^ 1;
		stored.Write(&corrupt, 1);
	}
	CAICHHashSet damaged(nullptr);
	damaged.SetFileSize(reference.disk.size());
	damaged.SetMasterHash(root, AICH_HASHSETCOMPLETE);
	ASSERT_FALSE(damaged.LoadHashSet(reference.disk.size()));
}
