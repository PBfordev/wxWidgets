///////////////////////////////////////////////////////////////////////////////
// Name:        wx/ribbon/art_mso.h
// Purpose:     Microsoft Office 365 style art provider for ribbon interface
// Author:      wxWidgets team
// Created:     2026-10-30
// Copyright:   (c) wxWidgets team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_RIBBON_ART_MSO_H_
#define _WX_RIBBON_ART_MSO_H_

#include "wx/defs.h"

// This art provider draws its glyphs from SVG images and antialiased shapes
// using wxGraphicsContext, so it needs both SVG and graphics context support.
#if wxUSE_RIBBON && defined(wxHAS_SVG) && wxUSE_GRAPHICS_CONTEXT

#include "wx/ribbon/art.h"
#include "wx/geometry.h"

#include <map>

class wxRibbonMSOGlyphCache;

// Art provider imitating the look of the ribbon used by Microsoft Office 365:
// tabs without borders with the active one marked by an accent underline, a
// rounded "card" containing the page, groups separated by thin vertical lines
// and buttons highlighted with rounded rectangles only when hovered, pressed
// or toggled.
//
// It derives from wxRibbonMSWFlatArtProvider as it is a flat theme too, but
// overrides all of its drawing and most of its measurement functions.
//
// Colour scheme: primary colour is used for the background of the tab area
// (and determines whether the light or dark variant is used), secondary
// colour is the accent used for the active tab underline and the labels of
// the highlighted tabs and tertiary colour is the text colour (it's ignored if it doesn't
// contrast with the background).
class WXDLLIMPEXP_RIBBON wxRibbonMSOArtProvider : public wxRibbonMSWFlatArtProvider
{
public:
    wxRibbonMSOArtProvider();
    virtual ~wxRibbonMSOArtProvider();

    wxNODISCARD wxRibbonArtProvider* Clone() const override;

    void SetColourScheme(const wxColour& primary,
                         const wxColour& secondary,
                         const wxColour& tertiary) override;

    void UpdateColoursFromSystem() override;

    // Use the accent colour from the system settings, if available, as the
    // secondary colour of the default colour scheme (this is the default),
    // or always use the Office blue. Resets the colour scheme to the default.
    void UseSystemAccentColour(bool use = true);
    bool IsUsingSystemAccentColour() const { return m_use_system_accent; }

    // The appearance for which the colours set by the functions below are
    // used.
    enum class Appearance
    {
        Light,
        Dark
    };

    // Set the colour scheme used instead of the default one when the system
    // uses the given appearance. Invalid colours mean that the default ones
    // are used. If the appearance is the current one, the colours are
    // updated immediately.
    void SetAppearanceColourScheme(Appearance appearance,
                                   const wxColour& primary,
                                   const wxColour& secondary = wxNullColour,
                                   const wxColour& tertiary = wxNullColour);

    // Get the colours set by SetAppearanceColourScheme(), the ones not set
    // are returned as invalid. Any pointer may be null.
    void GetAppearanceColourScheme(Appearance appearance,
                                   wxColour* primary,
                                   wxColour* secondary,
                                   wxColour* tertiary) const;

    // Set the individual colour used for the given appearance, overriding the
    // colour computed from the scheme. An invalid colour removes the override.
    void SetAppearanceColour(Appearance appearance,
                             int id,
                             const wxColour& colour);

    // Get the colour set by SetAppearanceColour() or an invalid one.
    wxColour GetAppearanceColour(Appearance appearance, int id) const;

    // Forget all the colours set for the given appearance.
    void ResetAppearanceColours(Appearance appearance);

    // The size metrics are stored in DIPs and returned in pixels for the DPI
    // of the last window measured by this provider.
    int GetMetric(int id) const override;
    void SetMetric(int id, int new_val) override;

    int GetTabCtrlHeight(
                        wxReadOnlyDC& dc,
                        wxWindow* wnd,
                        const wxRibbonPageTabInfoArray& pages) override;

    void GetBarTabWidth(
                        wxReadOnlyDC& dc,
                        wxWindow* wnd,
                        const wxString& label,
                        const wxBitmap& bitmap,
                        int* ideal,
                        int* small_begin_need_separator,
                        int* small_must_have_separator,
                        int* minimum) override;

    void DrawTabCtrlBackground(
                        wxDC& dc,
                        wxWindow* wnd,
                        const wxRect& rect) override;

    void DrawTab(wxDC& dc,
                 wxWindow* wnd,
                 const wxRibbonPageTabInfo& tab) override;

    void DrawTabSeparator(
                        wxDC& dc,
                        wxWindow* wnd,
                        const wxRect& rect,
                        double visibility) override;

    void DrawPageBackground(
                        wxDC& dc,
                        wxWindow* wnd,
                        const wxRect& rect) override;

    void DrawScrollButton(
                        wxDC& dc,
                        wxWindow* wnd,
                        const wxRect& rect,
                        long style) override;

    void DrawPanelBackground(
                        wxDC& dc,
                        wxRibbonPanel* wnd,
                        const wxRect& rect) override;

    void DrawGalleryBackground(
                        wxDC& dc,
                        wxRibbonGallery* wnd,
                        const wxRect& rect) override;

    void DrawGalleryItemBackground(
                        wxDC& dc,
                        wxRibbonGallery* wnd,
                        const wxRect& rect,
                        wxRibbonGalleryItem* item) override;

    void DrawMinimisedPanel(
                        wxDC& dc,
                        wxRibbonPanel* wnd,
                        const wxRect& rect,
                        wxBitmap& bitmap) override;

    void DrawButtonBarBackground(
                        wxDC& dc,
                        wxWindow* wnd,
                        const wxRect& rect) override;

    void DrawButtonBarButton(
                        wxDC& dc,
                        wxWindow* wnd,
                        const wxRect& rect,
                        wxRibbonButtonKind kind,
                        long state,
                        const wxString& label,
                        const wxBitmap& bitmap_large,
                        const wxBitmap& bitmap_small) override;

    void DrawToolBarBackground(
                        wxDC& dc,
                        wxWindow* wnd,
                        const wxRect& rect) override;

    void DrawToolGroupBackground(
                        wxDC& dc,
                        wxWindow* wnd,
                        const wxRect& rect) override;

    void DrawTool(
                wxDC& dc,
                wxWindow* wnd,
                const wxRect& rect,
                const wxBitmap& bitmap,
                wxRibbonButtonKind kind,
                long state) override;

    void DrawToggleButton(
                        wxDC& dc,
                        wxRibbonBar* wnd,
                        const wxRect& rect,
                        wxRibbonDisplayMode mode) override;

    void DrawHelpButton(wxDC& dc,
                        wxRibbonBar* wnd,
                        const wxRect& rect) override;

    wxSize GetScrollButtonMinimumSize(
                        wxReadOnlyDC& dc,
                        wxWindow* wnd,
                        long style) override;

    wxSize GetPanelSize(
                        wxReadOnlyDC& dc,
                        const wxRibbonPanel* wnd,
                        wxSize client_size,
                        wxPoint* client_offset) override;

    wxSize GetPanelClientSize(
                        wxReadOnlyDC& dc,
                        const wxRibbonPanel* wnd,
                        wxSize size,
                        wxPoint* client_offset) override;

    wxRect GetPanelExtButtonArea(
                        wxReadOnlyDC& dc,
                        const wxRibbonPanel* wnd,
                        wxRect rect) override;

    wxSize GetGallerySize(
                        wxReadOnlyDC& dc,
                        const wxRibbonGallery* wnd,
                        wxSize client_size) override;

    wxSize GetGalleryClientSize(
                        wxReadOnlyDC& dc,
                        const wxRibbonGallery* wnd,
                        wxSize size,
                        wxPoint* client_offset,
                        wxRect* scroll_up_button,
                        wxRect* scroll_down_button,
                        wxRect* extension_button) override;

    wxRect GetPageBackgroundRedrawArea(
                        wxReadOnlyDC& dc,
                        const wxRibbonPage* wnd,
                        wxSize page_old_size,
                        wxSize page_new_size) override;

    bool GetButtonBarButtonSize(
                        wxReadOnlyDC& dc,
                        wxWindow* wnd,
                        wxRibbonButtonKind kind,
                        wxRibbonButtonBarButtonState size,
                        const wxString& label,
                        wxCoord text_min_width,
                        wxSize bitmap_size_large,
                        wxSize bitmap_size_small,
                        wxSize* button_size,
                        wxRect* normal_region,
                        wxRect* dropdown_region) override;

    wxCoord GetButtonBarButtonTextWidth(
                        wxReadOnlyDC& dc, const wxString& label,
                        wxRibbonButtonKind kind,
                        wxRibbonButtonBarButtonState size) override;

    wxSize GetMinimisedPanelMinimumSize(
                        wxReadOnlyDC& dc,
                        const wxRibbonPanel* wnd,
                        wxSize* desired_bitmap_size,
                        wxDirection* expanded_panel_direction) override;

    wxSize GetToolSize(
                        wxReadOnlyDC& dc,
                        wxWindow* wnd,
                        wxSize bitmap_size,
                        wxRibbonButtonKind kind,
                        bool is_first,
                        bool is_last,
                        wxRect* dropdown_region) override;

    wxRect GetBarToggleButtonArea(const wxRect& rect) override;

    wxRect GetRibbonHelpButtonArea(const wxRect& rect) override;

    bool DrawKeyTip(wxDC& dc,
                    wxWindow* wnd,
                    const wxRect& rect,
                    const wxString& keytip) override;

private:
    // Glyphs drawn by DrawGlyph().
    enum Glyph
    {
        Glyph_ChevronUp,
        Glyph_ChevronDown,
        Glyph_ChevronLeft,
        Glyph_ChevronRight,
        Glyph_More,
        Glyph_Launcher,
        Glyph_Pin,
        Glyph_Help,

        Glyph_Max
    };

    // Draw the glyph in a square of the given size (16 DIPs by default)
    // centred at the given point.
    void DrawGlyph(wxDC& dc, Glyph glyph, const wxPoint2DDouble& centre,
                   double size, const wxColour& colour);

    // Colours set by the application for one of the appearances.
    struct AppearanceColours
    {
        wxColour primary;
        wxColour secondary;
        wxColour tertiary;

        // Individual colours set with SetAppearanceColour(), by their IDs.
        std::map<int, wxColour> colours;
    };

    AppearanceColours& GetAppearanceColours(Appearance appearance)
    {
        return m_appearance_colours[static_cast<int>(appearance)];
    }

    const AppearanceColours& GetAppearanceColours(Appearance appearance) const
    {
        return m_appearance_colours[static_cast<int>(appearance)];
    }

    // Return the appearance used by the system, or by the application if it
    // was changed with wxApp::SetAppearance().
    static Appearance GetSystemAppearance();

    // Return true if the colours of the given appearance are currently used,
    // i.e. it is the system appearance and no high contrast theme is active.
    static bool IsCurrentAppearance(Appearance appearance);

    // Colour scheme used for the current system appearance: the one set with
    // SetAppearanceColourScheme(), with the default colours used for the
    // colours not set, or the system colours in high contrast mode.
    void GetMSOColourScheme(wxColour& primary,
                            wxColour& secondary,
                            wxColour& tertiary) const;

    // Draw one of the gallery scroll or extension buttons.
    void DrawGalleryGlyphButton(wxDC& dc, wxRect rect,
                                wxRibbonGalleryButtonState state,
                                Glyph glyph);

    // Return the rectangle of the page "card" for the given page rectangle.
    wxRect GetPageCardRect(wxReadOnlyDC& dc, const wxRect& page_rect) const;

    // Remember the DPI scale of the DC used for measuring the given window,
    // see GetMetric().
    void UpdateDPIScale(const wxReadOnlyDC& dc, const wxWindow* wnd);

    // Draw the page background under the given part of a window inside it.
    void FillPageBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect);

    // Compute the label row and extension button rectangles of a panel.
    void GetPanelLabelLayout(wxReadOnlyDC& dc, const wxRibbonPanel* wnd,
                             const wxRect& rect, wxRect* label_rect,
                             wxRect* ext_rect, bool* has_separator);

    // Return the extra space taken by the panel decorations.
    wxSize GetPanelDecorationSize(wxReadOnlyDC& dc,
                                  wxPoint* client_offset) const;

    // Draw the bitmap, label and dropdown chevron of a button bar button.
    void DrawButtonBarButtonContent(
                        wxDC& dc,
                        wxWindow* wnd,
                        const wxRect& rect,
                        wxRibbonButtonKind kind,
                        long state,
                        const wxString& label,
                        const wxBitmap& bitmap_large,
                        const wxBitmap& bitmap_small);

    // Size of the toggle and help buttons, updated in GetTabCtrlHeight().
    int m_bar_button_size;
    int m_tab_ctrl_height;

    // Scale factor used for converting the metrics from DIPs to pixels.
    double m_dpi_scale;

    // Whether the system accent colour is used in the default colour scheme.
    bool m_use_system_accent;

    // Colours set for the light and dark appearances, indexed by Appearance.
    AppearanceColours m_appearance_colours[2];

    // Colour of the outline of the highlighted elements, only used in high
    // contrast mode (invalid otherwise).
    wxColour m_highlight_border_colour;

    // Glyph bitmaps already rendered from SVG, never null.
    wxRibbonMSOGlyphCache* m_glyph_cache;

    wxDECLARE_NO_COPY_CLASS(wxRibbonMSOArtProvider);
};

#endif // wxUSE_RIBBON && wxHAS_SVG && wxUSE_GRAPHICS_CONTEXT

#endif // _WX_RIBBON_ART_MSO_H_
