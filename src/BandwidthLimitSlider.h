//
// This file is part of the aMule Project.
//
// Copyright (c) 2003-2026 aMule Team ( https://amule-org.github.io )
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

#ifndef AMULE_BANDWIDTH_LIMIT_SLIDER_H
#define AMULE_BANDWIDTH_LIMIT_SLIDER_H

#include <algorithm>
#include <initializer_list>

#include <wx/checkbox.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include "muuli_wdr.h"

// The spin control remains the saved preference. The checkbox maps unlimited
// to zero; the slider only represents enabled limits from 1 to line capacity.
class BandwidthLimitSlider : public wxSlider
{
public:
	BandwidthLimitSlider(wxWindow *parent,
		wxWindowID id,
		wxSpinCtrl *limit,
		wxSpinCtrl *capacity,
		wxCheckBox *enabled,
		wxStaticText *equivalent)
	: wxSlider(parent, id, 1, 1, capacity->GetValue())
	, m_limit(limit)
	, m_capacity(capacity)
	, m_enabled(enabled)
	, m_equivalent(equivalent)
	, m_lastLimit(1)
	{
		SetName(limit->GetId() == IDC_MAXDOWN ? _("Download limit") : _("Upload limit"));
		SetToolTip(_("Adjust the enabled limit from 1 KiB/s to line capacity."));
		const wxSize maximum = m_equivalent->GetTextExtent(wxString::Format(
			_("%d KiB/s (%.2f MiB/s)"), m_limit->GetMax(), m_limit->GetMax() / 1024.0));
		wxSize minimum = m_equivalent->GetTextExtent(_("Unlimited"));
		minimum.IncTo(maximum);
		m_equivalent->SetMinSize(minimum);
		Sync();
		Bind(wxEVT_SLIDER, [this](wxCommandEvent &) {
			m_limit->SetValue(GetValue());
			NotifyLimitChanged();
		});
		enabled->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &event) {
			if (event.IsChecked()) {
				// A saved or typed limit may exceed capacity; only the slider is clamped.
				m_limit->SetValue(m_lastLimit);
			} else {
				m_limit->SetValue(0);
			}
			NotifyLimitChanged();
		});
		limit->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) {
			Sync();
			event.Skip();
		});
		limit->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent &event) {
			Sync();
			event.Skip();
		});
		capacity->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) {
			Sync();
			event.Skip();
		});
		capacity->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent &event) {
			Sync();
			event.Skip();
		});
	}

	void Sync()
	{
		const int value = m_limit->GetValue();
		const int capacity = m_capacity->GetValue();
		SetRange(1, std::max(1, capacity));
		SetValue(std::max(1, std::min(value, capacity)));
		m_enabled->SetValue(value != 0);
		Enable(value != 0);
		if (value != 0) {
			m_lastLimit = value;
		}
		m_equivalent->SetLabel(
			value == 0 ? _("Unlimited")
				   : wxString::Format(_("%d KiB/s (%.2f MiB/s)"), value, value / 1024.0));
	}

private:
	void NotifyLimitChanged()
	{
		wxSpinEvent changed(wxEVT_SPINCTRL, m_limit->GetId());
		changed.SetEventObject(m_limit);
		changed.SetPosition(m_limit->GetValue());
		m_limit->GetEventHandler()->ProcessEvent(changed);
		Sync();
	}

	wxSpinCtrl *m_limit;
	wxSpinCtrl *m_capacity;
	wxCheckBox *m_enabled;
	wxStaticText *m_equivalent;
	int m_lastLimit;
};

inline void SyncBandwidthLimitSliders(wxWindow *window)
{
	for (const int id : { IDC_DOWNLOAD_LIMIT_SLIDER, IDC_UPLOAD_LIMIT_SLIDER }) {
		if (auto *slider = dynamic_cast<BandwidthLimitSlider *>(window->FindWindow(id))) {
			slider->Sync();
		}
	}
}

#endif
