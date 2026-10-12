// This file is part of the aMule Project.
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
// Licensed under the GNU General Public License, version 2 or later.

#ifndef GEOIPUPDATESCHEDULE_H
#define GEOIPUPDATESCHEDULE_H

#include <cstdint>

// Uses the core's monotonic milliseconds, independent of provider release dates
// and wall-clock adjustments. Record attempts, including failures, to avoid retry storms.
class CGeoIPUpdateSchedule
{
public:
	static constexpr uint64_t Interval = 7ULL * 24 * 60 * 60 * 1000;
	explicit CGeoIPUpdateSchedule(uint64_t now = 0)
	: m_lastAttempt(now)
	{
	}

	bool IsDue(uint64_t now, bool enabled, bool autoUpdate, bool downloading) const
	{
		return enabled && autoUpdate && !downloading && now - m_lastAttempt >= Interval;
	}

	void RecordAttempt(uint64_t now) { m_lastAttempt = now; }

private:
	uint64_t m_lastAttempt;
};

#endif // GEOIPUPDATESCHEDULE_H
