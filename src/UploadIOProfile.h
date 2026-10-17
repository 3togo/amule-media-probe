//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Copyright (c) 2002-2011 Merkur ( devs@emule-project.net / http://www.emule-project.net )
//
// Any parts of this program derived from the xMule, lMule or eMule project,
// or contributed by third-party developers are copyrighted by their
// respective authors.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA
//

// Temporary, opt-in baseline instrumentation for issue #1586.
#ifndef UPLOADIOPROFILE_H
#define UPLOADIOPROFILE_H

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <vector>

class UploadIOProfile
{
	using Clock = std::chrono::steady_clock;
	struct Sample
	{
		int64_t duration;
		uint64_t bytes;
		bool success;
		bool part;
		bool mmap;
	};

public:
	UploadIOProfile()
	{
		const char *path = std::getenv("AMULE_UPLOAD_IO_PROFILE");
		if (path && *path) {
			m_output.open(path, std::ios::out | std::ios::trunc);
			if (!m_output) {
				return;
			}
			m_output << "kind,elapsed_ns,duration_ns,bytes,success,part,mmap\n";
			m_enabled = true;
		}
	}

	bool Enabled() const { return m_enabled; }
	bool Failed() const { return m_output.fail(); }

	void BeginLoop()
	{
		if (m_enabled) {
			m_loopStart = Clock::now();
		}
	}

	bool EndLoop()
	{
		if (!m_enabled) {
			return true;
		}
		const auto end = Clock::now();
		const auto elapsed = Nanoseconds(end - m_start);
		// Serialization, flushing and the condition-variable sleep are outside active time.
		for (const auto &sample : m_samples) {
			m_output << "read," << elapsed << ',' << sample.duration << ',' << sample.bytes << ','
				 << sample.success << ',' << sample.part << ',' << sample.mmap << '\n';
		}
		m_output << "loop," << elapsed << ',' << Nanoseconds(end - m_loopStart) << ",0,0,0,0\n";
		m_output.flush();
		if (!m_output) {
			m_enabled = false;
			m_samples.clear();
			return false;
		}
		m_samples.clear();
		return true;
	}

	class Read
	{
	public:
		Read(UploadIOProfile &profile, uint64_t bytes, bool part, bool mmap)
		: m_profile(profile)
		, m_sample{ 0, bytes, false, part, mmap }
		{
		}
		Read(const Read &) = delete;
		Read &operator=(const Read &) = delete;

		void Start()
		{
			if (m_profile.Enabled()) {
				m_started = true;
				m_start = Clock::now();
			}
		}

		void Finish()
		{
			if (m_started) {
				m_sample.duration = Nanoseconds(Clock::now() - m_start);
				m_sample.success = true;
			}
		}

		~Read()
		{
			if (m_started) {
				if (!m_sample.success) {
					m_sample.duration = Nanoseconds(Clock::now() - m_start);
				}
				m_profile.m_samples.push_back(m_sample);
			}
		}

	private:
		UploadIOProfile &m_profile;
		Sample m_sample;
		bool m_started = false;
		Clock::time_point m_start;
	};

private:
	static int64_t Nanoseconds(Clock::duration duration)
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
	}

	bool m_enabled = false;
	std::ofstream m_output;
	std::vector<Sample> m_samples;
	Clock::time_point m_start = Clock::now();
	Clock::time_point m_loopStart;
};

#endif
