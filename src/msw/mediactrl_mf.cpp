///////////////////////////////////////////////////////////////////////////////
// Name:        src/msw/mediactrl_mf.cpp
// Purpose:     wxMediaCtrl backend using IMFMediaEngine
// Author:      PB
// Created:     2026-06-25
// Copyright:   (c) 2026 wxWidgets development team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

/*

This wxMediaCtrl backend uses IMFMediaEngine API from Microsoft Media Foundation,
available since Windows 8: https://learn.microsoft.com/windows/win32/api/mfmediaengine/nn-mfmediaengine-imfmediaengine

The backend does not support proxy or showing player controls.

It is built only when mfmediaengine.h header is available. The header is provided by all
supported MSVC versions and mingw-w64 v12 and newer.

It does not require linking any additional library, the interface GUIDs are defined in this
file and the functions loaded at run-time.

*/


// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if defined(__VISUALC__) || wxHAS_CXX17_INCLUDE(<mfmediaengine.h>)
    #define wxUSE_MEDIACTRL_MF 1
#else
    #define wxUSE_MEDIACTRL_MF 0
#endif


#if wxUSE_MEDIACTRL && wxUSE_MEDIACTRL_MF

#ifndef WX_PRECOMP
    #include "wx/intl.h"
    #include "wx/log.h"
    #include "wx/module.h"
    #include "wx/timer.h"
#endif

#include "wx/dynlib.h"
#include "wx/filename.h"
#include "wx/filesys.h"
#include "wx/mediactrl.h"
#include "wx/msw/ole/comimpl.h"
#include "wx/msw/ole/oleutils.h"
#include "wx/msw/private/comptr.h"

#include <cmath>

#include <mfapi.h>
#include <mfmediaengine.h>

#include <initguid.h>


static const char* TRACE_MEDIACTRL_MF = "mediactrl_mf";


// ----------------------------------------------------------------------------
// wxMFMediaBackend declaration
// ----------------------------------------------------------------------------
class wxMFMediaBackend : public wxMediaBackendCommonBase
{
public:
    wxMFMediaBackend() = default;
    virtual ~wxMFMediaBackend();

    virtual bool CreateControl(wxControl* ctrl, wxWindow* parent,
                               wxWindowID id,
                               const wxPoint& pos,
                               const wxSize& size,
                               long style,
                               const wxValidator& validator,
                               const wxString& name) override;

    virtual bool Play() override;
    virtual bool Pause() override;
    virtual bool Stop() override;

    virtual bool Load(const wxString& fileName) override;
    virtual bool Load(const wxURI& location) override;
    virtual bool Load(const wxURI& location, const wxURI& proxy) override;

    virtual wxMediaState GetState() override;

    virtual bool SetPosition(wxLongLong where) override;
    virtual wxLongLong GetPosition() override;
    virtual wxLongLong GetDuration() override;

    virtual wxSize GetVideoSize() const override;

    virtual double GetPlaybackRate() override;
    virtual bool SetPlaybackRate(double) override;

    virtual double GetVolume() override;
    virtual bool SetVolume(double) override;

    wxControl* GetControl() const { return m_ctrl; }
    void OnNotify(DWORD dwEvent, DWORD_PTR param1, DWORD param2);

    static bool Uninitialize();

private:
    // If the media does not load within this time,
    // assume it is not playable and report error to the user.
    // This can happen e.g. for some unavailable network media.
    static constexpr int ms_loadingTimeout = 6000; // ms

    // these Media Foundation functions are available on all supported Windows
    // versions, but still load them dynamically to avoid linking mfplat.lib
    typedef HRESULT (WINAPI* MFStartup_t)(ULONG, DWORD);
    typedef HRESULT (WINAPI* MFShutdown_t)();
    typedef HRESULT (WINAPI* MFCreateAttributes_t)(IMFAttributes**, UINT32);

    static wxDynamicLibrary ms_dllMFPlat;
    static MFStartup_t ms_pfnMFStartup;
    static MFShutdown_t ms_pfnMFShutdown;
    static MFCreateAttributes_t ms_pfnMFCreateAttributes;

    static wxCOMPtr<IMFMediaEngineClassFactory> ms_factory;

    wxCOMPtr<IMFMediaEngine> m_engine;
    wxMediaState m_state{wxMEDIASTATE_STOPPED};
    wxTimer m_loadingTimer;

    bool CreateEngine();
    bool DoLoad(const wxString& source);

    static bool Initialize();

    wxDECLARE_DYNAMIC_CLASS(wxMFMediaBackend);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxMFMediaBackend, wxMediaBackend);

wxDynamicLibrary wxMFMediaBackend::ms_dllMFPlat;
wxMFMediaBackend::MFStartup_t wxMFMediaBackend::ms_pfnMFStartup = nullptr;
wxMFMediaBackend::MFShutdown_t wxMFMediaBackend::ms_pfnMFShutdown = nullptr;
wxMFMediaBackend::MFCreateAttributes_t wxMFMediaBackend::ms_pfnMFCreateAttributes = nullptr;

wxCOMPtr<IMFMediaEngineClassFactory>wxMFMediaBackend::ms_factory;

// ============================================================================
// implementation
// ============================================================================

namespace
{
// ----------------------------------------------------------------------------
// Define required GUIDs so we don't need to link mfuuid.lib.
// ----------------------------------------------------------------------------
DEFINE_GUID(wxCLSID_MFMediaEngineClassFactory,
            0xb44392da, 0x499b, 0x446b, 0xa4, 0xcb, 0x0, 0x5f, 0xea, 0xd0, 0xe6, 0xd5);
DEFINE_GUID(wxIID_IMFMediaEngineClassFactory,
            0x4d645ace, 0x26aa, 0x4688, 0x9b, 0xe1, 0xdf, 0x35, 0x16, 0x99, 0x0b, 0x93);
DEFINE_GUID(wxIID_IMFMediaEngineNotify,
            0xfee7c112, 0xe776, 0x42b5, 0x9b, 0xbf, 0x00, 0x48, 0x52, 0x4e, 0x2b, 0xd5);


// ----------------------------------------------------------------------------
// wxMediaEngineNotify: Implements the callback for MediaEngine events.
// ----------------------------------------------------------------------------
class wxMediaEngineNotify : public IMFMediaEngineNotify
{
public:
	wxMediaEngineNotify(wxMFMediaBackend* backend) : m_backend(backend)
	{}

	wxSTDMETHODIMP EventNotify(DWORD event, DWORD_PTR param1, DWORD param2)
	{
        m_backend->GetControl()->CallAfter([this, event, param1, param2]()
            {
                m_backend->OnNotify(event, param1, param2);
            });
		return S_OK;
	}

    DECLARE_IUNKNOWN_METHODS;
private:
    wxMFMediaBackend* m_backend{nullptr};

    wxDECLARE_NO_COPY_CLASS(wxMediaEngineNotify);
};

BEGIN_IID_TABLE(wxMediaEngineNotify)
ADD_IID(Unknown)
ADD_RAW_IID(wxIID_IMFMediaEngineNotify)
END_IID_TABLE;

IMPLEMENT_IUNKNOWN_METHODS(wxMediaEngineNotify)


} // unnamed namespace


// if the HRESULT is a failure: (1) log the API error and if not empty also
// the user error, and (2) return false
#define CHECK_HR_USER(hr, apiError, userError)                     \
{                                                                  \
    if ( FAILED(hr) )                                              \
    {                                                              \
        wxLogApiError(apiError, hr);                               \
        if ( !wxString(userError).empty() ) wxLogError(userError); \
        return false;                                              \
    }                                                              \
}

// same as CHECK_HR_USER but without showing the error to the user
#define CHECK_HR(hr, apiError)      \
{                                   \
    CHECK_HR_USER(hr, apiError, "") \
}


// ----------------------------------------------------------------------------
// wxMFMediaBackend definition
// ----------------------------------------------------------------------------
wxMFMediaBackend::~wxMFMediaBackend()
{
    if ( m_engine )
    {
        wxLogTrace(TRACE_MEDIACTRL_MF, "Shutting down engine");

        const HRESULT hr = m_engine->Shutdown();

        if ( FAILED(hr) )
            wxLogApiError("IMFMediaEngine::Shutdown()", hr);
    }
}

bool wxMFMediaBackend::CreateControl(wxControl* ctrl, wxWindow* parent,
                                     wxWindowID id,
                                     const wxPoint& pos,
                                     const wxSize& size,
                                     long style,
                                     const wxValidator& validator,
                                     const wxString& name)
{
    if ( !ctrl->wxControl::Create(parent, id, pos, size,
                            (style & ~wxBORDER_MASK) | wxBORDER_NONE,
                            validator, name) )
    {
        return false;
    }

    m_ctrl = wxStaticCast(ctrl, wxMediaCtrl);
    m_ctrl->SetBackgroundColour(*wxBLACK);

    if ( !CreateEngine() )
        return false;

    m_loadingTimer.Bind(wxEVT_TIMER, [this](wxTimerEvent&)
        {
            wxLogError(_("Could not play media (timed out)."));
        });

    return true;
}

bool wxMFMediaBackend::Load(const wxString& fileName)
{
    return DoLoad(wxFileSystem::FileNameToURL(wxFileName(fileName)));
}

bool wxMFMediaBackend::Load(const wxURI& location)
{
    return DoLoad(location.BuildURI());
}

bool wxMFMediaBackend::Load(const wxURI& WXUNUSED(location), const wxURI& WXUNUSED(proxy))
{
    return false; // proxy not supported by this backend
}

bool wxMFMediaBackend::DoLoad(const wxString& source)
{
    wxCHECK(m_engine, false);

    wxLogTrace(TRACE_MEDIACTRL_MF, "Opening '%s'...", source);
    const HRESULT hr = m_engine->SetSource(wxBasicString(source));
    CHECK_HR_USER(hr, "IMFMediaEngine::SetSource()", _("Could not load media."));
    return true;
}

bool wxMFMediaBackend::Play()
{
    wxCHECK(m_engine, false);

    const HRESULT hr = m_engine->Play();
    CHECK_HR_USER(hr, "IMFMediaEngine::Play()", _("Could not play media."));
    m_state = wxMEDIASTATE_PLAYING;
    return true;
}

bool wxMFMediaBackend::Pause()
{
    wxCHECK(m_engine, false);

    const HRESULT hr = m_engine->Pause();
    CHECK_HR_USER(hr, "IMFMediaEngine::Pause()", _("Could not pause media."));
    m_state = wxMEDIASTATE_PAUSED;
    return true;
}

bool wxMFMediaBackend::Stop()
{
   wxCHECK(m_engine, false);

   if ( !Pause() )
       return false;

    SetPosition(wxLongLong(0));
    m_state = wxMEDIASTATE_STOPPED;
    return true;
}

bool wxMFMediaBackend::SetPosition(wxLongLong where)
{
    wxCHECK(m_engine, false);

    const double seconds = where.ToDouble() / 1000.;
    const HRESULT hr = m_engine->SetCurrentTime(seconds);
    CHECK_HR_USER(hr, "IMFMediaEngine::SetCurrentTime()", _("Could not set media position."));
    return true;
}

wxLongLong wxMFMediaBackend::GetPosition()
{
    wxCHECK(m_engine, 0);

    const double seconds = m_engine->GetCurrentTime();
    return wxLongLong(static_cast<wxLongLong_t>(seconds * 1000.));
}

double wxMFMediaBackend::GetVolume()
{
    wxCHECK(m_engine, 0.);

    return m_engine->GetVolume();
}

bool wxMFMediaBackend::SetVolume(double dVolume)
{
    wxCHECK(m_engine, false);

    const HRESULT hr = m_engine->SetVolume(dVolume);
    CHECK_HR(hr,"IMFMediaEngine::SetVolume()");
    return true;
}

wxLongLong wxMFMediaBackend::GetDuration()
{
    wxCHECK(m_engine, 0);

    const double duration = m_engine->GetDuration();
    if ( !std::isnormal(duration) )
        return 0;
    return wxLongLong(static_cast<wxLongLong_t>(duration * 1000.));
}

wxMediaState wxMFMediaBackend::GetState()
{
    return m_state;
}

double wxMFMediaBackend::GetPlaybackRate()
{
    wxCHECK(m_engine, 1.);

    return m_engine->GetPlaybackRate();
}

bool wxMFMediaBackend::SetPlaybackRate(double dRate)
{
    wxCHECK(m_engine, false);

    const HRESULT hr = m_engine->SetPlaybackRate(dRate);
    CHECK_HR(hr, "IMFMediaEngine::SetPlaybackRate()");
    return true;
}

wxSize wxMFMediaBackend::GetVideoSize() const
{
    wxCHECK(m_engine, wxSize());

    if ( !m_engine->HasVideo() )
        return wxSize();

    DWORD cx = 0, cy = 0;
    const HRESULT hr = m_engine->GetNativeVideoSize(&cx, &cy);

    if ( SUCCEEDED(hr) )
        return wxSize(static_cast<int>(cx), static_cast<int>(cy));

    wxLogApiError("IMFMediaEngine::GetNativeVideoSize()", hr);
    return wxSize();
}

#if wxDEBUG_LEVEL >= 2

namespace
{
    void TraceMediaEngineEvent(MF_MEDIA_ENGINE_EVENT event, DWORD_PTR param1, DWORD param2)
    {
#define EVENT_TO_STRING(value) case value: name = wxS(#value); break;
        wxString name;

        switch ( event )
        {
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_LOADSTART)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_PROGRESS)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_SUSPEND)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_ABORT)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_ERROR)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_EMPTIED)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_STALLED)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_PLAY)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_PAUSE)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_LOADEDDATA)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_WAITING)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_PLAYING)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_CANPLAY)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_CANPLAYTHROUGH)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_SEEKING)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_SEEKED)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_TIMEUPDATE)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_ENDED)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_RATECHANGE)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_DURATIONCHANGE)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_VOLUMECHANGE)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_FORMATCHANGE)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_PURGEQUEUEDEVENTS)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_TIMELINE_MARKER)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_BALANCECHANGE)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_DOWNLOADCOMPLETE)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_BUFFERINGSTARTED)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_BUFFERINGENDED)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_FRAMESTEPCOMPLETED)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_FIRSTFRAMEREADY)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_TRACKSCHANGE)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_OPMINFO)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_RESOURCELOST)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_DELAYLOADEVENT_CHANGED)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_STREAMRENDERINGERROR)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_SUPPORTEDRATES_CHANGED)
            EVENT_TO_STRING(MF_MEDIA_ENGINE_EVENT_AUDIOENDPOINTCHANGE)
            default: name = wxS("Unknown Media Engine event"); break;
        }

        wxLogTrace(TRACE_MEDIACTRL_MF,"%s (param1=%d, param2=%d)",
            name, static_cast<int>(param1), static_cast<int>(param2));
    }
} // unnamed namespace

#endif // wxDEBUG_LEVEL >= 2

void wxMFMediaBackend::OnNotify(DWORD dwEvent, DWORD_PTR param1, DWORD param2)
{
    const MF_MEDIA_ENGINE_EVENT event = static_cast<MF_MEDIA_ENGINE_EVENT>(dwEvent);

    switch ( event )
    {
         case MF_MEDIA_ENGINE_EVENT_LOADSTART:
            m_loadingTimer.StartOnce(ms_loadingTimeout);
            break;

        case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA:
            if ( !GetVideoSize().IsEmpty() )
                NotifyMovieSizeChanged();
            break;

        case MF_MEDIA_ENGINE_EVENT_CANPLAYTHROUGH:
            m_loadingTimer.Stop();
            NotifyMovieLoaded();
            break;

        case MF_MEDIA_ENGINE_EVENT_PLAY:
            QueuePlayEvent();
            break;

        case MF_MEDIA_ENGINE_EVENT_PAUSE:
            if ( GetState() == wxMEDIASTATE_PAUSED )
                QueuePauseEvent();
            else
                QueueStopEvent();
            break;

        case MF_MEDIA_ENGINE_EVENT_ENDED:
            if ( SendStopEvent() )
            {
                m_state = wxMEDIASTATE_STOPPED;
                QueueFinishEvent();
            }
            break;

        case MF_MEDIA_ENGINE_EVENT_RESOURCELOST:
            wxLogError(_("Could not play media (resource lost)."));
            m_state = wxMEDIASTATE_STOPPED;
            QueueFinishEvent();
            break;

        case MF_MEDIA_ENGINE_EVENT_ERROR:
        {
            m_loadingTimer.Stop();
            wxLogError(_("Could not play media (error code %d)."), static_cast<MF_MEDIA_ENGINE_ERR>(param1));
            break;
        }
    }

#if wxDEBUG_LEVEL >= 2
    TraceMediaEngineEvent(event, param1, param2);
#else
    wxUnusedVar(param2);
#endif
}

bool wxMFMediaBackend::CreateEngine()
{
    if ( !Initialize() )
        return false;

    wxLogTrace(TRACE_MEDIACTRL_MF, "Creating engine");

    const wxString errorMsg(_("Could not create Media Foundation engine."));

    const UINT32 elementCount = 2; // MF_MEDIA_ENGINE_PLAYBACK_HWND + MF_MEDIA_ENGINE_CALLBACK
    wxCOMPtr<IMFAttributes> attributes;
    HRESULT hr;

    hr = ms_pfnMFCreateAttributes(&attributes, elementCount);
    CHECK_HR_USER(hr,"MFCreateAttributes()", errorMsg);

    hr = attributes->SetUINT64(MF_MEDIA_ENGINE_PLAYBACK_HWND, reinterpret_cast<UINT64>(m_ctrl->GetHWND()));
    CHECK_HR_USER(hr,"IMFAttributes::SetUINT64(MF_MEDIA_ENGINE_PLAYBACK_HWND ...)", errorMsg);

    wxCOMPtr<wxMediaEngineNotify> notify(new wxMediaEngineNotify(this));

    hr = attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, static_cast<IMFMediaEngineNotify*>(notify));
    CHECK_HR_USER(hr,"IMFAttributes::SetUnknown(MF_MEDIA_ENGINE_CALLBACK ...)", errorMsg);

    hr = ms_factory->CreateInstance(0, attributes, &m_engine);
    CHECK_HR_USER(hr,"IMFMediaEngineClassFactory::CreateInstance()", errorMsg);

    return true;
}

bool wxMFMediaBackend::Initialize()
{
    if ( ms_factory )
        return true; // already initialized

    wxLogTrace(TRACE_MEDIACTRL_MF, "Initializing Media Foundation");

    const wxString errorMsg(_("Could not initialize Media Foundation."));

    if ( ms_dllMFPlat.Load(wxS("Mfplat.dll"), wxDL_VERBATIM | wxDL_QUIET) )
    {
        wxDL_INIT_FUNC(ms_pfn, MFStartup, ms_dllMFPlat);
        wxDL_INIT_FUNC(ms_pfn, MFShutdown, ms_dllMFPlat);
        wxDL_INIT_FUNC(ms_pfn, MFCreateAttributes, ms_dllMFPlat);
    }

    if ( !ms_pfnMFStartup || !ms_pfnMFShutdown || !ms_pfnMFCreateAttributes )
    {
        wxLogTrace(TRACE_MEDIACTRL_MF, "Could not load Mfplat.dll or functions from it");
        wxLogError(errorMsg);
        return false;
    }

    HRESULT hr = ms_pfnMFStartup(MF_VERSION, MFSTARTUP_FULL);
    CHECK_HR_USER(hr, "MFStartup()", errorMsg);

    hr = ::CoCreateInstance(wxCLSID_MFMediaEngineClassFactory, nullptr,
                            CLSCTX_INPROC_SERVER,
                            wxIID_IMFMediaEngineClassFactory, (LPVOID*)&ms_factory);
    CHECK_HR_USER(hr, "CoCreateInstance(CLSID_MFMediaEngineClassFactory ...)", errorMsg);

    return true;
}

bool wxMFMediaBackend::Uninitialize()
{
    if ( ms_factory )
    {
        wxLogTrace(TRACE_MEDIACTRL_MF, "Shutting down Media Foundation");

        ms_factory.reset(nullptr);

        const HRESULT hr = ms_pfnMFShutdown();

        if ( FAILED(hr) )
            wxLogApiError("MFShutdown()", hr);

        ms_pfnMFStartup = nullptr;
        ms_pfnMFShutdown = nullptr;
        ms_pfnMFCreateAttributes = nullptr;
        ms_dllMFPlat.Unload();
    }

    return true;
}


// ----------------------------------------------------------------------------
// Module ensuring that OLE is initialized and Media Foundation shut down on exit.
// Media Foundation is initialized only when the first wxMFMediaBackend is created,
// so not with this module.
// ----------------------------------------------------------------------------
class wxMFMediaBackendModule : public wxModule
{
public:
    wxMFMediaBackendModule()
    {
        AddDependency("wxOleInitModule");
    }

    virtual bool OnInit() override
    {
        return true;
    }

    virtual void OnExit() override
    {
        wxMFMediaBackend::Uninitialize();
    }

private:
    wxDECLARE_DYNAMIC_CLASS(wxMFMediaBackendModule);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxMFMediaBackendModule, wxModule);


// Allow the user code to use wxFORCE_LINK_MODULE() to ensure that this object
// file is not discarded by the linker.
#include "wx/link.h"
wxFORCE_LINK_THIS_MODULE(wxmediabackend_mf)

#endif // wxUSE_MEDIACTRL && wxUSE_MEDIACTRL_MF