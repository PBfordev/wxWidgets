/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/cshelp.cpp
// Purpose:     Context sensitive help class implementation
// Author:      Julian Smart, Vadim Zeitlin
// Created:     08/09/2000
// Copyright:   (c) 2000 Julian Smart, Vadim Zeitlin
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#if wxUSE_HELP

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/module.h"
#endif

#include "wx/tipwin.h"
#include "wx/cshelp.h"

#if wxUSE_MS_HTML_HELP
    #include "wx/msw/helpchm.h"     // for ShowContextHelpPopup
#endif

// ----------------------------------------------------------------------------
// wxContextHelpEvtHandler private class
// ----------------------------------------------------------------------------

// This class exists in order to eat events until the left mouse button is
// pressed
class wxContextHelpEvtHandler: public wxEvtHandler
{
public:
    wxContextHelpEvtHandler(wxContextHelp* contextHelp)
    {
        m_contextHelp = contextHelp;
    }

    virtual bool ProcessEvent(wxEvent& event) override;

//// Data
    wxContextHelp* m_contextHelp;

    wxDECLARE_NO_COPY_CLASS(wxContextHelpEvtHandler);
};

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wxContextHelp
// ----------------------------------------------------------------------------

/*
 * Invokes context-sensitive help
 */


wxIMPLEMENT_DYNAMIC_CLASS(wxContextHelp, wxObject);

wxContextHelp::wxContextHelp(wxWindow* win, bool beginHelp)
{
    m_inHelp = false;

    if (beginHelp)
        BeginContextHelp(win);
}

wxContextHelp::~wxContextHelp()
{
    if (m_inHelp)
        EndContextHelp();
}

// Begin 'context help mode'
bool wxContextHelp::BeginContextHelp(wxWindow* win)
{
    if (!win)
        win = wxTheApp->GetTopWindow();
    if (!win)
        return false;

    wxCursor cursor(wxCURSOR_QUESTION_ARROW);
    wxCursor oldCursor = win->GetCursor();
    win->SetCursor(cursor);

#ifdef __WXMAC__
    wxSetCursor(cursor);
#endif

    m_status = false;

    win->PushEventHandler(new wxContextHelpEvtHandler(this));

    win->CaptureMouse();

    EventLoop();

    win->ReleaseMouse();

    win->PopEventHandler(true);

    win->SetCursor(oldCursor);

#ifdef __WXMAC__
    wxSetCursor(wxNullCursor);
#endif

    if (m_status)
    {
        wxPoint pt;
        wxWindow* winAtPtr = wxFindWindowAtPointer(pt);

#if 0
        if (winAtPtr)
        {
            printf("Picked %s (%d)\n", winAtPtr->GetName().c_str(),
                   winAtPtr->GetId());
        }
#endif

        if (winAtPtr)
            DispatchEvent(winAtPtr, pt);
    }

    return true;
}

bool wxContextHelp::EndContextHelp()
{
    m_inHelp = false;

    return true;
}

bool wxContextHelp::EventLoop()
{
    m_inHelp = true;

    while ( m_inHelp )
    {
        if (wxTheApp->Pending())
        {
            wxTheApp->Dispatch();
        }
        else
        {
            wxTheApp->ProcessIdle();
        }
    }

    return true;
}

bool wxContextHelpEvtHandler::ProcessEvent(wxEvent& event)
{
    if (event.GetEventType() == wxEVT_LEFT_DOWN)
    {
        m_contextHelp->SetStatus(true);
        m_contextHelp->EndContextHelp();
        return true;
    }

    if ((event.GetEventType() == wxEVT_CHAR) ||
        (event.GetEventType() == wxEVT_KEY_DOWN) ||
        (event.GetEventType() == wxEVT_ACTIVATE) ||
        (event.GetEventType() == wxEVT_MOUSE_CAPTURE_CHANGED))
    {
        // May have already been set to true by a left-click
        //m_contextHelp->SetStatus(false);
        m_contextHelp->EndContextHelp();
        return true;
    }

    if ((event.GetEventType() == wxEVT_PAINT) ||
        (event.GetEventType() == wxEVT_ERASE_BACKGROUND))
    {
        event.Skip();
        return false;
    }

    return true;
}

// Dispatch the help event to the relevant window
bool wxContextHelp::DispatchEvent(wxWindow* win, const wxPoint& pt)
{
    wxCHECK_MSG( win, false, wxT("win parameter can't be null") );

    wxHelpEvent helpEvent(wxEVT_HELP, win->GetId(), pt,
                          wxHelpEvent::Origin_HelpButton);
    helpEvent.SetEventObject(win);

    return win->GetEventHandler()->ProcessEvent(helpEvent);
}

// ----------------------------------------------------------------------------
// wxContextHelpButton
// ----------------------------------------------------------------------------

/*
 * wxContextHelpButton
 * You can add this to your dialogs (especially on non-Windows platforms)
 * to put the application into context help mode.
 */

static const char * csquery_xpm[] = {
"12 11 2 1",
"  c None",
". c #000000",
"            ",
"    ....    ",
"   ..  ..   ",
"   ..  ..   ",
"      ..    ",
"     ..     ",
"     ..     ",
"            ",
"     ..     ",
"     ..     ",
"            "};


wxIMPLEMENT_CLASS(wxContextHelpButton, wxBitmapButton);

wxBEGIN_EVENT_TABLE(wxContextHelpButton, wxBitmapButton)
    EVT_BUTTON(wxID_CONTEXT_HELP, wxContextHelpButton::OnContextHelp)
wxEND_EVENT_TABLE()

bool wxContextHelpButton::Create(wxWindow* parent,
                                         wxWindowID id,
                                         const wxPoint& pos,
                                         const wxSize& size,
                                         long style)
{
    return wxBitmapButton::Create(parent, id,
                                  wxBitmap(csquery_xpm),
                                  pos, size, style);
}

void wxContextHelpButton::OnContextHelp(wxCommandEvent& WXUNUSED(event))
{
    wxContextHelp contextHelp(GetParent());
}

// ----------------------------------------------------------------------------
// wxHelpProvider
// ----------------------------------------------------------------------------

wxHelpProvider *wxHelpProvider::ms_helpProvider = nullptr;

// trivial implementation of some methods which we don't want to make pure
// virtual for convenience

void wxHelpProvider::AddHelp(wxWindowBase * WXUNUSED(window),
                             const wxString& WXUNUSED(text))
{
}

void wxHelpProvider::AddHelp(wxWindowID WXUNUSED(id),
                             const wxString& WXUNUSED(text))
{
}

// removes the association
void wxHelpProvider::RemoveHelp(wxWindowBase* WXUNUSED(window))
{
}

wxHelpProvider::~wxHelpProvider()
{
}

wxString wxHelpProvider::GetHelpTextMaybeAtPoint(wxWindowBase *window)
{
    if ( m_helptextAtPoint != wxDefaultPosition ||
            m_helptextOrigin != wxHelpEvent::Origin_Unknown )
    {
        wxCHECK_MSG( window, wxEmptyString, wxT("window must not be null") );

        wxPoint pt = m_helptextAtPoint;
        wxHelpEvent::Origin origin = m_helptextOrigin;

        m_helptextAtPoint = wxDefaultPosition;
        m_helptextOrigin = wxHelpEvent::Origin_Unknown;

        return window->GetHelpTextAtPoint(pt, origin);
    }

    return GetHelp(window);
}

// ----------------------------------------------------------------------------
// wxSimpleHelpProvider
// ----------------------------------------------------------------------------

wxString wxSimpleHelpProvider::GetHelp(const wxWindowBase *window)
{
    const auto it = m_hashWindows.find(window);
    if ( it != m_hashWindows.end() )
        return it->second;

    const auto it2 = m_hashIds.find(window->GetId());
    if ( it2 != m_hashIds.end() )
        return it2->second;

    return {};
}

void wxSimpleHelpProvider::AddHelp(wxWindowBase *window, const wxString& text)
{
    m_hashWindows[window] = text;
}

void wxSimpleHelpProvider::AddHelp(wxWindowID id, const wxString& text)
{
    m_hashIds[id] = text;
}

// removes the association
void wxSimpleHelpProvider::RemoveHelp(wxWindowBase* window)
{
    m_hashWindows.erase(window);
}

bool wxSimpleHelpProvider::ShowHelp(wxWindowBase *window)
{
#if wxUSE_MS_HTML_HELP || wxUSE_TIPWINDOW
#if wxUSE_MS_HTML_HELP
    // m_helptextAtPoint will be reset by GetHelpTextMaybeAtPoint(), stash it
    const wxPoint posTooltip = m_helptextAtPoint;
#endif // wxUSE_MS_HTML_HELP

    const wxString text = GetHelpTextMaybeAtPoint(window);

    if ( !text.empty() )
    {
        // use the native help popup style if it's available
#if wxUSE_MS_HTML_HELP
        if ( !wxCHMHelpController::ShowContextHelpPopup
                                   (
                                        text,
                                        posTooltip,
                                        (wxWindow *)window
                                   ) )
#endif // wxUSE_MS_HTML_HELP
        {
#if wxUSE_TIPWINDOW
            static wxTipWindow* s_tipWindow = nullptr;

            if ( s_tipWindow )
            {
                // Prevent s_tipWindow being nulled in OnIdle, thereby removing
                // the chance for the window to be closed by ShowHelp
                s_tipWindow->SetTipWindowPtr(nullptr);
                s_tipWindow->Close();
            }

            s_tipWindow = new wxTipWindow((wxWindow *)window, text,
                                            100, &s_tipWindow);
#else // !wxUSE_TIPWINDOW
            // we tried wxCHMHelpController but it failed and we don't have
            // wxTipWindow to fall back on, so
            return false;
#endif // wxUSE_TIPWINDOW
        }

        return true;
    }
#else // !wxUSE_MS_HTML_HELP && !wxUSE_TIPWINDOW
    wxUnusedVar(window);
#endif // wxUSE_MS_HTML_HELP || wxUSE_TIPWINDOW

    return false;
}

// ----------------------------------------------------------------------------
// wxHelpControllerHelpProvider
// ----------------------------------------------------------------------------

wxHelpControllerHelpProvider::wxHelpControllerHelpProvider(wxHelpControllerBase* hc)
{
    m_helpController = hc;
}

bool wxHelpControllerHelpProvider::ShowHelp(wxWindowBase *window)
{
    const wxString text = GetHelpTextMaybeAtPoint(window);

    if ( text.empty() )
        return false;

    if ( m_helpController )
    {
        // if it's a numeric topic, show it
        long topic;
        if ( text.ToLong(&topic) )
            return m_helpController->DisplayContextPopup(topic);

        // otherwise show the text directly
        if ( m_helpController->DisplayTextPopup(text, wxGetMousePosition()) )
            return true;
    }

    // if there is no help controller or it's not capable of showing the help,
    // fallback to the default method
    return wxSimpleHelpProvider::ShowHelp(window);
}

// Convenience function for turning context id into wxString
wxString wxContextId(int id)
{
    return wxString::Format(wxT("%d"), id);
}

// ----------------------------------------------------------------------------
// wxHelpProviderModule: module responsible for cleaning up help provider.
// ----------------------------------------------------------------------------

class wxHelpProviderModule : public wxModule
{
public:
    bool OnInit() override;
    void OnExit() override;

private:
    wxDECLARE_DYNAMIC_CLASS(wxHelpProviderModule);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxHelpProviderModule, wxModule);

bool wxHelpProviderModule::OnInit()
{
    // Probably we don't want to do anything by default,
    // since it could pull in extra code
    // wxHelpProvider::Set(new wxSimpleHelpProvider);

    return true;
}

void wxHelpProviderModule::OnExit()
{
    if (wxHelpProvider::Get())
    {
        delete wxHelpProvider::Get();
        wxHelpProvider::Set(nullptr);
    }
}

#endif // wxUSE_HELP
