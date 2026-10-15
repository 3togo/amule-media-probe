#ifndef AMULE_SHUTDOWN_STATUS_H
#define AMULE_SHUTDOWN_STATUS_H

#include "SplashScreen.h"

#include <wx/app.h>
#include <wx/evtloop.h>
#include <wx/eventfilter.h>

// Shutdown owns this window until all core cleanup is complete. Pump only paint
// and layout events: timers, sockets, queued callbacks and user commands must
// not reenter core objects while they are being destroyed.
class CShutdownStatus : public CSplashScreen
{
public:
	CShutdownStatus()
	: CSplashScreen(nullptr, Phase::Shutdown)
	{
		Show();
	}

	void SetStatus(const wxString &status)
	{
		RepaintFilter filter(this);
		SetProgress(status, 0, true);
		// OnExit may run after the main loop has stopped (including macOS session
		// end). A temporary loop supplies the same restricted repaint there.
		if (auto *loop = wxEventLoopBase::GetActive()) {
			loop->YieldFor(wxEVT_CATEGORY_UI);
		} else {
			wxEventLoop temporaryLoop;
			wxEventLoopActivator active(&temporaryLoop);
			temporaryLoop.YieldFor(wxEVT_CATEGORY_UI);
		}
		Update();
	}

private:
	// GTK's nested native loop dispatches GLib timer sources even when YieldFor
	// requests only UI events. Block every wx event outside this window while
	// repainting, including those timers and events directed at the application.
	class RepaintFilter : public wxEventFilter
	{
	public:
		explicit RepaintFilter(wxWindow *window)
		: m_window(window)
		{
			// Native loops on some ports drain pending wx events while yielding.
			// Keep those events queued instead of consuming them in the filter.
			wxTheApp->SuspendProcessingOfPendingEvents();
			wxEvtHandler::AddFilter(this);
		}
		~RepaintFilter() override
		{
			wxEvtHandler::RemoveFilter(this);
			wxTheApp->ResumeProcessingOfPendingEvents();
		}

		int FilterEvent(wxEvent &event) override
		{
			for (auto *window = wxDynamicCast(event.GetEventObject(), wxWindow); window;
				window = window->GetParent()) {
				if (window == m_window) {
					return Event_Skip;
				}
			}
			return Event_Processed;
		}

	private:
		wxWindow *m_window;
	};
};

#endif
