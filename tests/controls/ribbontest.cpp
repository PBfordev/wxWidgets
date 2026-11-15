///////////////////////////////////////////////////////////////////////////////
// Name:        tests/controls/ribbontest.cpp
// Purpose:     wxRibbon unit tests
// Author:      PB
// Created:     2026-11-15
// Copyright:   (c) 2026 wxWidgets development team
///////////////////////////////////////////////////////////////////////////////

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "testprec.h"

#if wxUSE_RIBBON

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/dcclient.h"
    #include "wx/dcmemory.h"
    #include "wx/panel.h"
    #include "wx/settings.h"
    #include "wx/sizer.h"
    #include "wx/textctrl.h"
#endif // WX_PRECOMP

#include "wx/ribbon/art.h"
#include "wx/ribbon/art_mso.h"
#include "wx/ribbon/bar.h"
#include "wx/ribbon/buttonbar.h"
#include "wx/ribbon/gallery.h"
#include "wx/ribbon/panel.h"
#include "wx/ribbon/toolbar.h"
#include "wx/uiaction.h"

#include "asserthelper.h"
#include "waitfor.h"

#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>

// The art providers used by the tests running with all of them: a new art
// provider only needs to be added here to be tested too.
//
// Note that wxRibbonDefaultArtProvider is one of these classes.
#define wxRIBBON_TEST_STANDARD_ART_PROVIDERS \
    wxRibbonMSWArtProvider, wxRibbonMSWFlatArtProvider, wxRibbonAUIArtProvider

// wxRibbonMSOArtProvider is only available with SVG and graphics context
// support.
#if defined(wxHAS_SVG) && wxUSE_GRAPHICS_CONTEXT
    #define wxRIBBON_TEST_MSO_ART_PROVIDER 1

    #define wxRIBBON_TEST_ART_PROVIDERS \
        wxRIBBON_TEST_STANDARD_ART_PROVIDERS, wxRibbonMSOArtProvider
#else
    #define wxRIBBON_TEST_MSO_ART_PROVIDER 0

    #define wxRIBBON_TEST_ART_PROVIDERS wxRIBBON_TEST_STANDARD_ART_PROVIDERS
#endif

// To run the tests with a single art provider only, select them by the end of
// their names, e.g.
//
//      test_gui "*wxRibbonAUIArtProvider"

namespace
{

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------

// IDs of the items of the ribbon created by the test fixture.
enum
{
    ID_BUTTON_NORMAL = wxID_HIGHEST + 1,
    ID_BUTTON_TOGGLE,
    ID_BUTTON_HYBRID,
    ID_BUTTON_DISABLED,
    ID_BUTTON_EXTRA,

    ID_TOOL_NORMAL,
    ID_TOOL_TOGGLE,
    ID_TOOL_HYBRID,
    ID_TOOL_DISABLED,
    ID_TOOL_EXTRA,

    ID_GALLERY_ITEM_FIRST
};

const int GALLERY_ITEM_COUNT = 4;

wxBitmap MakeBitmap(int size)
{
    wxBitmap bmp(size, size);
    wxMemoryDC dc(bmp);
    dc.SetBackground(*wxBLUE_BRUSH);
    dc.Clear();

    return bmp;
}

wxPoint GetCentre(const wxRect& rect)
{
    return rect.GetPosition() + rect.GetSize() / 2;
}

// Force repainting the window immediately.
void Repaint(wxWindow* win)
{
    WaitForPaint waiter(win);

    win->Refresh();
    win->Update();

    waiter.YieldUntilPainted();
}

// The information about the ribbon elements which can't be obtained using the
// public API: it is recorded when the elements are drawn.
struct RibbonDrawn
{
    // The tabs, with their rectangles in wxRibbonBar coordinates.
    std::map<const wxRibbonPage*, wxRibbonPageTabInfo> tabs;

    // In wxRibbonBar coordinates.
    wxRect toggleButton;
    wxRect helpButton;

    // The tab scroll buttons by their direction, in wxRibbonBar coordinates.
    std::map<long, wxRect> tabScrollButtons;

    // The states of the buttons of the button bars by their labels.
    std::map<wxString, long> buttonStates;

    // The rectangles, in wxRibbonToolBar coordinates, and states of the tools.
    std::vector<std::pair<wxRect, long>> tools;

    // In wxRibbonGallery coordinates.
    std::map<const wxRibbonGalleryItem*, wxRect> galleryItems;

    // The key tips shown.
    std::vector<wxString> keyTips;
};

// The art provider behaving as the one it derives from but also recording
// the elements it draws.
template <class Art>
class RecordingArt : public Art
{
public:
    explicit RecordingArt(RibbonDrawn* drawn) : m_drawn(drawn) { }

    wxNODISCARD wxRibbonArtProvider* Clone() const override
    {
        return new RecordingArt(*this);
    }

    void DrawTab(wxDC& dc,
                 wxWindow* wnd,
                 const wxRibbonPageTabInfo& tab) override
    {
        m_drawn->tabs[tab.page] = tab;

        Art::DrawTab(dc, wnd, tab);
    }

    void DrawScrollButton(wxDC& dc,
                          wxWindow* wnd,
                          const wxRect& rect,
                          long style) override
    {
        if ( (style & wxRIBBON_SCROLL_BTN_FOR_MASK) ==
                wxRIBBON_SCROLL_BTN_FOR_TABS )
        {
            m_drawn->tabScrollButtons
                     [style & wxRIBBON_SCROLL_BTN_DIRECTION_MASK] = rect;
        }

        Art::DrawScrollButton(dc, wnd, rect, style);
    }

    void DrawToggleButton(wxDC& dc,
                          wxRibbonBar* wnd,
                          const wxRect& rect,
                          wxRibbonDisplayMode mode) override
    {
        m_drawn->toggleButton = rect;

        Art::DrawToggleButton(dc, wnd, rect, mode);
    }

    void DrawHelpButton(wxDC& dc,
                        wxRibbonBar* wnd,
                        const wxRect& rect) override
    {
        m_drawn->helpButton = rect;

        Art::DrawHelpButton(dc, wnd, rect);
    }

    void DrawButtonBarButton(wxDC& dc,
                             wxWindow* wnd,
                             const wxRect& rect,
                             wxRibbonButtonKind kind,
                             long state,
                             const wxString& label,
                             const wxBitmap& bitmap_large,
                             const wxBitmap& bitmap_small) override
    {
        m_drawn->buttonStates[label] = state;

        Art::DrawButtonBarButton(dc, wnd, rect, kind, state, label,
                                 bitmap_large, bitmap_small);
    }

    void DrawTool(wxDC& dc,
                  wxWindow* wnd,
                  const wxRect& rect,
                  const wxBitmap& bitmap,
                  wxRibbonButtonKind kind,
                  long state) override
    {
        m_drawn->tools.push_back(std::make_pair(rect, state));

        Art::DrawTool(dc, wnd, rect, bitmap, kind, state);
    }

    void DrawGalleryItemBackground(wxDC& dc,
                                   wxRibbonGallery* wnd,
                                   const wxRect& rect,
                                   wxRibbonGalleryItem* item) override
    {
        m_drawn->galleryItems[item] = rect;

        Art::DrawGalleryItemBackground(dc, wnd, rect, item);
    }

    bool DrawKeyTip(wxDC& dc,
                    wxWindow* wnd,
                    const wxRect& rect,
                    const wxString& keytip) override
    {
        m_drawn->keyTips.push_back(keytip);

        return Art::DrawKeyTip(dc, wnd, rect, keytip);
    }

private:
    RibbonDrawn* const m_drawn;
};

// Return the object specific to the event class.
const void* GetEventData(wxCommandEvent& WXUNUSED(event))
{
    return nullptr;
}

const void* GetEventData(wxRibbonBarEvent& event)
{
    return event.GetPage();
}

const void* GetEventData(wxRibbonButtonBarEvent& event)
{
    return event.GetButton();
}

const void* GetEventData(wxRibbonToolBarEvent& event)
{
    return event.GetBar();
}

const void* GetEventData(wxRibbonGalleryEvent& event)
{
    return event.GetGalleryItem();
}

const void* GetEventData(wxRibbonPanelEvent& event)
{
    return event.GetPanel();
}

// Records the events of the given type received by the window.
class EventRecorder
{
public:
    struct Entry
    {
        int id;
        int value;
        const wxObject* object;
        const void* data;
    };

    template <typename EventTag>
    EventRecorder(wxWindow* win, const EventTag& eventType)
        : m_entries(std::make_shared<std::vector<Entry>>())
    {
        // The window may outlive this object, so the handler shares the
        // ownership of the entries with it.
        const std::shared_ptr<std::vector<Entry>> entries = m_entries;
        win->Bind(eventType, [entries](typename EventTag::EventClass& event)
            {
                entries->push_back(Entry{event.GetId(), event.GetInt(),
                                         event.GetEventObject(),
                                         GetEventData(event)});
                event.Skip();
            });
    }

    size_t GetCount() const { return m_entries->size(); }

    // The accessors for the last event, which must exist.

    int GetLastId() const { return m_entries->back().id; }
    int GetLastValue() const { return m_entries->back().value; }
    const wxObject* GetLastObject() const { return m_entries->back().object; }

    // This is the page for wxRibbonBarEvent, the button for
    // wxRibbonButtonBarEvent, the tool bar for wxRibbonToolBarEvent, the item
    // for wxRibbonGalleryEvent and the panel for wxRibbonPanelEvent.
    const void* GetLastData() const { return m_entries->back().data; }

    // Wait until the given number of events is received and return true if
    // exactly this number of them was.
    bool WaitForCount(size_t count) const
    {
        WaitFor("ribbon event", [&]() { return GetCount() >= count; });

        return GetCount() == count;
    }

private:
    const std::shared_ptr<std::vector<Entry>> m_entries;
};

// ----------------------------------------------------------------------------
// test fixtures
// ----------------------------------------------------------------------------

// The fixture creating a ribbon with two pages:
//
//  - "Home" with the "Buttons" panel containing a button bar and the "Tools"
//    panel, which has the extension button and can't be minimised, containing
//    a tool bar.
//  - "Other" with the "Gallery" panel containing a gallery.
//
// There is also a text control below the ribbon.
class RibbonTestCaseBase
{
protected:
    RibbonTestCaseBase()
        : m_frame(wxTheApp->GetTopWindow()),
          m_frameSize(m_frame->GetSize())
    {
    }

    // Must be called from the derived class constructor, takes ownership of
    // the art provider.
    void Init(wxRibbonArtProvider* art)
    {
        // Make the frame big enough for all the panels to be fully shown.
        m_frame->SetClientSize(m_frame->FromDIP(wxSize(800, 400)));

        m_host = new wxPanel(m_frame);

        m_bar = new wxRibbonBar(m_host);
        m_bar->SetArtProvider(art);

        const wxBitmap bmpLarge = MakeBitmap(32);
        const wxBitmap bmpSmall = MakeBitmap(16);

        m_home = new wxRibbonPage(m_bar, wxID_ANY, "Home");

        m_buttonsPanel = new wxRibbonPanel(m_home, wxID_ANY, "Buttons",
                                           bmpLarge);
        m_buttons = new wxRibbonButtonBar(m_buttonsPanel);
        m_buttons->AddButton(ID_BUTTON_NORMAL, "Normal", bmpLarge, wxString());
        m_buttons->AddToggleButton(ID_BUTTON_TOGGLE, "Toggle", bmpLarge);
        m_buttons->AddHybridButton(ID_BUTTON_HYBRID, "Hybrid", bmpLarge);
        m_buttons->AddButton(ID_BUTTON_DISABLED, "Disabled", bmpLarge,
                             wxString());
        m_buttons->EnableButton(ID_BUTTON_DISABLED, false);

        m_toolsPanel = new wxRibbonPanel(m_home, wxID_ANY, "Tools",
                                         bmpLarge,
                                         wxDefaultPosition, wxDefaultSize,
                                         wxRIBBON_PANEL_NO_AUTO_MINIMISE |
                                         wxRIBBON_PANEL_EXT_BUTTON);
        m_tools = new wxRibbonToolBar(m_toolsPanel);
        m_tools->AddTool(ID_TOOL_NORMAL, bmpSmall, wxString());
        m_tools->AddToggleTool(ID_TOOL_TOGGLE, bmpSmall);
        m_tools->AddSeparator();
        m_tools->AddHybridTool(ID_TOOL_HYBRID, bmpSmall);
        m_tools->AddTool(ID_TOOL_DISABLED, bmpSmall, wxString());
        m_tools->EnableTool(ID_TOOL_DISABLED, false);

        m_other = new wxRibbonPage(m_bar, wxID_ANY, "Other");

        m_galleryPanel = new wxRibbonPanel(m_other, wxID_ANY, "Gallery",
                                           bmpLarge);
        m_gallery = new wxRibbonGallery(m_galleryPanel);
        for ( int n = 0; n < GALLERY_ITEM_COUNT; ++n )
            m_gallery->Append(bmpLarge, ID_GALLERY_ITEM_FIRST + n);

        m_bar->Realize();

        m_text = new wxTextCtrl(m_host, wxID_ANY);

        wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(m_bar, wxSizerFlags().Expand());
        sizer->Add(m_text, wxSizerFlags().Expand().Border());
        m_host->SetSizer(sizer);

        SetHostWidth(m_frame->GetClientSize().x);
    }

    ~RibbonTestCaseBase()
    {
        if ( m_bar )
            m_bar->DismissExpandedPanel();

        DeleteTestWindow(m_host);

        m_frame->SetSize(m_frameSize);
    }

    // Change the width of the window containing the ribbon, and so of the
    // ribbon itself.
    void SetHostWidth(int width)
    {
        m_host->SetSize(0, 0, width, m_frame->GetClientSize().y);
        m_host->Layout();
    }

    // Make the ribbon just narrow enough for the "Buttons" panel to become
    // minimised: this happens at different widths for different art
    // providers.
    void MinimiseButtonsPanel()
    {
        const int step = m_frame->FromDIP(10);
        for ( int width = m_host->GetSize().x;
              width > step && !m_buttonsPanel->IsMinimised();
              width -= step )
        {
            SetHostWidth(width);
        }

        REQUIRE( m_buttonsPanel->IsMinimised() );
    }

    // Realize the ribbon again and lay it out after changing its contents.
    void Relayout()
    {
        m_bar->Realize();
        m_host->Layout();
    }

    // Return the information about the tab of the given page, with its
    // rectangle in the ribbon bar coordinates.
    wxRibbonPageTabInfo GetTabInfo(const wxRibbonPage* page)
    {
        m_drawn.tabs.clear();
        Repaint(m_bar);

        const auto it = m_drawn.tabs.find(page);
        REQUIRE( it != m_drawn.tabs.end() );

        return it->second;
    }

    wxRect GetTabRect(const wxRibbonPage* page)
    {
        return GetTabInfo(page).rect;
    }

    // Return the rectangle of the button scrolling the tabs in the given
    // direction, which is empty if it is not shown.
    wxRect GetTabScrollButtonRect(wxRibbonScrollButtonStyle direction)
    {
        m_drawn.tabScrollButtons.clear();
        Repaint(m_bar);

        const auto it = m_drawn.tabScrollButtons.find(direction);

        return it == m_drawn.tabScrollButtons.end() ? wxRect() : it->second;
    }

    // Return true if the button with the given label is shown.
    bool IsButtonDrawn(const wxString& label)
    {
        m_drawn.buttonStates.clear();
        Repaint(m_buttons);

        return m_drawn.buttonStates.count(label) != 0;
    }

    // Return the state, i.e. the combination of wxRibbonButtonBarButtonState
    // values, of the button with the given label.
    long GetDrawnButtonState(const wxString& label)
    {
        REQUIRE( IsButtonDrawn(label) );

        return m_drawn.buttonStates[label];
    }

    // Return the state, i.e. the combination of wxRibbonToolBarToolState
    // values, of the tool with the given ID.
    long GetDrawnToolState(int id)
    {
        m_drawn.tools.clear();
        Repaint(m_tools);

        const wxPoint pt = GetCentre(m_tools->GetToolRect(id));
        for ( const auto& tool : m_drawn.tools )
        {
            if ( tool.first.Contains(pt) )
                return tool.second;
        }

        FAIL("Tool " << id << " was not drawn");

        return 0;
    }

    // Return the sorted key tips currently shown, without duplicates.
    std::vector<wxString> GetKeyTips()
    {
        m_drawn.keyTips.clear();

        wxWindow* const windows[] =
        {
            m_bar, m_home, m_buttonsPanel, m_buttons, m_toolsPanel, m_tools,
            m_other, m_galleryPanel, m_gallery
        };
        for ( wxWindow* win : windows )
            Repaint(win);

        // Repainting a window may repaint its children too, so the same key
        // tip can be drawn more than once.
        std::vector<wxString> keyTips = m_drawn.keyTips;
        std::sort(keyTips.begin(), keyTips.end());
        keyTips.erase(std::unique(keyTips.begin(), keyTips.end()),
                      keyTips.end());

        return keyTips;
    }

    // Get the rectangles of the gallery buttons, in the gallery coordinates.
    void GetGalleryButtonRects(wxRect* up, wxRect* down, wxRect* extension)
    {
        wxInfoDC dc(m_gallery);
        wxPoint origin;
        m_gallery->GetArtProvider()->GetGalleryClientSize
                                     (
                                        dc, m_gallery, m_gallery->GetSize(),
                                        &origin, up, down, extension
                                     );
    }

    wxRect GetToggleButtonRect()
    {
        m_drawn.toggleButton = wxRect();
        Repaint(m_bar);

        return m_drawn.toggleButton;
    }

    wxRect GetHelpButtonRect()
    {
        m_drawn.helpButton = wxRect();
        Repaint(m_bar);

        return m_drawn.helpButton;
    }

    // Return the rectangle of the given gallery item, in the gallery
    // coordinates: the gallery must be shown.
    wxRect GetGalleryItemRect(const wxRibbonGalleryItem* item)
    {
        m_drawn.galleryItems.clear();
        Repaint(m_gallery);

        const auto it = m_drawn.galleryItems.find(item);
        REQUIRE( it != m_drawn.galleryItems.end() );

        return it->second;
    }

    // Return the point in the part of the hybrid item not occupied by its
    // drop down part.
    static wxPoint GetHybridMainPoint(const wxRect& item, const wxRect& dropdown)
    {
        REQUIRE( !dropdown.IsEmpty() );
        REQUIRE( item.Contains(dropdown) );

        wxPoint pt = GetCentre(item);
        if ( dropdown.GetTop() > item.GetTop() )
        {
            // The drop down part is below the main one.
            pt.y = (item.GetTop() + dropdown.GetTop()) / 2;
        }
        else
        {
            // The drop down part is on the right of the main one.
            pt.x = (item.GetLeft() + dropdown.GetLeft()) / 2;
        }

        return pt;
    }

#if wxUSE_UIACTIONSIMULATOR
    // Prepare for using wxUIActionSimulator.
    void PrepareForInput()
    {
        m_frame->Raise();

        Repaint(m_host);
        wxYield();

        // Don't leave the mouse over the ribbon, where the previous test
        // could have left it, as the item under it would be hovered then.
        MoveMouseTo(m_text, wxPoint(5, 5));
        YieldForAWhile();
    }

    // Wait until the next click at the same position can't be taken for a
    // part of a double click any more.
    static void WaitForDoubleClickTime()
    {
        YieldForAWhile(wxSystemSettings::GetMetric(wxSYS_DCLICK_MSEC) + 100);
    }

    // Move the mouse to the given point in the window coordinates.
    void MoveMouseTo(wxWindow* win, const wxPoint& pt)
    {
        wxUIActionSimulator sim;

        // Moving the mouse twice ensures that the motion event is generated
        // even if the mouse already was at this position.
        sim.MouseMove(win->ClientToScreen(pt) + wxPoint(1, 1));
        wxYield();

        sim.MouseMove(win->ClientToScreen(pt));
        wxYield();
    }

    void ClickAt(wxWindow* win, const wxPoint& pt, int button = wxMOUSE_BTN_LEFT)
    {
        MoveMouseTo(win, pt);

        wxUIActionSimulator sim;
        sim.MouseClick(button);
        wxYield();
    }

    void DoubleClickAt(wxWindow* win, const wxPoint& pt)
    {
        MoveMouseTo(win, pt);

        wxUIActionSimulator sim;
        sim.MouseDblClick();
        wxYield();
    }

    void PressKey(int keycode)
    {
        wxUIActionSimulator sim;
        sim.Char(keycode);
        wxYield();
    }
#endif // wxUSE_UIACTIONSIMULATOR

    wxWindow* const m_frame;
    const wxSize m_frameSize;

    RibbonDrawn m_drawn;

    wxPanel* m_host = nullptr;
    wxRibbonBar* m_bar = nullptr;
    wxRibbonPage* m_home = nullptr;
    wxRibbonPanel* m_buttonsPanel = nullptr;
    wxRibbonButtonBar* m_buttons = nullptr;
    wxRibbonPanel* m_toolsPanel = nullptr;
    wxRibbonToolBar* m_tools = nullptr;
    wxRibbonPage* m_other = nullptr;
    wxRibbonPanel* m_galleryPanel = nullptr;
    wxRibbonGallery* m_gallery = nullptr;
    wxTextCtrl* m_text = nullptr;

    wxDECLARE_NO_COPY_CLASS(RibbonTestCaseBase);
};

// The fixture using the given art provider.
template <class Art>
class RibbonTestCase : public RibbonTestCaseBase
{
protected:
    RibbonTestCase()
    {
        Init(new RecordingArt<Art>(&m_drawn));
    }
};

// The fixture for the tests not depending on the art provider used.
typedef RibbonTestCase<wxRibbonDefaultArtProvider> RibbonDefaultTestCase;

} // anonymous namespace

// ----------------------------------------------------------------------------
// tests of the API
// ----------------------------------------------------------------------------

TEST_CASE_METHOD(RibbonDefaultTestCase, "Ribbon::Pages", "[ribbon]")
{
    EventRecorder changing(m_bar, wxEVT_RIBBONBAR_PAGE_CHANGING);
    EventRecorder changed(m_bar, wxEVT_RIBBONBAR_PAGE_CHANGED);

    CHECK( m_bar->GetPageCount() == 2 );
    CHECK( m_bar->GetActivePage() == 0 );

    CHECK( m_bar->GetPage(0) == m_home );
    CHECK( m_bar->GetPage(1) == m_other );
    CHECK( m_bar->GetPage(2) == nullptr );
    CHECK( m_bar->GetPage(-1) == nullptr );

    CHECK( m_bar->GetPageNumber(m_home) == 0 );
    CHECK( m_bar->GetPageNumber(m_other) == 1 );
    CHECK( m_bar->GetPageNumber(nullptr) == wxNOT_FOUND );

    CHECK( m_bar->GetPageById(m_other->GetId()) == m_other );

    CHECK( m_home->GetPanelCount() == 2 );
    CHECK( m_home->GetPanel(0) == m_buttonsPanel );
    CHECK( m_home->GetPanel(1) == m_toolsPanel );
    CHECK( m_home->GetPanelById(m_toolsPanel->GetId()) == m_toolsPanel );

    SECTION("Activate")
    {
        CHECK( m_home->IsShown() );
        CHECK_FALSE( m_other->IsShown() );

        CHECK( m_bar->SetActivePage(1) );
        CHECK( m_bar->GetActivePage() == 1 );
        CHECK_FALSE( m_home->IsShown() );
        CHECK( m_other->IsShown() );

        CHECK( m_bar->SetActivePage(m_home) );
        CHECK( m_bar->GetActivePage() == 0 );

        CHECK_FALSE( m_bar->SetActivePage(2) );
        CHECK( m_bar->GetActivePage() == 0 );

        // Changing the page programmatically doesn't generate any events.
        CHECK( changing.GetCount() == 0 );
        CHECK( changed.GetCount() == 0 );
    }

    SECTION("Show and hide")
    {
        CHECK( m_bar->IsPageShown(1) );

        m_bar->HidePage(1);
        CHECK_FALSE( m_bar->IsPageShown(1) );

        m_bar->ShowPage(1);
        CHECK( m_bar->IsPageShown(1) );

        CHECK_FALSE( m_bar->IsPageShown(2) );
    }

    SECTION("Highlight")
    {
        CHECK_FALSE( m_bar->IsPageHighlighted(1) );

        m_bar->AddPageHighlight(1);
        CHECK( m_bar->IsPageHighlighted(1) );
        CHECK_FALSE( m_bar->IsPageHighlighted(0) );

        m_bar->RemovePageHighlight(1);
        CHECK_FALSE( m_bar->IsPageHighlighted(1) );
    }

    SECTION("Delete active")
    {
        m_bar->DeletePage(0);
        m_bar->Realize();

        CHECK( m_bar->GetPageCount() == 1 );
        CHECK( m_bar->GetPage(0) == m_other );
        CHECK( m_bar->GetActivePage() == 0 );
        CHECK( m_other->IsShown() );
    }

    SECTION("Delete inactive")
    {
        CHECK( m_bar->SetActivePage(1) );

        m_bar->DeletePage(0);
        m_bar->Realize();

        CHECK( m_bar->GetPageCount() == 1 );
        CHECK( m_bar->GetActivePage() == 0 );
        CHECK( m_other->IsShown() );
    }

    SECTION("Clear")
    {
        m_bar->ClearPages();

        CHECK( m_bar->GetPageCount() == 0 );
        CHECK( m_bar->GetActivePage() == wxNOT_FOUND );
    }
}

TEST_CASE_METHOD(RibbonDefaultTestCase, "Ribbon::DisplayMode", "[ribbon]")
{
    CHECK( m_bar->ArePanelsShown() );
    CHECK( m_bar->GetDisplayMode() == wxRIBBON_BAR_PINNED );

    const int heightPinned = m_bar->GetSize().y;

    m_bar->HidePanels();
    CHECK_FALSE( m_bar->ArePanelsShown() );
    CHECK( m_bar->GetDisplayMode() == wxRIBBON_BAR_MINIMIZED );
    CHECK( m_bar->GetSize().y < heightPinned );

    m_bar->ShowPanels(wxRIBBON_BAR_EXPANDED);
    CHECK( m_bar->ArePanelsShown() );
    CHECK( m_bar->GetDisplayMode() == wxRIBBON_BAR_EXPANDED );

    m_bar->ShowPanels();
    CHECK( m_bar->ArePanelsShown() );
    CHECK( m_bar->GetDisplayMode() == wxRIBBON_BAR_PINNED );
    CHECK( m_bar->GetSize().y == heightPinned );
}

TEST_CASE_METHOD(RibbonDefaultTestCase, "Ribbon::ButtonBar", "[ribbon]")
{
    CHECK( m_buttons->GetButtonCount() == 4 );

    wxRibbonButtonBarButtonBase* const normal = m_buttons->GetItem(0);
    REQUIRE( normal );
    CHECK( m_buttons->GetItemById(ID_BUTTON_NORMAL) == normal );
    CHECK( m_buttons->GetItemId(normal) == ID_BUTTON_NORMAL );
    CHECK( m_buttons->GetItemId(m_buttons->GetItem(1)) == ID_BUTTON_TOGGLE );
    CHECK( m_buttons->GetItemById(ID_BUTTON_EXTRA) == nullptr );

    SECTION("Rectangles")
    {
        static const int ids[] =
        {
            ID_BUTTON_NORMAL, ID_BUTTON_TOGGLE,
            ID_BUTTON_HYBRID, ID_BUTTON_DISABLED
        };

        const wxRect rectAll = m_buttons->GetClientRect();
        for ( size_t n = 0; n < WXSIZEOF(ids); ++n )
        {
            INFO("Button " << n);

            const wxRect rect = m_buttons->GetItemRect(ids[n]);
            CHECK_FALSE( rect.IsEmpty() );
            CHECK( rectAll.Contains(rect) );

            for ( size_t m = 0; m < n; ++m )
                CHECK_FALSE( rect.Intersects(m_buttons->GetItemRect(ids[m])) );

            // Only the hybrid button has a separate drop down part.
            const wxRect rectDropdown = m_buttons->GetItemDropdownRect(ids[n]);
            if ( ids[n] == ID_BUTTON_HYBRID )
            {
                CHECK_FALSE( rectDropdown.IsEmpty() );
                CHECK( rect.Contains(rectDropdown) );
                CHECK( rectDropdown != rect );
            }
            else
            {
                CHECK( rectDropdown.IsEmpty() );
            }
        }

        CHECK( m_buttons->GetItemRect(ID_BUTTON_EXTRA).IsEmpty() );
    }

    SECTION("Enable")
    {
        CHECK( m_buttons->GetButtonEnabled(ID_BUTTON_NORMAL) );
        CHECK_FALSE( m_buttons->GetButtonEnabled(ID_BUTTON_DISABLED) );

        m_buttons->EnableButton(ID_BUTTON_DISABLED);
        CHECK( m_buttons->GetButtonEnabled(ID_BUTTON_DISABLED) );

        m_buttons->EnableButton(ID_BUTTON_NORMAL, false);
        CHECK_FALSE( m_buttons->GetButtonEnabled(ID_BUTTON_NORMAL) );
    }

    SECTION("Client data")
    {
        CHECK( m_buttons->GetItemClientData(normal) == nullptr );

        int data = 17;
        m_buttons->SetItemClientData(normal, &data);
        CHECK( m_buttons->GetItemClientData(normal) == &data );
    }

    SECTION("Insert and delete")
    {
        wxRibbonButtonBarButtonBase* const
            extra = m_buttons->InsertButton(1, ID_BUTTON_EXTRA, "Extra",
                                            MakeBitmap(32), wxString());
        REQUIRE( extra );
        m_bar->Realize();

        CHECK( m_buttons->GetButtonCount() == 5 );
        CHECK( m_buttons->GetItem(1) == extra );
        CHECK( m_buttons->GetItemById(ID_BUTTON_EXTRA) == extra );
        CHECK( m_buttons->GetItemId(m_buttons->GetItem(2)) == ID_BUTTON_TOGGLE );
        CHECK_FALSE( m_buttons->GetItemRect(ID_BUTTON_EXTRA).IsEmpty() );

        CHECK( m_buttons->DeleteButton(ID_BUTTON_EXTRA) );
        CHECK( m_buttons->GetButtonCount() == 4 );
        CHECK( m_buttons->GetItemById(ID_BUTTON_EXTRA) == nullptr );

        CHECK_FALSE( m_buttons->DeleteButton(ID_BUTTON_EXTRA) );
        CHECK( m_buttons->GetButtonCount() == 4 );
    }

    SECTION("Clear")
    {
        m_buttons->ClearButtons();

        CHECK( m_buttons->GetButtonCount() == 0 );
        CHECK( m_buttons->GetItemById(ID_BUTTON_NORMAL) == nullptr );
    }
}

TEST_CASE_METHOD(RibbonDefaultTestCase, "Ribbon::ToolBar", "[ribbon]")
{
    // The separator counts as a tool.
    CHECK( m_tools->GetToolCount() == 5 );

    wxRibbonToolBarToolBase* const normal = m_tools->FindById(ID_TOOL_NORMAL);
    REQUIRE( normal );
    CHECK( m_tools->GetToolId(normal) == ID_TOOL_NORMAL );
    CHECK( m_tools->GetToolByPos(0) == normal );
    CHECK( m_tools->GetToolByPos(2) == nullptr );
    CHECK( m_tools->FindById(ID_TOOL_EXTRA) == nullptr );

    CHECK( m_tools->GetToolPos(ID_TOOL_NORMAL) == 0 );
    CHECK( m_tools->GetToolPos(ID_TOOL_TOGGLE) == 1 );
    CHECK( m_tools->GetToolPos(ID_TOOL_HYBRID) == 3 );
    CHECK( m_tools->GetToolPos(ID_TOOL_DISABLED) == 4 );
    CHECK( m_tools->GetToolPos(ID_TOOL_EXTRA) == wxNOT_FOUND );

    CHECK( m_tools->GetToolKind(ID_TOOL_NORMAL) == wxRIBBON_BUTTON_NORMAL );
    CHECK( m_tools->GetToolKind(ID_TOOL_TOGGLE) == wxRIBBON_BUTTON_TOGGLE );
    CHECK( m_tools->GetToolKind(ID_TOOL_HYBRID) == wxRIBBON_BUTTON_HYBRID );

    SECTION("Rectangles")
    {
        static const int ids[] =
        {
            ID_TOOL_NORMAL, ID_TOOL_TOGGLE, ID_TOOL_HYBRID, ID_TOOL_DISABLED
        };

        const wxRect rectAll = m_tools->GetClientRect();
        for ( size_t n = 0; n < WXSIZEOF(ids); ++n )
        {
            INFO("Tool " << n);

            const wxRect rect = m_tools->GetToolRect(ids[n]);
            CHECK_FALSE( rect.IsEmpty() );
            CHECK( rectAll.Contains(rect) );

            for ( size_t m = 0; m < n; ++m )
                CHECK_FALSE( rect.Intersects(m_tools->GetToolRect(ids[m])) );

            const wxPoint pt = GetCentre(rect);
            CHECK( m_tools->GetToolByPos(pt.x, pt.y) == m_tools->FindById(ids[n]) );

            // Only the hybrid tool has a separate drop down part.
            const wxRect rectDropdown = m_tools->GetToolDropdownRect(ids[n]);
            if ( ids[n] == ID_TOOL_HYBRID )
            {
                CHECK_FALSE( rectDropdown.IsEmpty() );
                CHECK( rect.Contains(rectDropdown) );
                CHECK( rectDropdown != rect );
            }
            else
            {
                CHECK( rectDropdown.IsEmpty() );
            }
        }
    }

    SECTION("Enable")
    {
        CHECK( m_tools->GetToolEnabled(ID_TOOL_NORMAL) );
        CHECK_FALSE( m_tools->GetToolEnabled(ID_TOOL_DISABLED) );

        m_tools->EnableTool(ID_TOOL_DISABLED);
        CHECK( m_tools->GetToolEnabled(ID_TOOL_DISABLED) );

        m_tools->EnableTool(ID_TOOL_NORMAL, false);
        CHECK_FALSE( m_tools->GetToolEnabled(ID_TOOL_NORMAL) );
    }

    SECTION("Toggle")
    {
        CHECK_FALSE( m_tools->GetToolState(ID_TOOL_TOGGLE) );

        m_tools->ToggleTool(ID_TOOL_TOGGLE, true);
        CHECK( m_tools->GetToolState(ID_TOOL_TOGGLE) );

        m_tools->ToggleTool(ID_TOOL_TOGGLE, false);
        CHECK_FALSE( m_tools->GetToolState(ID_TOOL_TOGGLE) );
    }

    SECTION("Help string")
    {
        CHECK( m_tools->GetToolHelpString(ID_TOOL_NORMAL) == "" );

        m_tools->SetToolHelpString(ID_TOOL_NORMAL, "Some help");
        CHECK( m_tools->GetToolHelpString(ID_TOOL_NORMAL) == "Some help" );
    }

    SECTION("Insert and delete")
    {
        wxRibbonToolBarToolBase* const
            extra = m_tools->InsertTool(1, ID_TOOL_EXTRA, MakeBitmap(16),
                                        wxString());
        REQUIRE( extra );
        m_bar->Realize();

        CHECK( m_tools->GetToolCount() == 6 );
        CHECK( m_tools->GetToolByPos(1) == extra );
        CHECK( m_tools->GetToolPos(ID_TOOL_EXTRA) == 1 );
        CHECK( m_tools->GetToolPos(ID_TOOL_TOGGLE) == 2 );
        CHECK_FALSE( m_tools->GetToolRect(ID_TOOL_EXTRA).IsEmpty() );

        CHECK( m_tools->DeleteTool(ID_TOOL_EXTRA) );
        CHECK( m_tools->GetToolCount() == 5 );
        CHECK( m_tools->FindById(ID_TOOL_EXTRA) == nullptr );

        CHECK_FALSE( m_tools->DeleteTool(ID_TOOL_EXTRA) );

        // Deleting the separator merges the two groups of tools.
        CHECK( m_tools->DeleteToolByPos(2) );
        CHECK( m_tools->GetToolCount() == 4 );
        CHECK( m_tools->GetToolPos(ID_TOOL_HYBRID) == 2 );

        CHECK( m_tools->DeleteToolByPos(0) );
        CHECK( m_tools->GetToolCount() == 3 );
        CHECK( m_tools->FindById(ID_TOOL_NORMAL) == nullptr );
    }

    SECTION("Clear")
    {
        m_tools->ClearTools();

        CHECK( m_tools->GetToolCount() == 0 );
        CHECK( m_tools->FindById(ID_TOOL_NORMAL) == nullptr );
    }
}

TEST_CASE_METHOD(RibbonDefaultTestCase, "Ribbon::Gallery", "[ribbon]")
{
    CHECK_FALSE( m_gallery->IsEmpty() );
    CHECK( m_gallery->GetCount() == GALLERY_ITEM_COUNT );

    wxRibbonGalleryItem* const first = m_gallery->GetItem(0);
    wxRibbonGalleryItem* const second = m_gallery->GetItem(1);
    REQUIRE( first );
    REQUIRE( second );
    CHECK( first != second );
    CHECK( m_gallery->GetItem(GALLERY_ITEM_COUNT) == nullptr );

    SECTION("Selection")
    {
        CHECK( m_gallery->GetSelection() == nullptr );

        m_gallery->SetSelection(second);
        CHECK( m_gallery->GetSelection() == second );

        m_gallery->SetSelection(nullptr);
        CHECK( m_gallery->GetSelection() == nullptr );
    }

    SECTION("Client data")
    {
        CHECK( m_gallery->GetItemClientData(first) == nullptr );

        int data = 17;
        m_gallery->SetItemClientData(first, &data);
        CHECK( m_gallery->GetItemClientData(first) == &data );
        CHECK( m_gallery->GetItemClientData(second) == nullptr );
    }

    SECTION("Label")
    {
        CHECK( m_gallery->GetItemLabel(first) == "" );

        m_gallery->SetItemLabel(first, "First");
        CHECK( m_gallery->GetItemLabel(first) == "First" );

        // The label must be explicitly passed as wxString, as a literal could
        // be converted to "void*" and so select the overload taking the
        // client data instead (this happens with MSVC in its default mode).
        wxRibbonGalleryItem* const
            labelled = m_gallery->Append(MakeBitmap(32), wxID_ANY,
                                         wxString("Labelled"));
        REQUIRE( labelled );
        CHECK( m_gallery->GetItemLabel(labelled) == "Labelled" );
        CHECK( m_gallery->GetCount() == GALLERY_ITEM_COUNT + 1 );
    }

    SECTION("Clear")
    {
        m_gallery->SetSelection(first);
        m_gallery->Clear();

        CHECK( m_gallery->IsEmpty() );
        CHECK( m_gallery->GetCount() == 0 );
        CHECK( m_gallery->GetSelection() == nullptr );
    }
}

TEST_CASE_METHOD(RibbonDefaultTestCase, "Ribbon::Panel", "[ribbon]")
{
    CHECK_FALSE( m_buttonsPanel->HasExtButton() );
    CHECK( m_toolsPanel->HasExtButton() );

    CHECK( m_buttonsPanel->CanAutoMinimise() );
    CHECK_FALSE( m_toolsPanel->CanAutoMinimise() );

    CHECK_FALSE( m_buttonsPanel->IsMinimised() );
    CHECK_FALSE( m_toolsPanel->IsMinimised() );

    CHECK_FALSE( m_buttonsPanel->IsHovered() );
    CHECK( m_buttonsPanel->GetExpandedPanel() == nullptr );

    SECTION("Extension button")
    {
        Repaint(m_toolsPanel);
        Repaint(m_buttonsPanel);

        const wxRect rect = m_toolsPanel->GetExtButtonRect();
        CHECK_FALSE( rect.IsEmpty() );
        CHECK( m_toolsPanel->GetClientRect().Contains(rect) );

        CHECK( m_buttonsPanel->GetExtButtonRect().IsEmpty() );
    }

    SECTION("Minimise")
    {
        // There is not enough space for the buttons now...
        MinimiseButtonsPanel();

        CHECK_FALSE( m_toolsPanel->IsMinimised() );

        // ... and there is again.
        SetHostWidth(m_frame->GetClientSize().x);

        CHECK_FALSE( m_buttonsPanel->IsMinimised() );
        CHECK_FALSE( m_toolsPanel->IsMinimised() );
    }
}

TEST_CASE_METHOD(RibbonDefaultTestCase, "Ribbon::ArtProvider", "[ribbon]")
{
    wxRibbonControl* const controls[] =
    {
        m_home, m_buttonsPanel, m_buttons, m_toolsPanel, m_tools,
        m_other, m_galleryPanel, m_gallery
    };

    wxRibbonArtProvider* const art = m_bar->GetArtProvider();
    REQUIRE( art );

    for ( size_t n = 0; n < WXSIZEOF(controls); ++n )
    {
        INFO("Control " << n);
        CHECK( controls[n]->GetArtProvider() == art );
        CHECK( controls[n]->GetAncestorRibbonBar() == m_bar );
    }

    // Changing the art provider of the bar changes it for everything in it.
    wxRibbonArtProvider* const artNew = new wxRibbonAUIArtProvider;
    m_bar->SetArtProvider(artNew);

    CHECK( m_bar->GetArtProvider() == artNew );
    for ( size_t n = 0; n < WXSIZEOF(controls); ++n )
    {
        INFO("Control " << n);
        CHECK( controls[n]->GetArtProvider() == artNew );
    }

    // The art provider uses the flags of the bar.
    CHECK( artNew->GetFlags() == m_bar->GetWindowStyleFlag() );

    Relayout();
    Repaint(m_host);
}

TEST_CASE_METHOD(RibbonDefaultTestCase, "Ribbon::Styles", "[ribbon]")
{
    const long style = m_bar->GetWindowStyleFlag();
    CHECK( style == wxRIBBON_BAR_DEFAULT_STYLE );

    CHECK_FALSE( GetToggleButtonRect().IsEmpty() );
    CHECK_FALSE( GetHelpButtonRect().IsEmpty() );

    SECTION("No help button")
    {
        m_bar->SetWindowStyleFlag(style & ~wxRIBBON_BAR_SHOW_HELP_BUTTON);
        Relayout();

        CHECK( GetHelpButtonRect().IsEmpty() );
        CHECK_FALSE( GetToggleButtonRect().IsEmpty() );
    }

    SECTION("No toggle button")
    {
        m_bar->SetWindowStyleFlag(style & ~wxRIBBON_BAR_SHOW_TOGGLE_BUTTON);
        Relayout();

        CHECK( GetToggleButtonRect().IsEmpty() );
        CHECK_FALSE( GetHelpButtonRect().IsEmpty() );
    }

    SECTION("No panel extension buttons")
    {
        CHECK( m_toolsPanel->HasExtButton() );

        m_bar->SetWindowStyleFlag(style & ~wxRIBBON_BAR_SHOW_PANEL_EXT_BUTTONS);
        Relayout();

        CHECK_FALSE( m_toolsPanel->HasExtButton() );
    }

    SECTION("Always show tabs")
    {
        // The tabs are not shown by default if there is only one page.
        m_bar->DeletePage(1);
        Relayout();

        const int y = m_home->GetPosition().y;

        m_bar->SetWindowStyleFlag(style | wxRIBBON_BAR_ALWAYS_SHOW_TABS);
        Relayout();

        CHECK( m_home->GetPosition().y > y );
    }

    SECTION("Vertical")
    {
        CHECK( m_home->GetMajorAxis() == wxHORIZONTAL );
        CHECK( m_toolsPanel->GetPosition().x >=
                    m_buttonsPanel->GetRect().GetRight() );

        m_bar->SetWindowStyleFlag(style | wxRIBBON_BAR_FLOW_VERTICAL);
        Relayout();

        // The ribbon doesn't have enough height for its panels when it is
        // laid out horizontally, so give it all the space.
        m_bar->SetSize(m_host->GetClientSize());

        CHECK( m_home->GetMajorAxis() == wxVERTICAL );
        CHECK( m_toolsPanel->GetPosition().y >=
                    m_buttonsPanel->GetRect().GetBottom() );

        Repaint(m_host);
        Repaint(m_home);
        Repaint(m_buttonsPanel);
        Repaint(m_buttons);
        Repaint(m_toolsPanel);
        Repaint(m_tools);
    }
}

TEST_CASE_METHOD(RibbonDefaultTestCase, "Ribbon::ButtonBarState", "[ribbon]")
{
    SECTION("Initial")
    {
        const long normal = GetDrawnButtonState("Normal");
        CHECK_FALSE( normal & wxRIBBON_BUTTONBAR_BUTTON_DISABLED );
        CHECK_FALSE( normal & wxRIBBON_BUTTONBAR_BUTTON_TOGGLED );
        CHECK_FALSE( normal & wxRIBBON_BUTTONBAR_BUTTON_HOVER_MASK );
        CHECK_FALSE( normal & wxRIBBON_BUTTONBAR_BUTTON_ACTIVE_MASK );

        // There is enough space for the buttons to be shown at full size.
        CHECK( (normal & wxRIBBON_BUTTONBAR_BUTTON_SIZE_MASK) ==
                    wxRIBBON_BUTTONBAR_BUTTON_LARGE );

        CHECK( GetDrawnButtonState("Disabled") & wxRIBBON_BUTTONBAR_BUTTON_DISABLED );
    }

    SECTION("Enable")
    {
        m_buttons->EnableButton(ID_BUTTON_NORMAL, false);
        CHECK( GetDrawnButtonState("Normal") & wxRIBBON_BUTTONBAR_BUTTON_DISABLED );

        m_buttons->EnableButton(ID_BUTTON_NORMAL);
        CHECK_FALSE( GetDrawnButtonState("Normal") &
                        wxRIBBON_BUTTONBAR_BUTTON_DISABLED );
    }

    SECTION("Toggle")
    {
        CHECK_FALSE( GetDrawnButtonState("Toggle") &
                        wxRIBBON_BUTTONBAR_BUTTON_TOGGLED );

        m_buttons->ToggleButton(ID_BUTTON_TOGGLE, true);
        CHECK( GetDrawnButtonState("Toggle") & wxRIBBON_BUTTONBAR_BUTTON_TOGGLED );

        m_buttons->ToggleButton(ID_BUTTON_TOGGLE, false);
        CHECK_FALSE( GetDrawnButtonState("Toggle") &
                        wxRIBBON_BUTTONBAR_BUTTON_TOGGLED );
    }

    SECTION("Text")
    {
        const wxRect rect = m_buttons->GetItemRect(ID_BUTTON_NORMAL);

        m_buttons->SetButtonText(ID_BUTTON_NORMAL, "A much longer label");
        Relayout();

        CHECK( IsButtonDrawn("A much longer label") );
        CHECK_FALSE( IsButtonDrawn("Normal") );
        CHECK( m_buttons->GetItemRect(ID_BUTTON_NORMAL).width > rect.width );
    }

    SECTION("Text minimal width")
    {
        const wxRect rect = m_buttons->GetItemRect(ID_BUTTON_NORMAL);

        m_buttons->SetButtonTextMinWidth(ID_BUTTON_NORMAL,
                                         "A much longer label");
        Relayout();

        // The label doesn't change, but the button is as wide as needed for
        // the given one.
        CHECK( IsButtonDrawn("Normal") );
        CHECK( m_buttons->GetItemRect(ID_BUTTON_NORMAL).width > rect.width );
    }

    SECTION("Size class")
    {
        const wxRect rect = m_buttons->GetItemRect(ID_BUTTON_NORMAL);

        m_buttons->SetButtonMaxSizeClass(ID_BUTTON_NORMAL,
                                         wxRIBBON_BUTTONBAR_BUTTON_MEDIUM);
        Relayout();

        CHECK( (GetDrawnButtonState("Normal") & wxRIBBON_BUTTONBAR_BUTTON_SIZE_MASK)
                    == wxRIBBON_BUTTONBAR_BUTTON_MEDIUM );
        CHECK( m_buttons->GetItemRect(ID_BUTTON_NORMAL).height < rect.height );

        // The other buttons are not affected.
        CHECK( (GetDrawnButtonState("Toggle") & wxRIBBON_BUTTONBAR_BUTTON_SIZE_MASK)
                    == wxRIBBON_BUTTONBAR_BUTTON_LARGE );
    }

    SECTION("Icon")
    {
        m_buttons->SetButtonIcon(ID_BUTTON_NORMAL,
                                 MakeBitmap(32), MakeBitmap(16));
        Relayout();

        CHECK( IsButtonDrawn("Normal") );
    }

    SECTION("Dropdown")
    {
        REQUIRE( m_buttons->AddDropdownButton(ID_BUTTON_EXTRA, "Dropdown",
                                              MakeBitmap(32)) );
        Relayout();

        CHECK( m_buttons->GetButtonCount() == 5 );
        CHECK( IsButtonDrawn("Dropdown") );
        CHECK_FALSE( m_buttons->GetItemRect(ID_BUTTON_EXTRA).IsEmpty() );
    }
}

TEST_CASE_METHOD(RibbonDefaultTestCase, "Ribbon::ToolBarState", "[ribbon]")
{
    SECTION("Initial")
    {
        const long normal = GetDrawnToolState(ID_TOOL_NORMAL);
        CHECK_FALSE( normal & wxRIBBON_TOOLBAR_TOOL_DISABLED );
        CHECK_FALSE( normal & wxRIBBON_TOOLBAR_TOOL_TOGGLED );
        CHECK_FALSE( normal & wxRIBBON_TOOLBAR_TOOL_HOVER_MASK );
        CHECK_FALSE( normal & wxRIBBON_TOOLBAR_TOOL_ACTIVE_MASK );

        CHECK( GetDrawnToolState(ID_TOOL_DISABLED) & wxRIBBON_TOOLBAR_TOOL_DISABLED );

        // Check the positions of the tools in their groups.
        CHECK( normal & wxRIBBON_TOOLBAR_TOOL_FIRST );
        CHECK_FALSE( normal & wxRIBBON_TOOLBAR_TOOL_LAST );

        const long toggle = GetDrawnToolState(ID_TOOL_TOGGLE);
        CHECK_FALSE( toggle & wxRIBBON_TOOLBAR_TOOL_FIRST );
        CHECK( toggle & wxRIBBON_TOOLBAR_TOOL_LAST );
    }

    SECTION("Enable")
    {
        m_tools->EnableTool(ID_TOOL_NORMAL, false);
        CHECK( GetDrawnToolState(ID_TOOL_NORMAL) & wxRIBBON_TOOLBAR_TOOL_DISABLED );

        m_tools->EnableTool(ID_TOOL_NORMAL);
        CHECK_FALSE( GetDrawnToolState(ID_TOOL_NORMAL) &
                        wxRIBBON_TOOLBAR_TOOL_DISABLED );
    }

    SECTION("Toggle")
    {
        m_tools->ToggleTool(ID_TOOL_TOGGLE, true);
        CHECK( GetDrawnToolState(ID_TOOL_TOGGLE) & wxRIBBON_TOOLBAR_TOOL_TOGGLED );

        m_tools->ToggleTool(ID_TOOL_TOGGLE, false);
        CHECK_FALSE( GetDrawnToolState(ID_TOOL_TOGGLE) &
                        wxRIBBON_TOOLBAR_TOOL_TOGGLED );
    }

    SECTION("Client data")
    {
        CHECK( m_tools->GetToolClientData(ID_TOOL_NORMAL) == nullptr );

        wxObject data;
        m_tools->SetToolClientData(ID_TOOL_NORMAL, &data);
        CHECK( m_tools->GetToolClientData(ID_TOOL_NORMAL) == &data );
        CHECK( m_tools->GetToolClientData(ID_TOOL_TOGGLE) == nullptr );
    }

    SECTION("Bitmaps")
    {
        m_tools->SetToolNormalBitmap(ID_TOOL_NORMAL, MakeBitmap(16));
        m_tools->SetToolDisabledBitmap(ID_TOOL_NORMAL, MakeBitmap(16));
        Relayout();

        CHECK_FALSE( GetDrawnToolState(ID_TOOL_NORMAL) &
                        wxRIBBON_TOOLBAR_TOOL_DISABLED );
    }

    SECTION("Separator")
    {
        // Split the first group in two.
        REQUIRE( m_tools->InsertSeparator(1) );
        Relayout();

        CHECK( m_tools->GetToolCount() == 6 );
        CHECK( m_tools->GetToolByPos(1) == nullptr );
        CHECK( m_tools->GetToolPos(ID_TOOL_NORMAL) == 0 );
        CHECK( m_tools->GetToolPos(ID_TOOL_TOGGLE) == 2 );
        CHECK( m_tools->GetToolPos(ID_TOOL_HYBRID) == 4 );

        // Both tools are alone in their groups now.
        const long positions = wxRIBBON_TOOLBAR_TOOL_FIRST |
                               wxRIBBON_TOOLBAR_TOOL_LAST;
        CHECK( (GetDrawnToolState(ID_TOOL_NORMAL) & positions) == positions );
        CHECK( (GetDrawnToolState(ID_TOOL_TOGGLE) & positions) == positions );
    }

    SECTION("Rows")
    {
        // All the tools are in the same row by default.
        CHECK( m_tools->GetToolRect(ID_TOOL_HYBRID).y ==
                    m_tools->GetToolRect(ID_TOOL_NORMAL).y );

        m_tools->SetRows(2);
        Relayout();

        // Each of the two groups is in its own row now.
        CHECK( m_tools->GetToolRect(ID_TOOL_TOGGLE).y ==
                    m_tools->GetToolRect(ID_TOOL_NORMAL).y );
        CHECK( m_tools->GetToolRect(ID_TOOL_HYBRID).y >=
                    m_tools->GetToolRect(ID_TOOL_NORMAL).GetBottom() );
    }

    SECTION("Dropdown")
    {
        REQUIRE( m_tools->AddDropdownTool(ID_TOOL_EXTRA, MakeBitmap(16)) );
        Relayout();

        CHECK( m_tools->GetToolKind(ID_TOOL_EXTRA) == wxRIBBON_BUTTON_DROPDOWN );
        CHECK_FALSE( m_tools->GetToolRect(ID_TOOL_EXTRA).IsEmpty() );
    }
}

TEST_CASE_METHOD(RibbonDefaultTestCase, "Ribbon::UpdateUI", "[ribbon]")
{
    SECTION("Buttons")
    {
        m_buttons->Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event)
            {
                switch ( event.GetId() )
                {
                    case ID_BUTTON_NORMAL:
                        event.Enable(false);
                        break;

                    case ID_BUTTON_TOGGLE:
                        event.Check(true);
                        break;

                    case ID_BUTTON_HYBRID:
                        event.SetText("Updated");
                        break;

                    default:
                        event.Skip();
                }
            });

        // This function is not public in wxRibbonButtonBar itself.
        wxWindow* const win = m_buttons;
        win->UpdateWindowUI();

        CHECK_FALSE( m_buttons->GetButtonEnabled(ID_BUTTON_NORMAL) );
        CHECK( GetDrawnButtonState("Toggle") & wxRIBBON_BUTTONBAR_BUTTON_TOGGLED );
        CHECK( IsButtonDrawn("Updated") );
        CHECK_FALSE( IsButtonDrawn("Hybrid") );

        // The button for which the event was not handled didn't change.
        CHECK_FALSE( m_buttons->GetButtonEnabled(ID_BUTTON_DISABLED) );
    }

    SECTION("Tools")
    {
        m_tools->Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& event)
            {
                switch ( event.GetId() )
                {
                    case ID_TOOL_NORMAL:
                        event.Enable(false);
                        break;

                    case ID_TOOL_TOGGLE:
                        event.Check(true);
                        break;

                    case ID_TOOL_DISABLED:
                        event.Enable(true);
                        break;

                    default:
                        event.Skip();
                }
            });

        // This function is not public in wxRibbonToolBar itself.
        wxWindow* const win = m_tools;
        win->UpdateWindowUI();

        CHECK_FALSE( m_tools->GetToolEnabled(ID_TOOL_NORMAL) );
        CHECK( m_tools->GetToolState(ID_TOOL_TOGGLE) );
        CHECK( m_tools->GetToolEnabled(ID_TOOL_DISABLED) );
        CHECK( m_tools->GetToolEnabled(ID_TOOL_HYBRID) );
    }
}

TEST_CASE_METHOD(RibbonDefaultTestCase, "Ribbon::GalleryScroll", "[ribbon]")
{
    REQUIRE( m_bar->SetActivePage(1) );

    // Add enough items to make scrolling the gallery necessary.
    const wxBitmap bmp = MakeBitmap(32);
    for ( int n = 0; n < 200; ++n )
        m_gallery->Append(bmp, wxID_ANY);

    Relayout();

    wxRibbonGalleryItem* const first = m_gallery->GetItem(0);
    wxRibbonGalleryItem* const
        last = m_gallery->GetItem(m_gallery->GetCount() - 1);

    const wxRect rectGallery = m_gallery->GetClientRect();

    CHECK( m_gallery->GetUpButtonState() == wxRIBBON_GALLERY_BUTTON_DISABLED );
    REQUIRE( m_gallery->GetDownButtonState() == wxRIBBON_GALLERY_BUTTON_NORMAL );
    CHECK( m_gallery->GetExtensionButtonState() ==
                wxRIBBON_GALLERY_BUTTON_NORMAL );

    CHECK( rectGallery.Contains(GetGalleryItemRect(first)) );
    CHECK_FALSE( rectGallery.Intersects(GetGalleryItemRect(last)) );

    // It can't be scrolled up initially.
    CHECK_FALSE( m_gallery->ScrollLines(-1) );

    CHECK( m_gallery->ScrollLines(1) );
    CHECK( m_gallery->GetUpButtonState() == wxRIBBON_GALLERY_BUTTON_NORMAL );
    CHECK( m_gallery->GetDownButtonState() == wxRIBBON_GALLERY_BUTTON_NORMAL );

    CHECK( m_gallery->ScrollLines(-1) );
    CHECK( m_gallery->GetUpButtonState() == wxRIBBON_GALLERY_BUTTON_DISABLED );

    // Scroll to the very end.
    CHECK( m_gallery->ScrollPixels(100000) );
    CHECK( m_gallery->GetUpButtonState() == wxRIBBON_GALLERY_BUTTON_NORMAL );
    CHECK( m_gallery->GetDownButtonState() ==
                wxRIBBON_GALLERY_BUTTON_DISABLED );
    CHECK_FALSE( m_gallery->ScrollLines(1) );

    CHECK( rectGallery.Intersects(GetGalleryItemRect(last)) );
    CHECK_FALSE( rectGallery.Intersects(GetGalleryItemRect(first)) );

    m_gallery->EnsureVisible(first);
    CHECK( rectGallery.Intersects(GetGalleryItemRect(first)) );
    CHECK_FALSE( rectGallery.Intersects(GetGalleryItemRect(last)) );

    m_gallery->EnsureVisible(last);
    CHECK( rectGallery.Intersects(GetGalleryItemRect(last)) );
    CHECK_FALSE( rectGallery.Intersects(GetGalleryItemRect(first)) );
}

// ----------------------------------------------------------------------------
// tests of the art providers
// ----------------------------------------------------------------------------

TEMPLATE_TEST_CASE("Ribbon::ArtSettings", "[ribbon][art]",
                   wxRIBBON_TEST_ART_PROVIDERS)
{
    TestType art;

    SECTION("Metric")
    {
        art.SetMetric(wxRIBBON_ART_TAB_SEPARATION_SIZE, 17);
        CHECK( art.GetMetric(wxRIBBON_ART_TAB_SEPARATION_SIZE) == 17 );

        art.SetMetric(wxRIBBON_ART_PANEL_X_SEPARATION_SIZE, 9);
        CHECK( art.GetMetric(wxRIBBON_ART_PANEL_X_SEPARATION_SIZE) == 9 );
        CHECK( art.GetMetric(wxRIBBON_ART_TAB_SEPARATION_SIZE) == 17 );
    }

    SECTION("Colour")
    {
        const wxColour colour(1, 2, 3);

        art.SetColour(wxRIBBON_ART_BUTTON_BAR_LABEL_COLOUR, colour);
        CHECK( art.GetColour(wxRIBBON_ART_BUTTON_BAR_LABEL_COLOUR) == colour );

        art.SetColour(wxRIBBON_ART_TAB_LABEL_COLOUR, colour);
        CHECK( art.GetColour(wxRIBBON_ART_TAB_LABEL_COLOUR) == colour );

        art.SetColour(wxRIBBON_ART_PANEL_LABEL_COLOUR, colour);
        CHECK( art.GetColour(wxRIBBON_ART_PANEL_LABEL_COLOUR) == colour );
    }

    SECTION("Font")
    {
        wxFont font = art.GetFont(wxRIBBON_ART_TAB_LABEL_FONT);
        REQUIRE( font.IsOk() );

        font.MakeBold().MakeLarger();
        art.SetFont(wxRIBBON_ART_TAB_LABEL_FONT, font);
        CHECK( art.GetFont(wxRIBBON_ART_TAB_LABEL_FONT) == font );
    }

    SECTION("Flags")
    {
        art.SetFlags(wxRIBBON_BAR_FOLDBAR_STYLE);
        CHECK( art.GetFlags() == wxRIBBON_BAR_FOLDBAR_STYLE );

        art.SetFlags(wxRIBBON_BAR_DEFAULT_STYLE);
        CHECK( art.GetFlags() == wxRIBBON_BAR_DEFAULT_STYLE );
    }

    SECTION("Colour scheme")
    {
        const wxColour primary(194, 216, 241);
        const wxColour secondary(255, 223, 114);
        const wxColour tertiary(0, 0, 0);

        art.SetColourScheme(primary, secondary, tertiary);

        wxColour primary2, secondary2, tertiary2;
        art.GetColourScheme(&primary2, &secondary2, &tertiary2);

        CHECK( primary2 == primary );
        CHECK( secondary2 == secondary );
        CHECK( tertiary2 == tertiary );
    }
}

TEMPLATE_TEST_CASE("Ribbon::ArtClone", "[ribbon][art]",
                   wxRIBBON_TEST_ART_PROVIDERS)
{
    TestType art;

    // Change some settings to check that the clone doesn't just use the
    // default ones.
    art.SetFlags(wxRIBBON_BAR_FOLDBAR_STYLE);
    art.SetMetric(wxRIBBON_ART_TAB_SEPARATION_SIZE, 17);
    art.SetColour(wxRIBBON_ART_BUTTON_BAR_LABEL_COLOUR, wxColour(1, 2, 3));
    art.SetFont(wxRIBBON_ART_TAB_LABEL_FONT,
                art.GetFont(wxRIBBON_ART_TAB_LABEL_FONT).MakeBold());

    const std::unique_ptr<wxRibbonArtProvider> clone(art.Clone());
    REQUIRE( clone );

    CHECK( clone->GetFlags() == art.GetFlags() );

    for ( int id = wxRIBBON_ART_TAB_SEPARATION_SIZE;
          id <= wxRIBBON_ART_GALLERY_BITMAP_PADDING_BOTTOM_SIZE;
          ++id )
    {
        INFO("Metric " << id);
        CHECK( clone->GetMetric(id) == art.GetMetric(id) );
    }

    for ( int id = wxRIBBON_ART_PANEL_LABEL_FONT;
          id <= wxRIBBON_ART_TAB_LABEL_FONT;
          ++id )
    {
        INFO("Font " << id);
        CHECK( clone->GetFont(id) == art.GetFont(id) );
    }

    for ( int id = wxRIBBON_ART_BUTTON_BAR_LABEL_COLOUR;
          id <= wxRIBBON_ART_BUTTON_BAR_LABEL_HIGHLIGHT_GRADIENT_TOP_COLOUR;
          ++id )
    {
        INFO("Colour " << id);
        CHECK( clone->GetColour(id) == art.GetColour(id) );
    }
}

#if wxRIBBON_TEST_MSO_ART_PROVIDER

TEST_CASE("Ribbon::MSOArtAppearance", "[ribbon][art]")
{
    typedef wxRibbonMSOArtProvider::Appearance Appearance;

    wxRibbonMSOArtProvider art;

    const wxColour primary(10, 20, 30);
    const wxColour secondary(200, 100, 50);
    const wxColour tertiary(250, 250, 250);

    SECTION("System accent")
    {
        CHECK( art.IsUsingSystemAccentColour() );

        art.UseSystemAccentColour(false);
        CHECK_FALSE( art.IsUsingSystemAccentColour() );

        art.UseSystemAccentColour();
        CHECK( art.IsUsingSystemAccentColour() );
    }

    SECTION("Colour scheme")
    {
        wxColour primary2, secondary2, tertiary2;

        // No colours are set initially.
        art.GetAppearanceColourScheme(Appearance::Dark,
                                      &primary2, &secondary2, &tertiary2);
        CHECK_FALSE( primary2.IsOk() );
        CHECK_FALSE( secondary2.IsOk() );
        CHECK_FALSE( tertiary2.IsOk() );

        art.SetAppearanceColourScheme(Appearance::Dark,
                                      primary, secondary, tertiary);

        art.GetAppearanceColourScheme(Appearance::Dark,
                                      &primary2, &secondary2, &tertiary2);
        CHECK( primary2 == primary );
        CHECK( secondary2 == secondary );
        CHECK( tertiary2 == tertiary );

        // The other appearance is not affected.
        art.GetAppearanceColourScheme(Appearance::Light,
                                      &primary2, &secondary2, &tertiary2);
        CHECK_FALSE( primary2.IsOk() );
        CHECK_FALSE( secondary2.IsOk() );
        CHECK_FALSE( tertiary2.IsOk() );

        // Only some of the colours can be set and any pointer can be null.
        art.SetAppearanceColourScheme(Appearance::Light, primary);

        art.GetAppearanceColourScheme(Appearance::Light,
                                      &primary2, nullptr, &tertiary2);
        CHECK( primary2 == primary );
        CHECK_FALSE( tertiary2.IsOk() );

        art.ResetAppearanceColours(Appearance::Dark);

        art.GetAppearanceColourScheme(Appearance::Dark,
                                      &primary2, &secondary2, &tertiary2);
        CHECK_FALSE( primary2.IsOk() );
        CHECK_FALSE( secondary2.IsOk() );
        CHECK_FALSE( tertiary2.IsOk() );

        art.GetAppearanceColourScheme(Appearance::Light,
                                      &primary2, nullptr, nullptr);
        CHECK( primary2 == primary );
    }

    SECTION("Individual colour")
    {
        const int id = wxRIBBON_ART_BUTTON_BAR_LABEL_COLOUR;

        CHECK_FALSE( art.GetAppearanceColour(Appearance::Light, id).IsOk() );
        CHECK_FALSE( art.GetAppearanceColour(Appearance::Dark, id).IsOk() );

        // Setting the colour for both appearances makes it used whichever of
        // them is the current one.
        art.SetAppearanceColour(Appearance::Light, id, secondary);
        art.SetAppearanceColour(Appearance::Dark, id, secondary);

        CHECK( art.GetAppearanceColour(Appearance::Light, id) == secondary );
        CHECK( art.GetAppearanceColour(Appearance::Dark, id) == secondary );
        CHECK( art.GetColour(id) == secondary );

        // An invalid colour removes the override.
        art.SetAppearanceColour(Appearance::Light, id, wxNullColour);
        CHECK_FALSE( art.GetAppearanceColour(Appearance::Light, id).IsOk() );
        CHECK( art.GetAppearanceColour(Appearance::Dark, id) == secondary );

        art.ResetAppearanceColours(Appearance::Dark);
        CHECK_FALSE( art.GetAppearanceColour(Appearance::Dark, id).IsOk() );
        CHECK( art.GetColour(id) != secondary );
    }

    SECTION("Clone")
    {
        art.UseSystemAccentColour(false);
        art.SetAppearanceColourScheme(Appearance::Dark,
                                      primary, secondary, tertiary);
        art.SetAppearanceColour(Appearance::Light,
                                wxRIBBON_ART_TAB_LABEL_COLOUR, secondary);

        const std::unique_ptr<wxRibbonArtProvider> clone(art.Clone());
        wxRibbonMSOArtProvider* const
            mso = dynamic_cast<wxRibbonMSOArtProvider*>(clone.get());
        REQUIRE( mso );

        CHECK_FALSE( mso->IsUsingSystemAccentColour() );

        wxColour primary2, secondary2, tertiary2;
        mso->GetAppearanceColourScheme(Appearance::Dark,
                                       &primary2, &secondary2, &tertiary2);
        CHECK( primary2 == primary );
        CHECK( secondary2 == secondary );
        CHECK( tertiary2 == tertiary );

        CHECK( mso->GetAppearanceColour(Appearance::Light,
                                        wxRIBBON_ART_TAB_LABEL_COLOUR)
                    == secondary );
    }
}

namespace
{

// Check that the two providers have the same settings.
void CheckSameMSOArt(const wxRibbonMSOArtProvider& art1,
                     const wxRibbonMSOArtProvider& art2)
{
    typedef wxRibbonMSOArtProvider::Appearance Appearance;

    CHECK( art1.GetFlags() == art2.GetFlags() );
    CHECK( art1.IsUsingSystemAccentColour() ==
            art2.IsUsingSystemAccentColour() );

    for ( int id = wxRIBBON_ART_TAB_SEPARATION_SIZE;
          id <= wxRIBBON_ART_GALLERY_BITMAP_PADDING_BOTTOM_SIZE;
          ++id )
    {
        INFO("Metric " << id);
        CHECK( art1.GetMetric(id) == art2.GetMetric(id) );
    }

    for ( int id = wxRIBBON_ART_PANEL_LABEL_FONT;
          id <= wxRIBBON_ART_TAB_LABEL_FONT;
          ++id )
    {
        INFO("Font " << id);
        CHECK( art1.GetFont(id) == art2.GetFont(id) );
    }

    for ( int id = wxRIBBON_ART_BUTTON_BAR_LABEL_COLOUR;
          id <= wxRIBBON_ART_BUTTON_BAR_LABEL_HIGHLIGHT_GRADIENT_TOP_COLOUR;
          ++id )
    {
        INFO("Colour " << id);
        CHECK( art1.GetColour(id) == art2.GetColour(id) );
        CHECK( art1.GetAppearanceColour(Appearance::Light, id) ==
                art2.GetAppearanceColour(Appearance::Light, id) );
        CHECK( art1.GetAppearanceColour(Appearance::Dark, id) ==
                art2.GetAppearanceColour(Appearance::Dark, id) );
    }

    wxColour primary1, secondary1, tertiary1;
    wxColour primary2, secondary2, tertiary2;
    art1.GetColourScheme(&primary1, &secondary1, &tertiary1);
    art2.GetColourScheme(&primary2, &secondary2, &tertiary2);
    CHECK( primary1 == primary2 );
    CHECK( secondary1 == secondary2 );
    CHECK( tertiary1 == tertiary2 );

    const Appearance appearances[] = { Appearance::Light, Appearance::Dark };
    for ( const Appearance appearance : appearances )
    {
        art1.GetAppearanceColourScheme(appearance,
                                       &primary1, &secondary1, &tertiary1);
        art2.GetAppearanceColourScheme(appearance,
                                       &primary2, &secondary2, &tertiary2);
        CHECK( primary1 == primary2 );
        CHECK( secondary1 == secondary2 );
        CHECK( tertiary1 == tertiary2 );
    }
}

// Draw some elements, including the glyphs whose bitmaps are cached, with the
// given provider and return the result.
wxImage DrawWithMSOArt(wxRibbonArtProvider& art, wxRibbonBar* bar)
{
    const wxSize size = bar->FromDIP(wxSize(120, 30));
    const int button = size.y - bar->FromDIP(10);

    wxBitmap bmp(size);
    {
        wxMemoryDC dc(bmp);
        art.DrawTabCtrlBackground(dc, bar, wxRect(size));
        art.DrawTabSeparator(dc, bar, wxRect(10, 0, 10, size.y), 0.5);
        art.DrawToggleButton(dc, bar, wxRect(30, 5, button, button),
                             wxRIBBON_BAR_PINNED);
        art.DrawHelpButton(dc, bar, wxRect(70, 5, button, button));
    }

    return bmp.ConvertToImage();
}

bool AreSameImages(const wxImage& image1, const wxImage& image2)
{
    return image1.GetSize() == image2.GetSize() &&
           memcmp(image1.GetData(), image2.GetData(),
                  3*image1.GetWidth()*image1.GetHeight()) == 0;
}

} // anonymous namespace

typedef RibbonTestCase<wxRibbonMSOArtProvider> RibbonMSOTestCase;

// Check that the provider can be copied after it was used for drawing, i.e.
// with its caches filled, and that the copy is independent of the original.
TEST_CASE_METHOD(RibbonMSOTestCase, "Ribbon::MSOArtCopy", "[ribbon][art]")
{
    typedef wxRibbonMSOArtProvider::Appearance Appearance;

    wxRibbonMSOArtProvider* const
        art = dynamic_cast<wxRibbonMSOArtProvider*>(m_bar->GetArtProvider());
    REQUIRE( art );

    const wxColour colour(1, 2, 3);
    art->UseSystemAccentColour(false);
    art->SetAppearanceColour(Appearance::Light,
                             wxRIBBON_ART_TAB_LABEL_COLOUR, colour);
    art->SetAppearanceColour(Appearance::Dark,
                             wxRIBBON_ART_TAB_LABEL_COLOUR, colour);
    art->SetMetric(wxRIBBON_ART_TAB_SEPARATION_SIZE, 17);
    Relayout();

    Repaint(m_host);
    Repaint(m_buttons);
    Repaint(m_tools);
    const wxImage imageOrig = DrawWithMSOArt(*art, m_bar);

    wxRibbonMSOArtProvider copy(*art);
    CheckSameMSOArt(copy, *art);
    CHECK( AreSameImages(DrawWithMSOArt(copy, m_bar), imageOrig) );

    wxRibbonMSOArtProvider assigned;
    assigned = copy;
    CheckSameMSOArt(assigned, *art);
    CHECK( AreSameImages(DrawWithMSOArt(assigned, m_bar), imageOrig) );

    // Changing the copy, including drawing with it in different colours, must
    // not affect the original.
    const wxColour colourOther(4, 5, 6);
    copy.SetColourScheme(wxColour(32, 32, 32), wxColour(200, 100, 50),
                         wxColour(250, 250, 250));
    copy.SetColour(wxRIBBON_ART_BUTTON_BAR_LABEL_COLOUR, colourOther);
    copy.SetAppearanceColour(Appearance::Light,
                             wxRIBBON_ART_TAB_LABEL_COLOUR, colourOther);
    copy.SetMetric(wxRIBBON_ART_TAB_SEPARATION_SIZE, 3);
    CHECK_FALSE( AreSameImages(DrawWithMSOArt(copy, m_bar), imageOrig) );

    CHECK( art->GetColour(wxRIBBON_ART_BUTTON_BAR_LABEL_COLOUR) != colourOther );
    CHECK( art->GetAppearanceColour(Appearance::Light,
                                    wxRIBBON_ART_TAB_LABEL_COLOUR) == colour );
    CHECK( art->GetMetric(wxRIBBON_ART_TAB_SEPARATION_SIZE) ==
            m_bar->FromDIP(17) );
    CheckSameMSOArt(assigned, *art);
    CHECK( AreSameImages(DrawWithMSOArt(*art, m_bar), imageOrig) );

    // The copy remains usable after the original is destroyed.
    wxRibbonMSOArtProvider* const artNew = new wxRibbonMSOArtProvider(*art);
    m_bar->SetArtProvider(artNew);

    CheckSameMSOArt(*artNew, assigned);
    CHECK( AreSameImages(DrawWithMSOArt(*artNew, m_bar), imageOrig) );

    Relayout();
    Repaint(m_host);
    Repaint(m_buttons);
    Repaint(m_tools);
}

#endif // wxRIBBON_TEST_MSO_ART_PROVIDER

// Check that everything can be drawn and that the elements are where they are
// expected to be.
TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::Paint", "[ribbon][art]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    Repaint(this->m_host);
    Repaint(this->m_home);
    Repaint(this->m_buttonsPanel);
    Repaint(this->m_buttons);
    Repaint(this->m_toolsPanel);
    Repaint(this->m_tools);

    const wxRect rectBar = this->m_bar->GetClientRect();

    const wxRect tabHome = this->GetTabRect(this->m_home);
    const wxRect tabOther = this->GetTabRect(this->m_other);
    CHECK_FALSE( tabHome.IsEmpty() );
    CHECK_FALSE( tabOther.IsEmpty() );
    CHECK( rectBar.Contains(tabHome) );
    CHECK( rectBar.Contains(tabOther) );
    CHECK( tabHome.GetRight() < tabOther.GetLeft() );

    // The tabs are above the page.
    CHECK( tabHome.GetBottom() <= this->m_home->GetPosition().y );

    const wxRect toggle = this->GetToggleButtonRect();
    const wxRect help = this->GetHelpButtonRect();
    CHECK_FALSE( toggle.IsEmpty() );
    CHECK_FALSE( help.IsEmpty() );
    CHECK( rectBar.Contains(toggle) );
    CHECK( rectBar.Contains(help) );
    CHECK_FALSE( toggle.Intersects(help) );
    CHECK_FALSE( toggle.Intersects(tabOther) );
    CHECK_FALSE( help.Intersects(tabOther) );

    REQUIRE( this->m_bar->SetActivePage(1) );
    Repaint(this->m_other);
    Repaint(this->m_galleryPanel);

    const wxRect rectGallery = this->m_gallery->GetClientRect();

    const wxRect first = this->GetGalleryItemRect(this->m_gallery->GetItem(0));
    const wxRect second = this->GetGalleryItemRect(this->m_gallery->GetItem(1));
    CHECK_FALSE( first.IsEmpty() );
    CHECK_FALSE( second.IsEmpty() );
    CHECK( rectGallery.Contains(first) );
    CHECK_FALSE( first.Intersects(second) );

    // Check drawing the minimised ribbon and the minimised panel too.
    REQUIRE( this->m_bar->SetActivePage(static_cast<size_t>(0)) );
    this->MinimiseButtonsPanel();
    Repaint(this->m_host);
    Repaint(this->m_buttonsPanel);

    this->m_bar->HidePanels();
    Repaint(this->m_host);
}

// Check that the sizes computed by the art providers are consistent.
TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::ArtSizes", "[ribbon][art]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    wxRibbonArtProvider* const art = this->m_bar->GetArtProvider();
    REQUIRE( art );

    wxInfoDC dc(this->m_bar);

    SECTION("Tab")
    {
        int ideal = -1,
            smallBegin = -1,
            smallMust = -1,
            minimum = -1;
        art->GetBarTabWidth(dc, this->m_bar, "Label", wxNullBitmap,
                            &ideal, &smallBegin, &smallMust, &minimum);

        CHECK( minimum > 0 );
        CHECK( smallMust >= minimum );
        CHECK( smallBegin >= smallMust );
        CHECK( ideal >= smallBegin );

        int idealLong = -1;
        art->GetBarTabWidth(dc, this->m_bar, "Much longer label", wxNullBitmap,
                            &idealLong, nullptr, nullptr, nullptr);

        CHECK( idealLong > ideal );
    }

    SECTION("Panel")
    {
        const wxSize client(100, 60);

        wxPoint offset(-1, -1);
        const wxSize
            size = art->GetPanelSize(dc, this->m_buttonsPanel, client, &offset);

        CHECK( size.x > client.x );
        CHECK( size.y > client.y );
        CHECK( offset.x >= 0 );
        CHECK( offset.y >= 0 );

        // The client area fits into the panel.
        CHECK( offset.x + client.x <= size.x );
        CHECK( offset.y + client.y <= size.y );

        // Getting the client size is the inverse operation.
        wxPoint offset2(-1, -1);
        CHECK( art->GetPanelClientSize(dc, this->m_buttonsPanel, size, &offset2)
                    == client );
        CHECK( offset2 == offset );
    }

    SECTION("Gallery")
    {
        const wxSize client(100, 60);

        const wxSize size = art->GetGallerySize(dc, this->m_gallery, client);

        CHECK( size.x > client.x );
        CHECK( size.y >= client.y );

        wxPoint offset(-1, -1);
        wxRect up, down, extension;
        CHECK( art->GetGalleryClientSize(dc, this->m_gallery, size,
                                         &offset, &up, &down, &extension)
                    == client );

        // The buttons are inside the gallery, but outside of its client area
        // and don't overlap.
        const wxRect rectAll(size);
        const wxRect rectClient(offset, client);

        CHECK( rectAll.Contains(rectClient) );

        const wxRect* const buttons[] = { &up, &down, &extension };
        for ( size_t n = 0; n < WXSIZEOF(buttons); ++n )
        {
            INFO("Button " << n);

            CHECK_FALSE( buttons[n]->IsEmpty() );
            CHECK( rectAll.Contains(*buttons[n]) );
            CHECK_FALSE( rectClient.Intersects(*buttons[n]) );

            for ( size_t m = 0; m < n; ++m )
                CHECK_FALSE( buttons[n]->Intersects(*buttons[m]) );
        }
    }

    SECTION("Button")
    {
        const wxSize bmpLarge(32, 32);
        const wxSize bmpSmall(16, 16);

        static const wxRibbonButtonBarButtonState sizes[] =
        {
            wxRIBBON_BUTTONBAR_BUTTON_SMALL,
            wxRIBBON_BUTTONBAR_BUTTON_MEDIUM,
            wxRIBBON_BUTTONBAR_BUTTON_LARGE
        };

        wxSize sizesNormal[WXSIZEOF(sizes)];

        for ( size_t n = 0; n < WXSIZEOF(sizes); ++n )
        {
            INFO("Size " << n);

            wxRect normal, dropdown;
            REQUIRE( art->GetButtonBarButtonSize(dc, this->m_buttons,
                                                 wxRIBBON_BUTTON_NORMAL,
                                                 sizes[n], "Label", 0,
                                                 bmpLarge, bmpSmall,
                                                 &sizesNormal[n],
                                                 &normal, &dropdown) );

            const wxRect rectAll(sizesNormal[n]);

            CHECK_FALSE( rectAll.IsEmpty() );
            CHECK( normal == rectAll );
            CHECK( dropdown.IsEmpty() );

            // The hybrid button has both parts, which don't overlap.
            wxSize sizeHybrid;
            REQUIRE( art->GetButtonBarButtonSize(dc, this->m_buttons,
                                                 wxRIBBON_BUTTON_HYBRID,
                                                 sizes[n], "Label", 0,
                                                 bmpLarge, bmpSmall,
                                                 &sizeHybrid,
                                                 &normal, &dropdown) );

            const wxRect rectHybrid(sizeHybrid);

            CHECK_FALSE( normal.IsEmpty() );
            CHECK_FALSE( dropdown.IsEmpty() );
            CHECK( rectHybrid.Contains(normal) );
            CHECK( rectHybrid.Contains(dropdown) );
            CHECK_FALSE( normal.Intersects(dropdown) );

            // The dropdown button only has the dropdown part.
            wxSize sizeDropdown;
            wxRect normalNone, dropdownOnly;
            REQUIRE( art->GetButtonBarButtonSize(dc, this->m_buttons,
                                                 wxRIBBON_BUTTON_DROPDOWN,
                                                 sizes[n], "Label", 0,
                                                 bmpLarge, bmpSmall,
                                                 &sizeDropdown,
                                                 &normalNone, &dropdownOnly) );

            CHECK( normalNone.IsEmpty() );
            CHECK( dropdownOnly == wxRect(sizeDropdown) );
        }

        // The small button doesn't show the label, unlike the medium one,
        // and the large one shows it below the bitmap.
        CHECK( sizesNormal[0].x < sizesNormal[1].x );
        CHECK( sizesNormal[1].y < sizesNormal[2].y );

        // The minimal text width is taken into account.
        wxSize sizeWide;
        wxRect normal, dropdown;
        REQUIRE( art->GetButtonBarButtonSize(dc, this->m_buttons,
                                             wxRIBBON_BUTTON_NORMAL,
                                             wxRIBBON_BUTTONBAR_BUTTON_LARGE,
                                             "Label", 300,
                                             bmpLarge, bmpSmall,
                                             &sizeWide, &normal, &dropdown) );
        CHECK( sizeWide.x >= 300 );

        CHECK( art->GetButtonBarButtonTextWidth(dc, "Much longer label",
                                                wxRIBBON_BUTTON_NORMAL,
                                                wxRIBBON_BUTTONBAR_BUTTON_MEDIUM)
                > art->GetButtonBarButtonTextWidth(dc, "Label",
                                                wxRIBBON_BUTTON_NORMAL,
                                                wxRIBBON_BUTTONBAR_BUTTON_MEDIUM) );
    }

    SECTION("Tool")
    {
        const wxSize bmp(16, 16);

        wxRect dropdown;
        const wxSize
            normal = art->GetToolSize(dc, this->m_tools, bmp,
                                      wxRIBBON_BUTTON_NORMAL,
                                      false, false, &dropdown);

        CHECK( normal.x > bmp.x );
        CHECK( normal.y > bmp.y );
        CHECK( dropdown.IsEmpty() );

        const wxSize
            hybrid = art->GetToolSize(dc, this->m_tools, bmp,
                                      wxRIBBON_BUTTON_HYBRID,
                                      false, false, &dropdown);

        CHECK( hybrid.x > normal.x );
        CHECK( hybrid.y == normal.y );
        CHECK_FALSE( dropdown.IsEmpty() );
        CHECK( wxRect(hybrid).Contains(dropdown) );
        CHECK( dropdown != wxRect(hybrid) );

        const wxSize
            dropdownOnly = art->GetToolSize(dc, this->m_tools, bmp,
                                            wxRIBBON_BUTTON_DROPDOWN,
                                            false, false, &dropdown);

        CHECK( dropdownOnly.x > normal.x );
        CHECK( dropdown == wxRect(dropdownOnly) );
    }

    SECTION("Bar buttons")
    {
        const wxRect rectBar(0, 0, 800, 100);

        const wxRect toggle = art->GetBarToggleButtonArea(rectBar);
        const wxRect help = art->GetRibbonHelpButtonArea(rectBar);

        CHECK_FALSE( toggle.IsEmpty() );
        CHECK_FALSE( help.IsEmpty() );
        CHECK( rectBar.Contains(toggle) );
        CHECK( rectBar.Contains(help) );
        CHECK_FALSE( toggle.Intersects(help) );
    }

    SECTION("Other")
    {
        static const long styles[] =
        {
            wxRIBBON_SCROLL_BTN_LEFT | wxRIBBON_SCROLL_BTN_FOR_TABS,
            wxRIBBON_SCROLL_BTN_RIGHT | wxRIBBON_SCROLL_BTN_FOR_TABS,
            wxRIBBON_SCROLL_BTN_LEFT | wxRIBBON_SCROLL_BTN_FOR_PAGE,
            wxRIBBON_SCROLL_BTN_RIGHT | wxRIBBON_SCROLL_BTN_FOR_PAGE,
            wxRIBBON_SCROLL_BTN_UP | wxRIBBON_SCROLL_BTN_FOR_PAGE,
            wxRIBBON_SCROLL_BTN_DOWN | wxRIBBON_SCROLL_BTN_FOR_PAGE
        };

        for ( size_t n = 0; n < WXSIZEOF(styles); ++n )
        {
            INFO("Scroll button " << n);

            const wxSize
                size = art->GetScrollButtonMinimumSize(dc, this->m_bar,
                                                       styles[n]);
            CHECK( size.x > 0 );
            CHECK( size.y > 0 );
        }

        wxSize sizeBitmap;
        wxDirection direction = wxALL;
        const wxSize
            size = art->GetMinimisedPanelMinimumSize(dc, this->m_buttonsPanel,
                                                     &sizeBitmap, &direction);
        CHECK( size.x > sizeBitmap.x );
        CHECK( size.y > sizeBitmap.y );
        CHECK( sizeBitmap.x > 0 );
        CHECK( sizeBitmap.y > 0 );
        CHECK( direction != wxALL );

        const wxRect rectPanel(this->m_toolsPanel->GetSize());
        const wxRect
            rectExt = art->GetPanelExtButtonArea(dc, this->m_toolsPanel,
                                                 rectPanel);
        CHECK_FALSE( rectExt.IsEmpty() );
        CHECK( rectPanel.Contains(rectExt) );
    }
}

// ----------------------------------------------------------------------------
// tests using the simulated user input
// ----------------------------------------------------------------------------

#if wxUSE_UIACTIONSIMULATOR

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::TabClick", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    wxRibbonBar* const bar = this->m_bar;

    EventRecorder changing(bar, wxEVT_RIBBONBAR_PAGE_CHANGING);
    EventRecorder changed(bar, wxEVT_RIBBONBAR_PAGE_CHANGED);

    SECTION("Change")
    {
        this->ClickAt(bar, GetCentre(this->GetTabRect(this->m_other)));

        REQUIRE( changed.WaitForCount(1) );
        CHECK( changed.GetLastData() == this->m_other );
        CHECK( changed.GetLastObject() == bar );
        REQUIRE( changing.GetCount() == 1 );
        CHECK( changing.GetLastData() == this->m_other );
        CHECK( changing.GetLastObject() == bar );
        CHECK( bar->GetActivePage() == 1 );
        CHECK( this->m_other->IsShown() );
        CHECK_FALSE( this->m_home->IsShown() );

        // Clicking the tab of the active page doesn't do anything.
        this->ClickAt(bar, GetCentre(this->GetTabRect(this->m_other)));
        YieldForAWhile();

        CHECK( changing.GetCount() == 1 );
        CHECK( changed.GetCount() == 1 );
        CHECK( bar->GetActivePage() == 1 );

        this->ClickAt(bar, GetCentre(this->GetTabRect(this->m_home)));

        REQUIRE( changed.WaitForCount(2) );
        CHECK( changed.GetLastData() == this->m_home );
        REQUIRE( changing.GetCount() == 2 );
        CHECK( changing.GetLastData() == this->m_home );
        CHECK( bar->GetActivePage() == 0 );
    }

    SECTION("Veto")
    {
        bar->Bind(wxEVT_RIBBONBAR_PAGE_CHANGING,
                  [](wxRibbonBarEvent& event) { event.Veto(); });

        this->ClickAt(bar, GetCentre(this->GetTabRect(this->m_other)));
        YieldForAWhile();

        CHECK( changed.GetCount() == 0 );
        CHECK( bar->GetActivePage() == 0 );
        CHECK( this->m_home->IsShown() );
    }
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::TabDClick", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    wxRibbonBar* const bar = this->m_bar;

    // Double clicking the tab of the active page minimises the ribbon...
    this->DoubleClickAt(bar, GetCentre(this->GetTabRect(this->m_home)));

    CHECK( WaitFor("ribbon to be minimised",
                   [&]() { return !bar->ArePanelsShown(); }) );
    CHECK( bar->GetDisplayMode() == wxRIBBON_BAR_MINIMIZED );
    CHECK( bar->GetActivePage() == 0 );

    this->WaitForDoubleClickTime();

    // ... and doing it again restores it.
    this->DoubleClickAt(bar, GetCentre(this->GetTabRect(this->m_home)));

    CHECK( WaitFor("ribbon to be pinned",
                   [&]() { return bar->GetDisplayMode() == wxRIBBON_BAR_PINNED; }) );
    CHECK( bar->ArePanelsShown() );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::TabOtherButtons", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    wxRibbonBar* const bar = this->m_bar;

    EventRecorder changing(bar, wxEVT_RIBBONBAR_PAGE_CHANGING);
    EventRecorder rightDown(bar, wxEVT_RIBBONBAR_TAB_RIGHT_DOWN);
    EventRecorder rightUp(bar, wxEVT_RIBBONBAR_TAB_RIGHT_UP);
    EventRecorder middleDown(bar, wxEVT_RIBBONBAR_TAB_MIDDLE_DOWN);
    EventRecorder middleUp(bar, wxEVT_RIBBONBAR_TAB_MIDDLE_UP);

    const wxPoint pt = GetCentre(this->GetTabRect(this->m_other));

    this->ClickAt(bar, pt, wxMOUSE_BTN_RIGHT);

    REQUIRE( rightUp.WaitForCount(1) );
    CHECK( rightUp.GetLastData() == this->m_other );
    CHECK( rightUp.GetLastObject() == bar );
    REQUIRE( rightDown.GetCount() == 1 );
    CHECK( rightDown.GetLastData() == this->m_other );
    CHECK( middleDown.GetCount() == 0 );
    CHECK( middleUp.GetCount() == 0 );

    this->ClickAt(bar, pt, wxMOUSE_BTN_MIDDLE);

    REQUIRE( middleUp.WaitForCount(1) );
    CHECK( middleUp.GetLastData() == this->m_other );
    REQUIRE( middleDown.GetCount() == 1 );
    CHECK( middleDown.GetLastData() == this->m_other );
    CHECK( rightDown.GetCount() == 1 );
    CHECK( rightUp.GetCount() == 1 );

    // Only the left button changes the page.
    CHECK( changing.GetCount() == 0 );
    CHECK( bar->GetActivePage() == 0 );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::BarButtons", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    wxRibbonBar* const bar = this->m_bar;

    EventRecorder toggled(bar, wxEVT_RIBBONBAR_TOGGLED);
    EventRecorder help(bar, wxEVT_RIBBONBAR_HELP_CLICK);

    this->ClickAt(bar, GetCentre(this->GetToggleButtonRect()));

    REQUIRE( toggled.WaitForCount(1) );
    CHECK( toggled.GetLastObject() == bar );
    CHECK( toggled.GetLastId() == bar->GetId() );
    CHECK_FALSE( bar->ArePanelsShown() );
    CHECK( bar->GetDisplayMode() == wxRIBBON_BAR_MINIMIZED );

    // The second click of a double click is not handled by this button.
    this->WaitForDoubleClickTime();

    this->ClickAt(bar, GetCentre(this->GetToggleButtonRect()));

    CHECK( toggled.WaitForCount(2) );
    CHECK( bar->ArePanelsShown() );
    CHECK( bar->GetDisplayMode() == wxRIBBON_BAR_PINNED );

    CHECK( help.GetCount() == 0 );

    this->ClickAt(bar, GetCentre(this->GetHelpButtonRect()));

    REQUIRE( help.WaitForCount(1) );
    CHECK( help.GetLastObject() == bar );
    CHECK( help.GetLastId() == bar->GetId() );
    CHECK( toggled.GetCount() == 2 );
    CHECK( bar->ArePanelsShown() );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::ButtonClick", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    wxRibbonButtonBar* const buttons = this->m_buttons;

    EventRecorder clicked(buttons, wxEVT_RIBBONBUTTONBAR_CLICKED);
    EventRecorder dropdown(buttons, wxEVT_RIBBONBUTTONBAR_DROPDOWN_CLICKED);

    SECTION("Normal")
    {
        this->ClickAt(buttons, GetCentre(buttons->GetItemRect(ID_BUTTON_NORMAL)));

        REQUIRE( clicked.WaitForCount(1) );
        CHECK( clicked.GetLastId() == ID_BUTTON_NORMAL );
        CHECK( clicked.GetLastObject() == buttons );
        CHECK( clicked.GetLastData() == buttons->GetItemById(ID_BUTTON_NORMAL) );
        CHECK( dropdown.GetCount() == 0 );
        CHECK( buttons->GetActiveItem() == nullptr );
    }

    SECTION("Hover")
    {
        CHECK( buttons->GetHoveredItem() == nullptr );

        this->MoveMouseTo(buttons,
                          GetCentre(buttons->GetItemRect(ID_BUTTON_TOGGLE)));

        CHECK( WaitFor("button to be hovered", [&]()
            {
                return buttons->GetHoveredItem() ==
                        buttons->GetItemById(ID_BUTTON_TOGGLE);
            }) );
        CHECK( this->m_buttonsPanel->IsHovered() );

        this->MoveMouseTo(this->m_text, wxPoint(5, 5));

        CHECK( WaitFor("button not to be hovered",
                       [&]() { return buttons->GetHoveredItem() == nullptr; }) );
        CHECK_FALSE( this->m_buttonsPanel->IsHovered() );

        CHECK( clicked.GetCount() == 0 );
    }

    SECTION("Disabled")
    {
        this->ClickAt(buttons, GetCentre(buttons->GetItemRect(ID_BUTTON_DISABLED)));
        YieldForAWhile();

        CHECK( clicked.GetCount() == 0 );
        CHECK( dropdown.GetCount() == 0 );
    }

    SECTION("Toggle")
    {
        const wxPoint pt = GetCentre(buttons->GetItemRect(ID_BUTTON_TOGGLE));

        this->ClickAt(buttons, pt);

        REQUIRE( clicked.WaitForCount(1) );
        CHECK( clicked.GetLastId() == ID_BUTTON_TOGGLE );
        CHECK( clicked.GetLastValue() != 0 );

        this->ClickAt(buttons, pt);

        REQUIRE( clicked.WaitForCount(2) );
        CHECK( clicked.GetLastId() == ID_BUTTON_TOGGLE );
        CHECK( clicked.GetLastValue() == 0 );
    }

    SECTION("Hybrid")
    {
        const wxRect rect = buttons->GetItemRect(ID_BUTTON_HYBRID);
        const wxRect rectDropdown = buttons->GetItemDropdownRect(ID_BUTTON_HYBRID);

        this->ClickAt(buttons, this->GetHybridMainPoint(rect, rectDropdown));

        REQUIRE( clicked.WaitForCount(1) );
        CHECK( clicked.GetLastId() == ID_BUTTON_HYBRID );
        CHECK( dropdown.GetCount() == 0 );

        this->ClickAt(buttons, GetCentre(rectDropdown));

        REQUIRE( dropdown.WaitForCount(1) );
        CHECK( dropdown.GetLastId() == ID_BUTTON_HYBRID );
        CHECK( dropdown.GetLastObject() == buttons );
        CHECK( dropdown.GetLastData() == buttons->GetItemById(ID_BUTTON_HYBRID) );
        CHECK( clicked.GetCount() == 1 );
    }
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::ToolClick", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    wxRibbonToolBar* const tools = this->m_tools;

    EventRecorder clicked(tools, wxEVT_RIBBONTOOLBAR_CLICKED);
    EventRecorder dropdown(tools, wxEVT_RIBBONTOOLBAR_DROPDOWN_CLICKED);

    SECTION("Normal")
    {
        this->ClickAt(tools, GetCentre(tools->GetToolRect(ID_TOOL_NORMAL)));

        REQUIRE( clicked.WaitForCount(1) );
        CHECK( clicked.GetLastId() == ID_TOOL_NORMAL );
        CHECK( clicked.GetLastObject() == tools );
        CHECK( clicked.GetLastData() == tools );
        CHECK( dropdown.GetCount() == 0 );
        CHECK( tools->GetActiveTool() == nullptr );
    }

    SECTION("Disabled")
    {
        this->ClickAt(tools, GetCentre(tools->GetToolRect(ID_TOOL_DISABLED)));
        YieldForAWhile();

        CHECK( clicked.GetCount() == 0 );
        CHECK( dropdown.GetCount() == 0 );
    }

    SECTION("Toggle")
    {
        const wxPoint pt = GetCentre(tools->GetToolRect(ID_TOOL_TOGGLE));

        this->ClickAt(tools, pt);

        REQUIRE( clicked.WaitForCount(1) );
        CHECK( clicked.GetLastId() == ID_TOOL_TOGGLE );
        CHECK( clicked.GetLastValue() != 0 );
        CHECK( tools->GetToolState(ID_TOOL_TOGGLE) );

        this->ClickAt(tools, pt);

        REQUIRE( clicked.WaitForCount(2) );
        CHECK( clicked.GetLastValue() == 0 );
        CHECK_FALSE( tools->GetToolState(ID_TOOL_TOGGLE) );
    }

    SECTION("Hybrid")
    {
        const wxRect rect = tools->GetToolRect(ID_TOOL_HYBRID);
        const wxRect rectDropdown = tools->GetToolDropdownRect(ID_TOOL_HYBRID);

        this->ClickAt(tools, this->GetHybridMainPoint(rect, rectDropdown));

        REQUIRE( clicked.WaitForCount(1) );
        CHECK( clicked.GetLastId() == ID_TOOL_HYBRID );
        CHECK( dropdown.GetCount() == 0 );

        this->ClickAt(tools, GetCentre(rectDropdown));

        REQUIRE( dropdown.WaitForCount(1) );
        CHECK( dropdown.GetLastId() == ID_TOOL_HYBRID );
        CHECK( dropdown.GetLastData() == tools );
        CHECK( clicked.GetCount() == 1 );
    }
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::GalleryClick", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    REQUIRE( this->m_bar->SetActivePage(1) );

    this->PrepareForInput();

    wxRibbonGallery* const gallery = this->m_gallery;

    EventRecorder hoverChanged(gallery, wxEVT_RIBBONGALLERY_HOVER_CHANGED);
    EventRecorder selected(gallery, wxEVT_RIBBONGALLERY_SELECTED);
    EventRecorder clicked(gallery, wxEVT_RIBBONGALLERY_CLICKED);

    wxRibbonGalleryItem* const first = gallery->GetItem(0);
    wxRibbonGalleryItem* const second = gallery->GetItem(1);

    const wxPoint ptFirst = GetCentre(this->GetGalleryItemRect(first));
    const wxPoint ptSecond = GetCentre(this->GetGalleryItemRect(second));

    CHECK( gallery->GetHoveredItem() == nullptr );

    this->MoveMouseTo(gallery, ptFirst);

    CHECK( WaitFor("gallery item to be hovered",
                   [&]() { return gallery->GetHoveredItem() == first; }) );
    CHECK( gallery->IsHovered() );
    REQUIRE( hoverChanged.GetCount() == 1 );
    CHECK( hoverChanged.GetLastData() == first );
    CHECK( hoverChanged.GetLastObject() == gallery );
    CHECK( hoverChanged.GetLastId() == gallery->GetId() );
    CHECK( selected.GetCount() == 0 );

    this->ClickAt(gallery, ptFirst);

    REQUIRE( clicked.WaitForCount(1) );
    CHECK( clicked.GetLastData() == first );
    CHECK( clicked.GetLastObject() == gallery );
    REQUIRE( selected.GetCount() == 1 );
    CHECK( selected.GetLastData() == first );
    CHECK( gallery->GetSelection() == first );

    // Clicking the selected item again doesn't select it again.
    this->ClickAt(gallery, ptFirst);

    CHECK( clicked.WaitForCount(2) );
    CHECK( selected.GetCount() == 1 );

    this->ClickAt(gallery, ptSecond);

    REQUIRE( clicked.WaitForCount(3) );
    CHECK( clicked.GetLastData() == second );
    REQUIRE( selected.GetCount() == 2 );
    CHECK( selected.GetLastData() == second );
    CHECK( gallery->GetSelection() == second );
    CHECK( gallery->GetHoveredItem() == second );
    REQUIRE( hoverChanged.GetCount() == 2 );
    CHECK( hoverChanged.GetLastData() == second );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::PanelExtButton", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    wxRibbonPanel* const panel = this->m_toolsPanel;

    EventRecorder activated(panel, wxEVT_RIBBONPANEL_EXTBUTTON_ACTIVATED);

    Repaint(panel);
    const wxRect rect = panel->GetExtButtonRect();
    REQUIRE_FALSE( rect.IsEmpty() );

    this->MoveMouseTo(panel, GetCentre(rect));

    CHECK( WaitFor("extension button to be hovered",
                   [&]() { return panel->IsExtButtonHovered(); }) );
    CHECK( activated.GetCount() == 0 );

    this->ClickAt(panel, GetCentre(rect));

    REQUIRE( activated.WaitForCount(1) );
    CHECK( activated.GetLastId() == panel->GetId() );
    CHECK( activated.GetLastObject() == panel );
    CHECK( activated.GetLastData() == panel );

    // Clicking elsewhere in the panel doesn't activate the button.
    this->ClickAt(panel, wxPoint(rect.GetLeft() / 2, GetCentre(rect).y));
    YieldForAWhile();

    CHECK_FALSE( panel->IsExtButtonHovered() );
    CHECK( activated.GetCount() == 1 );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::MinimisedPanel", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->MinimiseButtonsPanel();

    this->PrepareForInput();

    wxRibbonPanel* const panel = this->m_buttonsPanel;
    CHECK( panel->GetExpandedPanel() == nullptr );

    // Clicking the minimised panel shows its contents in a popup...
    this->ClickAt(panel, GetCentre(panel->GetClientRect()));

    REQUIRE( WaitFor("panel to be expanded",
                     [&]() { return panel->GetExpandedPanel() != nullptr; }) );

    wxRibbonPanel* const expanded = panel->GetExpandedPanel();
    CHECK( expanded->IsShownOnScreen() );
    CHECK( expanded->GetExpandedDummy() == panel );
    CHECK( this->m_buttons->GetParent() == expanded );

    // ... in which the buttons can be used ...
    EventRecorder clicked(this->m_buttons, wxEVT_RIBBONBUTTONBAR_CLICKED);

    this->ClickAt(this->m_buttons,
                  GetCentre(this->m_buttons->GetItemRect(ID_BUTTON_NORMAL)));

    REQUIRE( clicked.WaitForCount(1) );
    CHECK( clicked.GetLastId() == ID_BUTTON_NORMAL );

    // ... and which is closed when it loses focus: click the tab of the
    // active page for this, as the popup can't cover it and this doesn't do
    // anything else.
    this->ClickAt(this->m_bar, GetCentre(this->GetTabRect(this->m_home)));

    CHECK( WaitFor("panel to be collapsed",
                   [&]() { return panel->GetExpandedPanel() == nullptr; }) );
    CHECK( this->m_buttons->GetParent() == panel );
    CHECK( panel->IsMinimised() );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::KeyTips", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    wxRibbonBar* const bar = this->m_bar;

    // Use only the letters which are at the same place in most of the
    // keyboard layouts.
    bar->SetPageKeyTip(static_cast<size_t>(0), "H");
    bar->SetPageKeyTip(1, "O");
    this->m_buttons->SetKeyTip(ID_BUTTON_NORMAL, "N");
    this->m_buttons->SetKeyTip(ID_BUTTON_TOGGLE, "T");

    this->PrepareForInput();

    this->m_text->SetFocus();
    wxYield();

    EventRecorder changed(bar, wxEVT_RIBBONBAR_PAGE_CHANGED);
    EventRecorder clicked(this->m_buttons, wxEVT_RIBBONBUTTONBAR_CLICKED);

    CHECK_FALSE( bar->AreKeyTipsShown() );

    // The default key showing the key tips.
    this->PressKey(WXK_F10);

    REQUIRE( WaitFor("key tips to be shown",
                     [&]() { return bar->AreKeyTipsShown(); }) );

    // The key tip of a page switches to it and keeps showing the key tips.
    this->PressKey('O');

    CHECK( changed.WaitForCount(1) );
    CHECK( bar->GetActivePage() == 1 );
    CHECK( bar->AreKeyTipsShown() );

    this->PressKey('H');

    CHECK( changed.WaitForCount(2) );
    CHECK( bar->GetActivePage() == 0 );
    CHECK( bar->AreKeyTipsShown() );

    // The key tip of a button activates it and hides the key tips.
    this->PressKey('T');

    REQUIRE( clicked.WaitForCount(1) );
    CHECK( clicked.GetLastId() == ID_BUTTON_TOGGLE );
    CHECK( clicked.GetLastValue() != 0 );
    CHECK_FALSE( bar->AreKeyTipsShown() );

    // The key tips are not used when they are not shown.
    this->PressKey('N');
    YieldForAWhile();

    CHECK( clicked.GetCount() == 1 );

    // The key tips are hidden by Esc or the key showing them.
    this->PressKey(WXK_F10);

    REQUIRE( WaitFor("key tips to be shown again",
                     [&]() { return bar->AreKeyTipsShown(); }) );

    this->PressKey(WXK_ESCAPE);

    CHECK( WaitFor("key tips to be hidden by Esc",
                   [&]() { return !bar->AreKeyTipsShown(); }) );

    this->PressKey(WXK_F10);

    REQUIRE( WaitFor("key tips to be shown once more",
                     [&]() { return bar->AreKeyTipsShown(); }) );

    this->PressKey(WXK_F10);

    CHECK( WaitFor("key tips to be hidden by F10",
                   [&]() { return !bar->AreKeyTipsShown(); }) );

    CHECK( clicked.GetCount() == 1 );
    CHECK( changed.GetCount() == 2 );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::Keyboard", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    wxRibbonBar* const bar = this->m_bar;

    EventRecorder changing(bar, wxEVT_RIBBONBAR_PAGE_CHANGING);
    EventRecorder changed(bar, wxEVT_RIBBONBAR_PAGE_CHANGED);
    EventRecorder clicked(this->m_buttons, wxEVT_RIBBONBUTTONBAR_CLICKED);

    bar->SetFocus();
    REQUIRE( WaitFor("ribbon to get focus", [&]() { return bar->HasFocus(); }) );

    // The arrows change the page when a tab has focus.
    this->PressKey(WXK_RIGHT);

    CHECK( changed.WaitForCount(1) );
    CHECK( changing.GetCount() == 1 );
    CHECK( bar->GetActivePage() == 1 );

    this->PressKey(WXK_LEFT);

    CHECK( changed.WaitForCount(2) );
    CHECK( changing.GetCount() == 2 );
    CHECK( bar->GetActivePage() == 0 );

    // There is no page before the first one.
    this->PressKey(WXK_LEFT);
    YieldForAWhile();

    CHECK( changing.GetCount() == 2 );
    CHECK( bar->GetActivePage() == 0 );

    // The down arrow moves to the first item of the page, which can then be
    // activated.
    this->PressKey(WXK_DOWN);
    this->PressKey(WXK_RETURN);

    REQUIRE( clicked.WaitForCount(1) );
    CHECK( clicked.GetLastId() == ID_BUTTON_NORMAL );

    // The arrows move between the items now.
    this->PressKey(WXK_RIGHT);
    this->PressKey(WXK_SPACE);

    REQUIRE( clicked.WaitForCount(2) );
    CHECK( clicked.GetLastId() == ID_BUTTON_TOGGLE );
    CHECK( clicked.GetLastValue() != 0 );

    this->PressKey(WXK_LEFT);
    this->PressKey(WXK_RETURN);

    REQUIRE( clicked.WaitForCount(3) );
    CHECK( clicked.GetLastId() == ID_BUTTON_NORMAL );

    // Esc goes back to the tabs.
    this->PressKey(WXK_ESCAPE);
    this->PressKey(WXK_RIGHT);

    CHECK( changed.WaitForCount(3) );
    CHECK( bar->GetActivePage() == 1 );
    CHECK( clicked.GetCount() == 3 );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::DropdownClick", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    wxRibbonButtonBar* const buttons = this->m_buttons;
    wxRibbonToolBar* const tools = this->m_tools;

    REQUIRE( buttons->AddDropdownButton(ID_BUTTON_EXTRA, "Dropdown",
                                        MakeBitmap(32)) );
    REQUIRE( tools->AddDropdownTool(ID_TOOL_EXTRA, MakeBitmap(16)) );
    this->Relayout();

    this->PrepareForInput();

    // The whole dropdown button or tool is its dropdown part.
    SECTION("Button")
    {
        EventRecorder clicked(buttons, wxEVT_RIBBONBUTTONBAR_CLICKED);
        EventRecorder dropdown(buttons, wxEVT_RIBBONBUTTONBAR_DROPDOWN_CLICKED);

        this->ClickAt(buttons, GetCentre(buttons->GetItemRect(ID_BUTTON_EXTRA)));

        REQUIRE( dropdown.WaitForCount(1) );
        CHECK( dropdown.GetLastId() == ID_BUTTON_EXTRA );
        CHECK( dropdown.GetLastObject() == buttons );
        CHECK( dropdown.GetLastData() == buttons->GetItemById(ID_BUTTON_EXTRA) );
        CHECK( clicked.GetCount() == 0 );
    }

    SECTION("Tool")
    {
        EventRecorder clicked(tools, wxEVT_RIBBONTOOLBAR_CLICKED);
        EventRecorder dropdown(tools, wxEVT_RIBBONTOOLBAR_DROPDOWN_CLICKED);

        this->ClickAt(tools, GetCentre(tools->GetToolRect(ID_TOOL_EXTRA)));

        REQUIRE( dropdown.WaitForCount(1) );
        CHECK( dropdown.GetLastId() == ID_TOOL_EXTRA );
        CHECK( dropdown.GetLastObject() == tools );
        CHECK( dropdown.GetLastData() == tools );
        CHECK( clicked.GetCount() == 0 );
    }
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::ClickCancelled", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    wxRibbonButtonBar* const buttons = this->m_buttons;
    wxRibbonToolBar* const tools = this->m_tools;

    EventRecorder buttonClicked(buttons, wxEVT_RIBBONBUTTONBAR_CLICKED);
    EventRecorder toolClicked(tools, wxEVT_RIBBONTOOLBAR_CLICKED);

    wxUIActionSimulator sim;

    // Releasing the mouse button outside of the item doesn't click it.
    SECTION("Button")
    {
        this->MoveMouseTo(buttons,
                          GetCentre(buttons->GetItemRect(ID_BUTTON_NORMAL)));
        sim.MouseDown();
        wxYield();

        CHECK( WaitFor("button to be pressed", [&]()
            {
                return buttons->GetActiveItem() ==
                        buttons->GetItemById(ID_BUTTON_NORMAL);
            }) );
        CHECK( this->GetDrawnButtonState("Normal") &
                    wxRIBBON_BUTTONBAR_BUTTON_NORMAL_ACTIVE );

        this->MoveMouseTo(this->m_text, wxPoint(5, 5));
        sim.MouseUp();
        wxYield();
    }

    SECTION("Tool")
    {
        this->MoveMouseTo(tools, GetCentre(tools->GetToolRect(ID_TOOL_NORMAL)));
        sim.MouseDown();
        wxYield();

        CHECK( WaitFor("tool to be pressed", [&]()
            {
                return tools->GetActiveTool() ==
                        tools->FindById(ID_TOOL_NORMAL);
            }) );
        CHECK( this->GetDrawnToolState(ID_TOOL_NORMAL) &
                    wxRIBBON_TOOLBAR_TOOL_NORMAL_ACTIVE );

        this->MoveMouseTo(this->m_text, wxPoint(5, 5));
        sim.MouseUp();
        wxYield();
    }

    YieldForAWhile();

    CHECK( buttonClicked.GetCount() == 0 );
    CHECK( toolClicked.GetCount() == 0 );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::Hover", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    const wxPoint ptOutside(5, 5);

    SECTION("Tab")
    {
        CHECK_FALSE( this->GetTabInfo(this->m_other).hovered );
        CHECK( this->GetTabInfo(this->m_home).active );
        CHECK_FALSE( this->GetTabInfo(this->m_other).active );

        this->MoveMouseTo(this->m_bar,
                          GetCentre(this->GetTabRect(this->m_other)));

        CHECK( WaitFor("tab to be hovered",
                       [&]() { return this->GetTabInfo(this->m_other).hovered; }) );
        CHECK_FALSE( this->GetTabInfo(this->m_home).hovered );

        this->MoveMouseTo(this->m_text, ptOutside);

        CHECK( WaitFor("tab not to be hovered",
                       [&]() { return !this->GetTabInfo(this->m_other).hovered; }) );
    }

    SECTION("Hybrid button")
    {
        wxRibbonButtonBar* const buttons = this->m_buttons;

        const wxRect rect = buttons->GetItemRect(ID_BUTTON_HYBRID);
        const wxRect rectDropdown = buttons->GetItemDropdownRect(ID_BUTTON_HYBRID);

        const long mask = wxRIBBON_BUTTONBAR_BUTTON_HOVER_MASK;

        this->MoveMouseTo(buttons, this->GetHybridMainPoint(rect, rectDropdown));

        CHECK( WaitFor("main part to be hovered", [&]()
            {
                return (this->GetDrawnButtonState("Hybrid") & mask) ==
                            wxRIBBON_BUTTONBAR_BUTTON_NORMAL_HOVERED;
            }) );

        this->MoveMouseTo(buttons, GetCentre(rectDropdown));

        CHECK( WaitFor("dropdown part to be hovered", [&]()
            {
                return (this->GetDrawnButtonState("Hybrid") & mask) ==
                            wxRIBBON_BUTTONBAR_BUTTON_DROPDOWN_HOVERED;
            }) );

        // The other buttons are not hovered.
        CHECK_FALSE( this->GetDrawnButtonState("Normal") & mask );

        this->MoveMouseTo(this->m_text, ptOutside);

        CHECK( WaitFor("button not to be hovered", [&]()
            {
                return !(this->GetDrawnButtonState("Hybrid") & mask);
            }) );
    }

    SECTION("Tool")
    {
        wxRibbonToolBar* const tools = this->m_tools;

        const long mask = wxRIBBON_TOOLBAR_TOOL_HOVER_MASK;

        this->MoveMouseTo(tools, GetCentre(tools->GetToolRect(ID_TOOL_NORMAL)));

        CHECK( WaitFor("tool to be hovered", [&]()
            {
                return (this->GetDrawnToolState(ID_TOOL_NORMAL) & mask) ==
                            wxRIBBON_TOOLBAR_TOOL_NORMAL_HOVERED;
            }) );
        CHECK( this->m_toolsPanel->IsHovered() );

        const wxRect rect = tools->GetToolRect(ID_TOOL_HYBRID);
        const wxRect rectDropdown = tools->GetToolDropdownRect(ID_TOOL_HYBRID);

        this->MoveMouseTo(tools, this->GetHybridMainPoint(rect, rectDropdown));

        CHECK( WaitFor("main part to be hovered", [&]()
            {
                return (this->GetDrawnToolState(ID_TOOL_HYBRID) & mask) ==
                            wxRIBBON_TOOLBAR_TOOL_NORMAL_HOVERED;
            }) );
        CHECK_FALSE( this->GetDrawnToolState(ID_TOOL_NORMAL) & mask );

        this->MoveMouseTo(tools, GetCentre(rectDropdown));

        CHECK( WaitFor("dropdown part to be hovered", [&]()
            {
                return (this->GetDrawnToolState(ID_TOOL_HYBRID) & mask) ==
                            wxRIBBON_TOOLBAR_TOOL_DROPDOWN_HOVERED;
            }) );

        // The disabled tools are not hovered.
        this->MoveMouseTo(tools, GetCentre(tools->GetToolRect(ID_TOOL_DISABLED)));
        YieldForAWhile();

        CHECK_FALSE( this->GetDrawnToolState(ID_TOOL_DISABLED) & mask );
        CHECK_FALSE( this->GetDrawnToolState(ID_TOOL_HYBRID) & mask );

        this->MoveMouseTo(this->m_text, ptOutside);

        CHECK( WaitFor("panel not to be hovered",
                       [&]() { return !this->m_toolsPanel->IsHovered(); }) );
    }
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::Disabled", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    wxRibbonBar* const bar = this->m_bar;
    wxRibbonButtonBar* const buttons = this->m_buttons;

    EventRecorder changing(bar, wxEVT_RIBBONBAR_PAGE_CHANGING);
    EventRecorder buttonClicked(buttons, wxEVT_RIBBONBUTTONBAR_CLICKED);
    EventRecorder toolClicked(this->m_tools, wxEVT_RIBBONTOOLBAR_CLICKED);

    const wxPoint ptButton = GetCentre(buttons->GetItemRect(ID_BUTTON_NORMAL));

    SECTION("Control")
    {
        // Disabling the control resets its hovered item.
        this->MoveMouseTo(buttons, ptButton);

        REQUIRE( WaitFor("button to be hovered",
                         [&]() { return buttons->GetHoveredItem() != nullptr; }) );

        buttons->Disable();

        CHECK( buttons->GetHoveredItem() == nullptr );

        this->ClickAt(buttons, ptButton);
        YieldForAWhile();

        CHECK( buttonClicked.GetCount() == 0 );

        // The other controls still work.
        this->ClickAt(this->m_tools,
                      GetCentre(this->m_tools->GetToolRect(ID_TOOL_NORMAL)));

        CHECK( toolClicked.WaitForCount(1) );

        buttons->Enable();

        this->ClickAt(buttons, ptButton);

        CHECK( buttonClicked.WaitForCount(1) );
    }

    SECTION("Bar")
    {
        bar->Disable();

        this->ClickAt(bar, GetCentre(this->GetTabRect(this->m_other)));
        this->ClickAt(buttons, ptButton);
        this->ClickAt(this->m_tools,
                      GetCentre(this->m_tools->GetToolRect(ID_TOOL_NORMAL)));
        YieldForAWhile();

        CHECK( changing.GetCount() == 0 );
        CHECK( bar->GetActivePage() == 0 );
        CHECK( buttonClicked.GetCount() == 0 );
        CHECK( toolClicked.GetCount() == 0 );

        bar->Enable();

        this->ClickAt(buttons, ptButton);

        CHECK( buttonClicked.WaitForCount(1) );
    }
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::ExpandedMode", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    wxRibbonBar* const bar = this->m_bar;

    bar->HidePanels();

    this->PrepareForInput();

    const auto isExpanded = [&]()
        {
            return bar->GetDisplayMode() == wxRIBBON_BAR_EXPANDED;
        };
    const auto isMinimised = [&]()
        {
            return bar->GetDisplayMode() == wxRIBBON_BAR_MINIMIZED;
        };

    // Clicking a tab of the minimised ribbon shows its panels temporarily.
    const auto expand = [&]()
        {
            this->WaitForDoubleClickTime();
            this->ClickAt(bar, GetCentre(this->GetTabRect(this->m_home)));

            REQUIRE( WaitFor("ribbon to be expanded", isExpanded) );
            CHECK( bar->ArePanelsShown() );
            CHECK( this->m_home->IsShown() );
        };

    expand();

    SECTION("Click tab")
    {
        this->WaitForDoubleClickTime();
        this->ClickAt(bar, GetCentre(this->GetTabRect(this->m_home)));
    }

    SECTION("Click other tab")
    {
        // This changes the page, but keeps the panels shown.
        this->ClickAt(bar, GetCentre(this->GetTabRect(this->m_other)));

        CHECK( WaitFor("page to be changed",
                       [&]() { return bar->GetActivePage() == 1; }) );
        CHECK( isExpanded() );

        this->PressKey(WXK_ESCAPE);
    }

    SECTION("Escape")
    {
        this->PressKey(WXK_ESCAPE);
    }

    SECTION("Lose focus")
    {
        this->ClickAt(this->m_text, wxPoint(5, 5));
    }

    SECTION("Click button")
    {
        EventRecorder clicked(this->m_buttons, wxEVT_RIBBONBUTTONBAR_CLICKED);

        this->ClickAt(this->m_buttons,
                      GetCentre(this->m_buttons->GetItemRect(ID_BUTTON_NORMAL)));

        REQUIRE( clicked.WaitForCount(1) );
        CHECK( clicked.GetLastId() == ID_BUTTON_NORMAL );
    }

    CHECK( WaitFor("ribbon to be minimised", isMinimised) );
    CHECK_FALSE( bar->ArePanelsShown() );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::TabScroll", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    wxRibbonBar* const bar = this->m_bar;

    CHECK( this->GetTabScrollButtonRect(wxRIBBON_SCROLL_BTN_LEFT).IsEmpty() );
    CHECK( this->GetTabScrollButtonRect(wxRIBBON_SCROLL_BTN_RIGHT).IsEmpty() );

    // Add more pages than can be shown.
    for ( int n = 0; n < 8; ++n )
    {
        new wxRibbonPage(bar, wxID_ANY,
                         wxString::Format("Additional page %d", n));
    }

    this->Relayout();
    this->SetHostWidth(this->m_frame->FromDIP(300));

    this->PrepareForInput();

    // Only scrolling to the right is possible initially.
    CHECK( this->GetTabScrollButtonRect(wxRIBBON_SCROLL_BTN_LEFT).IsEmpty() );

    const wxRect
        rectRight = this->GetTabScrollButtonRect(wxRIBBON_SCROLL_BTN_RIGHT);
    REQUIRE_FALSE( rectRight.IsEmpty() );

    const int x = this->GetTabRect(this->m_other).x;

    this->ClickAt(bar, GetCentre(rectRight));

    CHECK( WaitFor("tabs to be scrolled",
                   [&]() { return this->GetTabRect(this->m_other).x < x; }) );

    const wxRect
        rectLeft = this->GetTabScrollButtonRect(wxRIBBON_SCROLL_BTN_LEFT);
    REQUIRE_FALSE( rectLeft.IsEmpty() );

    this->ClickAt(bar, GetCentre(rectLeft));

    CHECK( WaitFor("tabs to be scrolled back",
                   [&]() { return this->GetTabRect(this->m_other).x == x; }) );
    CHECK( this->GetTabScrollButtonRect(wxRIBBON_SCROLL_BTN_LEFT).IsEmpty() );

    // Scrolling doesn't change the active page.
    CHECK( bar->GetActivePage() == 0 );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::GalleryButtons", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    wxRibbonGallery* const gallery = this->m_gallery;

    REQUIRE( this->m_bar->SetActivePage(1) );

    // Add enough items to make scrolling the gallery necessary.
    const wxBitmap bmp = MakeBitmap(32);
    for ( int n = 0; n < 200; ++n )
        gallery->Append(bmp, wxID_ANY);

    this->Relayout();

    this->PrepareForInput();

    EventRecorder extensionClicked(gallery, wxEVT_BUTTON);
    EventRecorder itemClicked(gallery, wxEVT_RIBBONGALLERY_CLICKED);

    wxRect up, down, extension;
    this->GetGalleryButtonRects(&up, &down, &extension);
    REQUIRE_FALSE( up.IsEmpty() );
    REQUIRE_FALSE( down.IsEmpty() );
    REQUIRE_FALSE( extension.IsEmpty() );

    CHECK( gallery->GetUpButtonState() == wxRIBBON_GALLERY_BUTTON_DISABLED );

    this->MoveMouseTo(gallery, GetCentre(down));

    CHECK( WaitFor("down button to be hovered", [&]()
        {
            return gallery->GetDownButtonState() ==
                        wxRIBBON_GALLERY_BUTTON_HOVERED;
        }) );

    const int y = this->GetGalleryItemRect(gallery->GetItem(0)).y;

    this->ClickAt(gallery, GetCentre(down));

    CHECK( WaitFor("gallery to be scrolled down", [&]()
        {
            return gallery->GetUpButtonState() !=
                        wxRIBBON_GALLERY_BUTTON_DISABLED;
        }) );
    CHECK( this->GetGalleryItemRect(gallery->GetItem(0)).y < y );

    this->ClickAt(gallery, GetCentre(up));

    CHECK( WaitFor("gallery to be scrolled up", [&]()
        {
            return this->GetGalleryItemRect(gallery->GetItem(0)).y == y;
        }) );

    CHECK( extensionClicked.GetCount() == 0 );

    this->ClickAt(gallery, GetCentre(extension));

    CHECK( extensionClicked.WaitForCount(1) );
    CHECK( extensionClicked.GetLastId() == gallery->GetId() );

    // None of this selects or clicks any items.
    CHECK( itemClicked.GetCount() == 0 );
    CHECK( gallery->GetSelection() == nullptr );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::HandlerChangesRibbon", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    wxRibbonBar* const bar = this->m_bar;
    wxRibbonButtonBar* const buttons = this->m_buttons;

    SECTION("Delete clicked button")
    {
        buttons->Bind(wxEVT_RIBBONBUTTONBAR_CLICKED,
                      [=](wxRibbonButtonBarEvent& event)
            {
                if ( event.GetId() == ID_BUTTON_NORMAL )
                {
                    buttons->DeleteButton(ID_BUTTON_NORMAL);
                    bar->Realize();
                }
            });
    }

    SECTION("Delete all buttons")
    {
        buttons->Bind(wxEVT_RIBBONBUTTONBAR_CLICKED,
                      [=](wxRibbonButtonBarEvent& event)
            {
                if ( event.GetId() == ID_BUTTON_NORMAL )
                {
                    buttons->ClearButtons();
                    buttons->AddToggleButton(ID_BUTTON_TOGGLE, "Toggle",
                                             MakeBitmap(32));
                    bar->Realize();
                }
            });
    }

    SECTION("Realize")
    {
        buttons->Bind(wxEVT_RIBBONBUTTONBAR_CLICKED,
                      [=](wxRibbonButtonBarEvent& event)
            {
                if ( event.GetId() == ID_BUTTON_NORMAL )
                {
                    buttons->SetButtonText(ID_BUTTON_NORMAL, "Clicked");
                    bar->Realize();
                }
            });
    }

    // The recorder must be created after binding the handlers above, as
    // they don't skip the event and the last bound handler is called first.
    EventRecorder clicked(buttons, wxEVT_RIBBONBUTTONBAR_CLICKED);

    this->ClickAt(buttons, GetCentre(buttons->GetItemRect(ID_BUTTON_NORMAL)));

    REQUIRE( clicked.WaitForCount(1) );
    CHECK( clicked.GetLastId() == ID_BUTTON_NORMAL );
    YieldForAWhile();

    CHECK( buttons->GetActiveItem() == nullptr );

    // The ribbon still works after this.
    this->m_host->Layout();
    this->MoveMouseTo(this->m_text, wxPoint(5, 5));

    REQUIRE( buttons->GetItemById(ID_BUTTON_TOGGLE) );
    this->ClickAt(buttons, GetCentre(buttons->GetItemRect(ID_BUTTON_TOGGLE)));

    REQUIRE( clicked.WaitForCount(2) );
    CHECK( clicked.GetLastId() == ID_BUTTON_TOGGLE );
    CHECK( clicked.GetLastValue() != 0 );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::KeyTipsTargets", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    wxRibbonBar* const bar = this->m_bar;
    wxRibbonButtonBar* const buttons = this->m_buttons;
    wxRibbonToolBar* const tools = this->m_tools;
    wxRibbonPanel* const panel = this->m_toolsPanel;

    // Use only the letters which are at the same place in most of the
    // keyboard layouts.
    bar->SetPageKeyTip(static_cast<size_t>(0), "H");
    bar->SetPageKeyTip(this->m_other, "O");
    bar->SetToggleButtonKeyTip("G");
    bar->SetHelpButtonKeyTip("P");
    buttons->SetKeyTip(ID_BUTTON_NORMAL, "FA");
    buttons->SetKeyTip(ID_BUTTON_TOGGLE, "fb");
    buttons->SetKeyTip(ID_BUTTON_HYBRID, "U");
    buttons->SetDropdownKeyTip(ID_BUTTON_HYBRID, "D");
    buttons->SetKeyTip(ID_BUTTON_DISABLED, "I");
    tools->SetKeyTip(ID_TOOL_NORMAL, "L");

    // The key tip of the drop down part is only used if the item itself has
    // a key tip too.
    tools->SetKeyTip(ID_TOOL_HYBRID, "M");
    tools->SetDropdownKeyTip(ID_TOOL_HYBRID, "R");
    tools->SetDropdownKeyTip(ID_TOOL_TOGGLE, "S");

    panel->SetExtButtonKeyTip("E");

    // The key tips are case-insensitive.
    CHECK( buttons->GetKeyTip(ID_BUTTON_TOGGLE) == "FB" );
    CHECK( buttons->GetDropdownKeyTip(ID_BUTTON_HYBRID) == "D" );
    CHECK( buttons->GetKeyTip(ID_BUTTON_EXTRA) == "" );
    CHECK( tools->GetKeyTip(ID_TOOL_NORMAL) == "L" );
    CHECK( tools->GetDropdownKeyTip(ID_TOOL_HYBRID) == "R" );
    CHECK( panel->GetExtButtonKeyTip() == "E" );

    this->PrepareForInput();

    this->m_text->SetFocus();
    wxYield();

    EventRecorder buttonClicked(buttons, wxEVT_RIBBONBUTTONBAR_CLICKED);
    EventRecorder buttonDropdown(buttons, wxEVT_RIBBONBUTTONBAR_DROPDOWN_CLICKED);
    EventRecorder toolClicked(tools, wxEVT_RIBBONTOOLBAR_CLICKED);
    EventRecorder toolDropdown(tools, wxEVT_RIBBONTOOLBAR_DROPDOWN_CLICKED);
    EventRecorder extActivated(panel, wxEVT_RIBBONPANEL_EXTBUTTON_ACTIVATED);
    EventRecorder toggled(bar, wxEVT_RIBBONBAR_TOGGLED);
    EventRecorder help(bar, wxEVT_RIBBONBAR_HELP_CLICK);

    const auto showKeyTips = [&]()
        {
            this->PressKey(WXK_F10);

            REQUIRE( WaitFor("key tips to be shown",
                             [&]() { return bar->AreKeyTipsShown(); }) );
        };

    CHECK( this->GetKeyTips().empty() );

    showKeyTips();

    // The key tip of the disabled button is not shown.
    const std::vector<wxString> all =
    {
        "D", "E", "FA", "FB", "G", "H", "L", "M", "O", "P", "R", "U"
    };
    CHECK( this->GetKeyTips() == all );

    SECTION("Prefix")
    {
        // Only the key tips starting with the entered letter remain, and only
        // their remaining part is shown.
        this->PressKey('F');
        YieldForAWhile();

        CHECK( bar->AreKeyTipsShown() );
        CHECK( this->GetKeyTips() == std::vector<wxString>{"A", "B"} );
        CHECK( buttonClicked.GetCount() == 0 );

        // The keys not corresponding to any key tip are ignored.
        this->PressKey('L');
        YieldForAWhile();

        CHECK( bar->AreKeyTipsShown() );
        CHECK( toolClicked.GetCount() == 0 );

        // Esc goes back to showing all the key tips.
        this->PressKey(WXK_ESCAPE);
        YieldForAWhile();

        CHECK( bar->AreKeyTipsShown() );
        CHECK( this->GetKeyTips() == all );

        this->PressKey('F');
        this->PressKey('B');

        REQUIRE( buttonClicked.WaitForCount(1) );
        CHECK( buttonClicked.GetLastId() == ID_BUTTON_TOGGLE );
        CHECK_FALSE( bar->AreKeyTipsShown() );
        CHECK( this->GetKeyTips().empty() );
    }

    SECTION("Button")
    {
        this->PressKey('U');

        REQUIRE( buttonClicked.WaitForCount(1) );
        CHECK( buttonClicked.GetLastId() == ID_BUTTON_HYBRID );
        CHECK( buttonDropdown.GetCount() == 0 );
        CHECK_FALSE( bar->AreKeyTipsShown() );

        showKeyTips();
        this->PressKey('D');

        REQUIRE( buttonDropdown.WaitForCount(1) );
        CHECK( buttonDropdown.GetLastId() == ID_BUTTON_HYBRID );
        CHECK( buttonClicked.GetCount() == 1 );
        CHECK_FALSE( bar->AreKeyTipsShown() );
    }

    SECTION("Tool")
    {
        this->PressKey('L');

        REQUIRE( toolClicked.WaitForCount(1) );
        CHECK( toolClicked.GetLastId() == ID_TOOL_NORMAL );
        CHECK_FALSE( bar->AreKeyTipsShown() );

        showKeyTips();
        this->PressKey('R');

        REQUIRE( toolDropdown.WaitForCount(1) );
        CHECK( toolDropdown.GetLastId() == ID_TOOL_HYBRID );
        CHECK( toolClicked.GetCount() == 1 );
    }

    SECTION("Extension button")
    {
        this->PressKey('E');

        REQUIRE( extActivated.WaitForCount(1) );
        CHECK( extActivated.GetLastData() == panel );
        CHECK_FALSE( bar->AreKeyTipsShown() );
    }

    SECTION("Bar buttons")
    {
        this->PressKey('P');

        CHECK( help.WaitForCount(1) );
        CHECK_FALSE( bar->AreKeyTipsShown() );

        showKeyTips();
        this->PressKey('G');

        CHECK( toggled.WaitForCount(1) );
        CHECK_FALSE( bar->ArePanelsShown() );
        CHECK_FALSE( bar->AreKeyTipsShown() );

        // Only the key tips of the bar itself are available when the panels
        // are not shown.
        showKeyTips();

        CHECK( this->GetKeyTips() == std::vector<wxString>{"G", "H", "O", "P"} );

        // Using the key tip of a page shows its panels.
        this->PressKey('O');

        CHECK( WaitFor("page to be changed",
                       [&]() { return bar->GetActivePage() == 1; }) );
        CHECK( bar->GetDisplayMode() == wxRIBBON_BAR_EXPANDED );
        CHECK( bar->AreKeyTipsShown() );

        this->PressKey(WXK_ESCAPE);
    }

    SECTION("Other page")
    {
        this->PressKey('O');

        CHECK( WaitFor("page to be changed",
                       [&]() { return bar->GetActivePage() == 1; }) );

        // The key tips of the items of the other page are not shown any more.
        CHECK( this->GetKeyTips() == std::vector<wxString>{"G", "H", "O", "P"} );

        this->PressKey('L');
        YieldForAWhile();

        CHECK( toolClicked.GetCount() == 0 );
        CHECK( bar->AreKeyTipsShown() );

        this->PressKey(WXK_ESCAPE);
    }

    SECTION("Mouse click")
    {
        // Clicking anywhere in the ribbon hides the key tips.
        this->ClickAt(bar, GetCentre(this->GetTabRect(this->m_home)));

        CHECK( WaitFor("key tips to be hidden",
                       [&]() { return !bar->AreKeyTipsShown(); }) );
    }

    SECTION("Accelerator")
    {
        // The keys pressed with modifiers are not used for the key tips.
        wxUIActionSimulator sim;
        sim.Char('L', wxMOD_CONTROL);
        wxYield();

        CHECK( WaitFor("key tips to be hidden",
                       [&]() { return !bar->AreKeyTipsShown(); }) );
        CHECK( toolClicked.GetCount() == 0 );
    }

    CHECK( WaitFor("key tips to be hidden",
                   [&]() { return !bar->AreKeyTipsShown(); }) );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::KeyTipsTrigger", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    wxRibbonBar* const bar = this->m_bar;

    bar->SetPageKeyTip(1, "O");
    bar->SetKeyTipsTriggerKey(WXK_F6);
    bar->AddKeyTipsTriggerKey(WXK_F7);

    this->PrepareForInput();

    this->m_text->SetFocus();
    wxYield();

    const auto areShown = [&]() { return bar->AreKeyTipsShown(); };
    const auto areHidden = [&]() { return !bar->AreKeyTipsShown(); };

    this->PressKey(WXK_F6);
    CHECK( WaitFor("key tips to be shown by F6", areShown) );

    // Any of the trigger keys hides them too.
    this->PressKey(WXK_F7);
    CHECK( WaitFor("key tips to be hidden by F7", areHidden) );

    this->PressKey(WXK_F7);
    CHECK( WaitFor("key tips to be shown by F7", areShown) );

    this->PressKey(WXK_F6);
    CHECK( WaitFor("key tips to be hidden by F6", areHidden) );

    // Nothing shows the key tips without any trigger keys...
    bar->ClearKeyTipsTriggerKeys();

    this->PressKey(WXK_F6);
    YieldForAWhile();
    CHECK_FALSE( bar->AreKeyTipsShown() );

    // ... but this can still be done programmatically.
    CHECK( bar->ShowKeyTips() );
    CHECK( bar->AreKeyTipsShown() );

    this->PressKey('O');
    CHECK( WaitFor("page to be changed",
                   [&]() { return bar->GetActivePage() == 1; }) );

    bar->HideKeyTips();
    CHECK_FALSE( bar->AreKeyTipsShown() );

    // There is nothing to show without any key tips.
    bar->SetPageKeyTip(1, "");
    CHECK_FALSE( bar->ShowKeyTips() );
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::KeyboardVeto", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    wxRibbonBar* const bar = this->m_bar;

    bar->SetPageKeyTip(1, "O");

    this->PrepareForInput();

    EventRecorder changed(bar, wxEVT_RIBBONBAR_PAGE_CHANGED);

    bar->Bind(wxEVT_RIBBONBAR_PAGE_CHANGING,
              [](wxRibbonBarEvent& event) { event.Veto(); });

    // This must be created after binding the handler above, which doesn't
    // skip the event, as the last bound handler is called first.
    EventRecorder changing(bar, wxEVT_RIBBONBAR_PAGE_CHANGING);

    bar->SetFocus();
    REQUIRE( WaitFor("ribbon to get focus", [&]() { return bar->HasFocus(); }) );

    SECTION("Arrow")
    {
        this->PressKey(WXK_RIGHT);
    }

    SECTION("End")
    {
        this->PressKey(WXK_END);
    }

    SECTION("Key tip")
    {
        this->PressKey(WXK_F10);

        REQUIRE( WaitFor("key tips to be shown",
                         [&]() { return bar->AreKeyTipsShown(); }) );

        this->PressKey('O');
    }

    REQUIRE( changing.WaitForCount(1) );
    CHECK( changing.GetLastData() == this->m_other );
    YieldForAWhile();

    CHECK( changed.GetCount() == 0 );
    CHECK( bar->GetActivePage() == 0 );
    CHECK( this->m_home->IsShown() );

    bar->HideKeyTips();
}

TEMPLATE_TEST_CASE_METHOD(RibbonTestCase, "Ribbon::KeyboardItems", "[ui][ribbon]",
                          wxRIBBON_TEST_ART_PROVIDERS)
{
    if ( !EnableUITests() )
        return;

    this->PrepareForInput();

    wxRibbonBar* const bar = this->m_bar;
    wxRibbonGallery* const gallery = this->m_gallery;

    EventRecorder changed(bar, wxEVT_RIBBONBAR_PAGE_CHANGED);
    EventRecorder buttonClicked(this->m_buttons, wxEVT_RIBBONBUTTONBAR_CLICKED);
    EventRecorder buttonDropdown(this->m_buttons,
                                 wxEVT_RIBBONBUTTONBAR_DROPDOWN_CLICKED);
    EventRecorder toolClicked(this->m_tools, wxEVT_RIBBONTOOLBAR_CLICKED);
    EventRecorder toolDropdown(this->m_tools,
                               wxEVT_RIBBONTOOLBAR_DROPDOWN_CLICKED);
    EventRecorder extActivated(this->m_toolsPanel,
                               wxEVT_RIBBONPANEL_EXTBUTTON_ACTIVATED);
    EventRecorder gallerySelected(gallery, wxEVT_RIBBONGALLERY_SELECTED);
    EventRecorder galleryClicked(gallery, wxEVT_RIBBONGALLERY_CLICKED);
    EventRecorder toggled(bar, wxEVT_RIBBONBAR_TOGGLED);
    EventRecorder help(bar, wxEVT_RIBBONBAR_HELP_CLICK);

    bar->SetFocus();
    REQUIRE( WaitFor("ribbon to get focus", [&]() { return bar->HasFocus(); }) );

    SECTION("Home page")
    {
        // Tab moves from the tabs to the first item of the page.
        this->PressKey(WXK_TAB);
        this->PressKey(WXK_RETURN);

        REQUIRE( buttonClicked.WaitForCount(1) );
        CHECK( buttonClicked.GetLastId() == ID_BUTTON_NORMAL );

        // End moves to the last item, which is the panel extension button.
        this->PressKey(WXK_END);
        this->PressKey(WXK_RETURN);

        REQUIRE( extActivated.WaitForCount(1) );
        CHECK( extActivated.GetLastData() == this->m_toolsPanel );

        // The last tool is before it, as the disabled one is skipped.
        this->PressKey(WXK_LEFT);
        this->PressKey(WXK_RETURN);

        REQUIRE( toolClicked.WaitForCount(1) );
        CHECK( toolClicked.GetLastId() == ID_TOOL_HYBRID );
        CHECK( toolDropdown.GetCount() == 0 );

        // The down arrow activates the drop down part.
        this->PressKey(WXK_DOWN);

        REQUIRE( toolDropdown.WaitForCount(1) );
        CHECK( toolDropdown.GetLastId() == ID_TOOL_HYBRID );
        CHECK( toolClicked.GetCount() == 1 );

        // Home moves back to the first item.
        this->PressKey(WXK_HOME);
        this->PressKey(WXK_SPACE);

        REQUIRE( buttonClicked.WaitForCount(2) );
        CHECK( buttonClicked.GetLastId() == ID_BUTTON_NORMAL );

        // The disabled button is skipped too.
        this->PressKey(WXK_RIGHT);
        this->PressKey(WXK_RIGHT);
        this->PressKey(WXK_DOWN);

        REQUIRE( buttonDropdown.WaitForCount(1) );
        CHECK( buttonDropdown.GetLastId() == ID_BUTTON_HYBRID );

        this->PressKey(WXK_RIGHT);
        this->PressKey(WXK_RETURN);

        REQUIRE( toolClicked.WaitForCount(2) );
        CHECK( toolClicked.GetLastId() == ID_TOOL_NORMAL );

        // The up arrow goes back to the tabs.
        this->PressKey(WXK_UP);
        this->PressKey(WXK_RIGHT);

        CHECK( changed.WaitForCount(1) );
        CHECK( bar->GetActivePage() == 1 );
        CHECK( buttonClicked.GetCount() == 2 );
        CHECK( toolClicked.GetCount() == 2 );
    }

    SECTION("Gallery")
    {
        // End selects the last page when the tabs have focus.
        this->PressKey(WXK_END);

        REQUIRE( changed.WaitForCount(1) );
        CHECK( bar->GetActivePage() == 1 );

        this->PressKey(WXK_DOWN);
        this->PressKey(WXK_RETURN);

        REQUIRE( galleryClicked.WaitForCount(1) );
        CHECK( galleryClicked.GetLastData() == gallery->GetItem(0) );
        CHECK( gallerySelected.GetCount() == 1 );
        CHECK( gallery->GetSelection() == gallery->GetItem(0) );

        this->PressKey(WXK_RIGHT);
        this->PressKey(WXK_SPACE);

        REQUIRE( galleryClicked.WaitForCount(2) );
        CHECK( galleryClicked.GetLastData() == gallery->GetItem(1) );
        CHECK( gallerySelected.GetCount() == 2 );
        CHECK( gallery->GetSelection() == gallery->GetItem(1) );

        // Home selects the first page when the tabs have focus.
        this->PressKey(WXK_ESCAPE);
        this->PressKey(WXK_HOME);

        CHECK( changed.WaitForCount(2) );
        CHECK( bar->GetActivePage() == 0 );
    }

    SECTION("Bar buttons")
    {
        this->PressKey(WXK_END);

        REQUIRE( changed.WaitForCount(1) );

        // Make sure the positions of the buttons are known.
        Repaint(bar);

        // The toggle and help buttons are after the last tab. Their order
        // depends on the art provider, so just check that both can be used.
        this->PressKey(WXK_RIGHT);
        this->PressKey(WXK_RETURN);

        CHECK( WaitFor("first button to be used", [&]()
            {
                return toggled.GetCount() + help.GetCount() == 1;
            }) );

        this->PressKey(WXK_RIGHT);
        this->PressKey(WXK_SPACE);

        CHECK( toggled.WaitForCount(1) );
        CHECK( help.WaitForCount(1) );
        CHECK_FALSE( bar->ArePanelsShown() );

        // The page didn't change.
        CHECK( changed.GetCount() == 1 );

        // Going back to the tabs.
        this->PressKey(WXK_LEFT);
        this->PressKey(WXK_LEFT);
        this->PressKey(WXK_LEFT);

        CHECK( changed.WaitForCount(2) );
        CHECK( bar->GetActivePage() == 0 );
    }

    SECTION("Minimised")
    {
        bar->HidePanels();

        // The panels are shown temporarily and the first item is focused.
        this->PressKey(WXK_DOWN);

        REQUIRE( WaitFor("ribbon to be expanded", [&]()
            {
                return bar->GetDisplayMode() == wxRIBBON_BAR_EXPANDED;
            }) );

        // The first Esc goes back to the tabs and the second one hides the
        // panels.
        this->PressKey(WXK_ESCAPE);
        YieldForAWhile();

        CHECK( bar->GetDisplayMode() == wxRIBBON_BAR_EXPANDED );

        this->PressKey(WXK_ESCAPE);

        CHECK( WaitFor("ribbon to be minimised", [&]()
            {
                return bar->GetDisplayMode() == wxRIBBON_BAR_MINIMIZED;
            }) );

        // Using an item hides the panels too.
        this->PressKey(WXK_RETURN);
        this->PressKey(WXK_RETURN);

        REQUIRE( buttonClicked.WaitForCount(1) );
        CHECK( buttonClicked.GetLastId() == ID_BUTTON_NORMAL );
        CHECK( WaitFor("ribbon to be minimised again", [&]()
            {
                return bar->GetDisplayMode() == wxRIBBON_BAR_MINIMIZED;
            }) );
    }
}

#endif // wxUSE_UIACTIONSIMULATOR

#endif // wxUSE_RIBBON
