#ifndef AMULE_BANDWIDTH_EQUIVALENT_LABEL_H
#define AMULE_BANDWIDTH_EQUIVALENT_LABEL_H

#include <initializer_list>

#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include "muuli_wdr.h"

class BandwidthEquivalentLabel : public wxStaticText
{
public:
	BandwidthEquivalentLabel(wxWindow *parent, wxWindowID id, wxSpinCtrl *limit)
	: wxStaticText(parent,
		  id,
		  wxString::Format(_("KiB/s (%.2f MiB/s)"), limit->GetMax() / 1024.0),
		  wxDefaultPosition,
		  wxDefaultSize,
		  wxST_NO_AUTORESIZE)
	, m_limit(limit)
	{
		// Reserve both the largest rate and the translated unlimited label.
		wxSize minimum = GetBestSize();
		minimum.IncTo(GetTextExtent(_("KiB/s (unlimited)")));
		SetMinSize(minimum);
		Sync();
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
		SetLabel(m_limit->GetValue() == 0
				 ? _("KiB/s (unlimited)")
				 : wxString::Format(_("KiB/s (%.2f MiB/s)"), m_limit->GetValue() / 1024.0));
	}

private:
	wxSpinCtrl *m_limit;
};

inline void SyncBandwidthEquivalents(wxWindow *window)
{
	for (const int id : { IDC_DOWNLOAD_EQUIVALENT, IDC_UPLOAD_EQUIVALENT }) {
		if (auto *label = dynamic_cast<BandwidthEquivalentLabel *>(window->FindWindow(id))) {
			label->Sync();
		}
	}
}

#endif
