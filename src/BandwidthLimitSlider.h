#ifndef AMULE_BANDWIDTH_LIMIT_SLIDER_H
#define AMULE_BANDWIDTH_LIMIT_SLIDER_H

#include <algorithm>
#include <initializer_list>

#include <wx/slider.h>
#include <wx/spinctrl.h>

#include "muuli_wdr.h"

// The numeric control remains the preference's source of truth. Zero keeps its
// existing meaning (unlimited); the slider expands to accommodate typed values.
class BandwidthLimitSlider : public wxSlider
{
public:
	BandwidthLimitSlider(wxWindow *parent, wxWindowID id, wxSpinCtrl *limit)
	: wxSlider(parent, id, 0, 0, 1024)
	, m_limit(limit)
	{
		SetName(limit->GetId() == IDC_MAXDOWN ? _("Download limit") : _("Upload limit"));
		SetToolTip(_("Bandwidth limit in KiB/s (0 means unlimited). Use the numeric field for larger "
			     "values."));
		Sync();
		Bind(wxEVT_SLIDER, [this](wxCommandEvent &) {
			m_limit->SetValue(GetValue());
			// Preserve the existing upload/download ratio enforcement.
			wxSpinEvent changed(wxEVT_SPINCTRL, m_limit->GetId());
			changed.SetEventObject(m_limit);
			changed.SetPosition(GetValue());
			m_limit->GetEventHandler()->ProcessEvent(changed);
		});
		limit->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) {
			Sync();
			event.Skip();
		});
		limit->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent &event) {
			Sync();
			event.Skip();
		});
	}

	void Sync()
	{
		const int value = m_limit->GetValue();
		SetRange(0, std::max(GetMax(), std::max(1024, value)));
		SetValue(value);
	}

private:
	wxSpinCtrl *m_limit;
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
