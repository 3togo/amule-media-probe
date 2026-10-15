// Copyright (c) 2026 aMule Team
// SPDX-License-Identifier: GPL-2.0-or-later
#include <wx/wx.h>

#include "InternalEvents.h"
#include "ShutdownStatus.h"

#include <iostream>
#include <stdexcept>

class ShutdownStatusTestApp : public wxApp
{
public:
	bool OnInit() override { return true; }
};

wxIMPLEMENT_APP_NO_MAIN(ShutdownStatusTestApp);

static void Require(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

int main(int argc, char **argv)
{
	if (!wxEntryStart(argc, argv)) {
		return 77;
	}
	int result = 0;
	if (!wxTheApp->CallOnInit()) {
		result = 1;
	} else {
		bool internalRan = false;
		bool callbackRan = false;
		bool timerRan = false;
		bool checked = false;
		const wxEventTypeTag<CMuleInternalEvent> internalType(wxNewEventType());
		wxTheApp->Bind(internalType, [&](CMuleInternalEvent &) { internalRan = true; });
		{
			wxFrame anchor(nullptr, wxID_ANY, "Shutdown status regression test");
			anchor.Show();
			wxTheApp->SetTopWindow(&anchor);
			wxTimer background;
			background.Bind(wxEVT_TIMER, [&](wxTimerEvent &) { timerRan = true; });
			wxTimer trigger;
			// Start from a native timer callback in a real running loop. Calling
			// YieldFor outside the loop misses GTK's native timer reentrancy.
			trigger.Bind(wxEVT_TIMER, [&](wxTimerEvent &) {
				checked = true;
				try {
					CShutdownStatus status;
					int paints = 0;
					status.Bind(wxEVT_PAINT, [&](wxPaintEvent &event) {
						++paints;
						event.Skip();
					});
					wxQueueEvent(wxTheApp, new CMuleInternalEvent(internalType));
					wxTheApp->CallAfter([&] { callbackRan = true; });
					background.StartOnce(1);
					wxMilliSleep(
						20); // Make the GLib/native timer ready before repainting.
					for (int i = 0; i < 100 && paints == 0; ++i) {
						status.SetStatus("Saving downloads...");
						wxMilliSleep(5);
					}
					Require(paints > 0, "Shutdown window did not repaint");
					Require(!timerRan, "Native timer ran during shutdown repaint");
					Require(!internalRan, "Core event ran during shutdown repaint");
					Require(!callbackRan, "Queued callback ran during shutdown repaint");
					Require(status.IsShown() && !status.Close(),
						"Status window can be closed");
					status.SetStatus("Finishing shutdown...");
				} catch (const std::exception &error) {
					std::cerr << error.what() << '\n';
					result = 1;
				}
				background.Stop();
				wxTheApp->ExitMainLoop();
			});
			trigger.StartOnce(1);
			wxTheApp->OnRun();
			if (!checked) {
				result = 1;
			}
			// Pending core events and callbacks remain available after repainting.
			wxTheApp->ProcessPendingEvents();
			if (!internalRan || !callbackRan) {
				result = 1;
			}
			wxTheApp->SetTopWindow(nullptr);
		}
		wxTheApp->OnExit();
	}
	wxEntryCleanup();
	return result;
}
