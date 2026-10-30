///////////////////////////////////////////////////////////////////////////////
// Name:        src/ribbon/art_mso.cpp
// Purpose:     Microsoft Office 365 style art provider for ribbon interface
// Author:      wxWidgets team
// Created:     2026-10-30
// Copyright:   (c) wxWidgets team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"


#if wxUSE_RIBBON

#include "wx/bmpbndl.h"

#if !defined(wxHAS_SVG) || !wxUSE_GRAPHICS_CONTEXT
    #pragma message("wxRibbonMSOArtProvider is not available without SVG and graphics context support.")
#else

#include "wx/ribbon/art_mso.h"
#include "wx/ribbon/art_internal.h"
#include "wx/ribbon/bar.h"
#include "wx/ribbon/buttonbar.h"
#include "wx/ribbon/gallery.h"
#include "wx/ribbon/page.h"
#include "wx/ribbon/panel.h"
#include "wx/ribbon/toolbar.h"
#include "wx/graphics.h"

#ifndef WX_PRECOMP
    #include "wx/dc.h"
    #include "wx/log.h"
    #include "wx/settings.h"
#endif

#ifdef __WXMSW__
    #include "wx/msw/private/metrics.h"
    #include "wx/msw/registry.h"
#endif

#include <cmath>
#include <unordered_map>

namespace
{

// Size of the glyph images in DIPs, all their coordinates are in this range.
const int GLYPH_SIZE = 16;

// The horizontal and vertical lines of some glyphs are on the grid with this
// step, in the units of the glyph images.
const int GLYPH_GRID_STEP = 4;

// ----------------------------------------------------------------------------
// Glyph images: "{colour}" and "{width}" are replaced with the colour and the
// stroke width and "{offset}" with the shift needed to align the strokes to
// the pixel grid before rasterizing them.
// ----------------------------------------------------------------------------

#define MSO_GLYPH_SVG(body) \
    "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' " \
        "viewBox='0 0 16 16'>" \
    "<g fill='none' stroke='{colour}' stroke-width='{width}' " \
        "stroke-linecap='round' stroke-linejoin='round' " \
        "transform='translate({offset},{offset})'>" \
    body \
    "</g></svg>"

const char* const GLYPH_SVGS[] =
{
    // Glyph_ChevronUp
    MSO_GLYPH_SVG("<polyline points='4,10 8,6 12,10'/>"),

    // Glyph_ChevronDown
    MSO_GLYPH_SVG("<polyline points='4,6 8,10 12,6'/>"),

    // Glyph_ChevronLeft
    MSO_GLYPH_SVG("<polyline points='10,4 6,8 10,12'/>"),

    // Glyph_ChevronRight
    MSO_GLYPH_SVG("<polyline points='6,4 10,8 6,12'/>"),

    // Glyph_More: a small chevron with a wider bar above it.
    MSO_GLYPH_SVG("<path d='M4,5 H12'/>"
                  "<polyline points='5.25,7.5 8,10.25 10.75,7.5'/>"),

    // Glyph_Launcher: a corner bracket with an arrow pointing to the
    // opposite corner.
    MSO_GLYPH_SVG("<polyline points='4,11 4,4 11,4'/>"
                  "<path d='M7,7 L12,12'/>"
                  "<polyline points='7,12 12,12 12,7'/>"),

    // Glyph_Pin: a push pin.
    MSO_GLYPH_SVG("<polygon points='6,4 6,8 4,9.5 12,9.5 10,8 10,4'/>"
                  "<path d='M8,9.5 V13.25'/>"),

    // Glyph_Help: a question mark in a circle.
    MSO_GLYPH_SVG("<circle cx='8' cy='8' r='7'/>"
                  "<path d='M6.2,6.6 A1.8,1.8 0 1 1 8.9,8.1 "
                           "C8.3,8.5 8,8.9 8,9.6 V9.9'/>"
                  "<circle cx='8' cy='11.75' r='0.4' fill='{colour}'/>"),
};

#undef MSO_GLYPH_SVG

// ----------------------------------------------------------------------------
// Colour helpers
// ----------------------------------------------------------------------------

// Mix two colours, weight is the proportion of the second one.
wxColour Blend(const wxColour& c1, const wxColour& c2, double weight)
{
    const auto mix = [weight](unsigned char a, unsigned char b)
    {
        return static_cast<unsigned char>(a + (b - a) * weight + 0.5);
    };

    return wxColour(mix(c1.Red(), c2.Red()),
                    mix(c1.Green(), c2.Green()),
                    mix(c1.Blue(), c2.Blue()));
}

// Colours with luminance below this value are considered to be dark.
const double DARK_COLOUR_LUMINANCE = 0.5;

bool IsDarkColour(const wxColour& colour)
{
    return colour.GetLuminance() < DARK_COLOUR_LUMINANCE;
}

// Return the absolute difference between the luminances of two colours.
double GetLuminanceDifference(const wxColour& c1, const wxColour& c2)
{
    return std::fabs(c1.GetLuminance() - c2.GetLuminance());
}

// ----------------------------------------------------------------------------
// Colour scheme constants
// ----------------------------------------------------------------------------

// Colour which is different for the light and dark appearances, both are
// given as 0xRRGGBB.
struct SchemeColour
{
    unsigned long light;
    unsigned long dark;

    wxColour For(bool isDark) const
    {
        const unsigned long rgb = isDark ? dark : light;
        return wxColour(static_cast<unsigned char>(rgb >> 16),
                        static_cast<unsigned char>(rgb >> 8),
                        static_cast<unsigned char>(rgb));
    }
};

// The default colours, as used by Microsoft Office.
const SchemeColour DEFAULT_BACKGROUND_COLOUR = { 0xf3f3f3, 0x1f1f1f };
const SchemeColour DEFAULT_ACCENT_COLOUR = { 0x185abd, 0x8ab4f8 };
const SchemeColour DEFAULT_TEXT_COLOUR = { 0x242424, 0xffffff };

// Minimal luminance differences from the page card colour for the text and
// accent colours of the scheme to be used: the default colours are used
// instead of the less contrasting ones.
const double MIN_TEXT_LUMINANCE_DIFFERENCE = 0.4;
const double MIN_ACCENT_LUMINANCE_DIFFERENCE = 0.2;

// The system accent colour is compared with the background, which is less
// contrasting than the card, so require a slightly bigger difference for it.
const double MIN_SYSTEM_ACCENT_LUMINANCE_DIFFERENCE = 0.25;

// Proportion of the second colour mixed into the first one by Blend(), which
// is different for the light and dark appearances.
struct MixRatio
{
    double light;
    double dark;

    double For(bool isDark) const { return isDark ? dark : light; }
};

// White mixed into the background: the page card colour.
const MixRatio MIX_CARD = { 0.9, 0.045 };

// Text colour mixed into the card colour: the states of buttons, tools and
// other elements on the page and the lines drawn on it.
const MixRatio MIX_HOVER = { 0.08, 0.12 };
const MixRatio MIX_PRESSED = { 0.16, 0.20 };
const MixRatio MIX_TOGGLED = { 0.12, 0.16 };
const MixRatio MIX_TOGGLED_BORDER = { 0.28, 0.32 };
const MixRatio MIX_SEPARATOR = { 0.14, 0.20 };
const MixRatio MIX_GALLERY_BORDER = { 0.21, 0.28 };
const MixRatio MIX_GALLERY_STRONG_BORDER = { 0.72, 0.60 };

// Card colour mixed into the text colour: disabled text.
const MixRatio MIX_DISABLED_TEXT = { 0.55, 0.55 };

// Text colour mixed into the background: the tabs and the page border.
const MixRatio MIX_TAB_HOVER = { 0.07, 0.10 };
const MixRatio MIX_TAB_HOVER_UNDERLINE = { 0.30, 0.40 };
const MixRatio MIX_TAB_SEPARATOR = { 0.25, 0.25 };
const MixRatio MIX_PAGE_BORDER = { 0.10, 0.12 };

// Mix of two state colours used for the intermediate states, e.g. a hovered
// toggled button or the part of a hybrid button not under the mouse.
const double MIX_HALFWAY = 0.5;

// Return true if the colour is sufficiently different from the background to
// be used as the accent colour on it.
bool IsUsableAccent(const wxColour& colour, const wxColour& background)
{
    return GetLuminanceDifference(colour, background) >=
                MIN_SYSTEM_ACCENT_LUMINANCE_DIFFERENCE;
}

// Get the accent colour chosen by the user in the system settings, in the
// variant appropriate for using on the given background.
bool GetSystemAccentColour(const wxColour& background, wxColour& colour)
{
    // Use the shade if it is usable on the background.
    const auto use = [&background, &colour](const wxColour& shade)
    {
        if ( !shade.IsOk() || !IsUsableAccent(shade, background) )
            return false;

        colour = shade;
        return true;
    };

#if defined(__WXMSW__) && wxUSE_REGKEY
    // There is no Win32 API for getting the accent colour, but it is stored
    // in the registry. Don't log errors if it isn't there, e.g. under Wine.
    wxLogNull noLog;

    // The palette is not documented, but contains 8 RGBA colours: 3 lighter
    // shades of the accent colour (from the lightest), the accent colour
    // itself, 3 darker shades of it and an unused one. Like Windows itself,
    // prefer the second lighter shade on dark background and the first darker
    // one on light background, but use a more contrasting one if necessary.
    const size_t ACCENT_PALETTE_SHADES = 8;
    const size_t ACCENT_PALETTE_SHADE_SIZE = 4;

    wxRegKey accent(wxRegKey::HKCU,
                    "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent");
    wxMemoryBuffer palette;
    if ( accent.Exists() && accent.HasValue("AccentPalette") &&
            accent.QueryValue("AccentPalette", palette) &&
                palette.GetDataLen() >= ACCENT_PALETTE_SHADES * ACCENT_PALETTE_SHADE_SIZE )
    {
        static const int shadesForDark[] = { 1, 0 };
        static const int shadesForLight[] = { 4, 5, 6 };

        const bool dark = IsDarkColour(background);
        const int* const shades = dark ? shadesForDark : shadesForLight;
        const size_t count = dark ? WXSIZEOF(shadesForDark)
                                  : WXSIZEOF(shadesForLight);

        const unsigned char* const
            data = static_cast<const unsigned char*>(palette.GetData());
        for ( size_t n = 0; n < count; ++n )
        {
            const unsigned char* const rgba = data + shades[n] * ACCENT_PALETTE_SHADE_SIZE;
            if ( use(wxColour(rgba[0], rgba[1], rgba[2])) )
                return true;
        }
    }

    // Fall back to the accent colour itself, stored as 0xAABBGGRR, if there
    // is no palette or none of its shades is usable.
    wxRegKey dwm(wxRegKey::HKCU, "Software\\Microsoft\\Windows\\DWM");
    long value = 0;
    if ( dwm.Exists() && dwm.HasValue("AccentColor") &&
            dwm.QueryValue("AccentColor", &value) )
    {
        return use(wxColour(value & 0xff, (value >> 8) & 0xff, (value >> 16) & 0xff));
    }

    return false;
#else // !__WXMSW__
    // The other platforms don't provide the accent colour directly, but the
    // selection colour normally follows it.
    return use(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
#endif // __WXMSW__
}

// Return true if the system uses a high contrast theme, in which case only the
// system colours should be used.
bool IsHighContrast()
{
#ifdef __WXMSW__
    return wxMSWImpl::IsHighContrast();
#else
    return false;
#endif
}

// ----------------------------------------------------------------------------
// Painter: draws antialiased shapes using wxGraphicsContext
// ----------------------------------------------------------------------------

// Notice that the graphics context created for the DCs other than wxGCDC may
// only take into account the clipping region in effect when it is created, so
// the DC must be clipped before creating the painter. And as mixing drawing
// using the graphics context and directly on the DC is not recommended, the
// painter should be destroyed before drawing on the DC directly.

class MSOPainter
{
public:
    // If highlight_border is valid, it is used for outlining the highlights
    // drawn by Highlight() (this is used in high contrast mode, in which the
    // highlights are not distinguishable by their fill colour).
    explicit MSOPainter(wxDC& dc, const wxColour& highlight_border = wxColour())
        : m_dc(dc),
          m_highlightBorder(highlight_border),
          m_gc(dc.GetGraphicsContext())
    {
        // Only wxGCDC uses a graphics context, create one for the other DCs.
        if ( !m_gc )
        {
            m_gc = wxGraphicsContext::CreateFromUnknownDC(dc);
            m_ownsGC = true;
        }
    }

    ~MSOPainter()
    {
        if ( m_ownsGC )
        {
            delete m_gc;
        }
        else if ( m_gc )
        {
            // The context belongs to wxGCDC, which doesn't expect its pen and
            // brush to be changed behind its back, so restore them.
            m_gc->SetPen(m_dc.GetPen());
            m_gc->SetBrush(m_dc.GetBrush());
        }
    }

    // Draw a highlight (e.g. of a hovered or pressed element) of the given
    // colour, outlined in high contrast mode.
    void Highlight(const wxRect& rect, double radius, const wxColour& fill)
    {
        RoundedRect(rect, radius, fill, m_highlightBorder);
    }

    // Fill and/or outline a rounded rectangle, pass invalid colours to skip.
    void RoundedRect(const wxRect& rect, double radius,
                     const wxColour& fill,
                     const wxColour& border = wxColour())
    {
        if ( rect.width <= 0 || rect.height <= 0 )
            return;

        if ( m_gc )
        {
            if ( fill.IsOk() )
            {
                m_gc->SetPen(*wxTRANSPARENT_PEN);
                m_gc->SetBrush(wxBrush(fill));
                if ( border.IsOk() )
                {
                    // Don't let the antialiased edge of the fill extend
                    // beyond the border line, whose centre is at 0.5.
                    m_gc->DrawRoundedRectangle(rect.x + 0.5, rect.y + 0.5,
                                               rect.width - 1, rect.height - 1,
                                               radius);
                }
                else
                {
                    m_gc->DrawRoundedRectangle(rect.x, rect.y,
                                               rect.width, rect.height, radius);
                }
            }
            if ( border.IsOk() )
            {
                // Put the line on the pixel centres, unless the context
                // already does it on its own.
                const double offset = m_gc->OffsetEnabled() ? 0.0 : 0.5;
                m_gc->SetBrush(*wxTRANSPARENT_BRUSH);
                m_gc->SetPen(wxPen(border));
                m_gc->DrawRoundedRectangle(rect.x + offset, rect.y + offset,
                                           rect.width - 1, rect.height - 1,
                                           radius);
            }
            return;
        }

        // Creating the graphics context fails for the DCs not supported by
        // it, draw without antialiasing on them.
        wxDCPenChanger pen(m_dc, border.IsOk() ? wxPen(border)
                                               : *wxTRANSPARENT_PEN);
        wxDCBrushChanger brush(m_dc, fill.IsOk() ? wxBrush(fill)
                                                 : *wxTRANSPARENT_BRUSH);
        m_dc.DrawRoundedRectangle(rect, radius);
    }

private:
    wxDC& m_dc;
    const wxColour m_highlightBorder;
    wxGraphicsContext* m_gc;
    bool m_ownsGC = false;

    wxDECLARE_NO_COPY_CLASS(MSOPainter);
};

// ----------------------------------------------------------------------------
// Sizes, all in DIPs unless stated otherwise
// ----------------------------------------------------------------------------

// Size of the toggle and help buttons in the tab row.
const int BAR_BUTTON_SIZE = 24;

// Tabs: the space above the label or the icon, the horizontal space around
// the label in the ideal, small and minimal tab widths, the minimal label
// width and the gap between the icon and the label (normally and in the
// minimal tab width).
const int TAB_TOP_PADDING = 3;
const int TAB_IDEAL_HORZ_PADDING = 20;
const int TAB_SMALL_HORZ_PADDING = 12;
const int TAB_MIN_HORZ_PADDING = 6;
const int TAB_MIN_LABEL_WIDTH = 25;
const int TAB_ICON_GAP = 4;
const int TAB_MIN_ICON_GAP = 2;

// Tabs: the horizontal margin of their contents, the gap between the label
// or the icon and the underline of the active and hovered tabs, its height,
// the gap between it and the bottom of the tab, i.e. the page, and how much
// it extends beyond the label on each side when the active tab is hovered.
const int TAB_CONTENT_MARGIN = 2;
const int TAB_UNDERLINE_TOP_GAP = 1;
const int TAB_UNDERLINE_HEIGHT = 2;
const int TAB_UNDERLINE_BOTTOM_GAP = 2;
const int TAB_UNDERLINE_HOVER_EXTENSION = 9;

// Height of the tab control, in pixels, when the tabs are not shown.
const int TAB_CTRL_HEIGHT_WITHOUT_TABS = 2;

// Inset of the page "card" from the page edges and its corner radius.
const int PAGE_CARD_INSET = 3;
const int PAGE_CARD_RADIUS = 6;

// Corner radius of the highlights and of the gallery border, and the smaller
// one used for the highlights of the small panel and gallery buttons.
const int CORNER_RADIUS = 4;
const int SMALL_CORNER_RADIUS = 2;

// Scroll buttons: their minimal size, the inset of their background and the
// vertical inset of the highlight of the buttons scrolling the tabs.
const int SCROLL_BUTTON_SIZE = 14;
const int SCROLL_BUTTON_INSET = 1;
const int SCROLL_BUTTON_TAB_VERT_INSET = 4;

// Office uses smaller chevrons for dropdowns and collapsed panels.
const int SMALL_GLYPH_SIZE = 10;

// Width of the lines of the glyphs.
const int GLYPH_LINE_WIDTH = 1;

// Panels: the padding around their contents, the gap between the contents
// and the label, the size of the extension button and the distance of the
// ends of the separator line from the panel edges, for the horizontal and
// vertical ribbon layouts.
const int PANEL_HORZ_PADDING = 4;
const int PANEL_TOP_PADDING = 2;
const int PANEL_LABEL_GAP = 2;
const int PANEL_BOTTOM_PADDING = 2;
const int PANEL_EXT_BUTTON_SIZE = 14;
const int PANEL_SEPARATOR_INSET = 4;
const int PANEL_SEPARATOR_INSET_VERTICAL = 6;

// Panels: the padding removed by RemovePanelPadding() on each side and the
// width of the separator line, both in pixels.
const int PANEL_PADDING_PIXELS = 1;
const int PANEL_SEPARATOR_PIXELS = 1;

// Collapsed panels: the size of the icon slot, the inset of their highlight,
// the padding of the contents inside it, the resulting padding of the
// contents from the panel edges and the gap between the label and the
// chevron in the vertical layout.
const int MINIMISED_PANEL_ICON_SIZE = 32;
const int MINIMISED_PANEL_HIGHLIGHT_INSET = 2;
const int MINIMISED_PANEL_CONTENT_PADDING = 4;
const int MINIMISED_PANEL_PADDING = MINIMISED_PANEL_HIGHLIGHT_INSET +
                                    MINIMISED_PANEL_CONTENT_PADDING;
const int MINIMISED_PANEL_CHEVRON_GAP = 4;

// Galleries: the width of the column with the scroll and extension buttons,
// the padding around the items, the inset of the button highlights and the
// maximal width of the frame of the hovered or selected item.
const int GALLERY_BUTTON_WIDTH = 15;
const int GALLERY_PADDING = 2;
const int GALLERY_BUTTON_INSET = 2;
const int GALLERY_ITEM_FRAME_WIDTH = 3;

// Width of the dropdown part of small and medium buttons and tools and the
// extra width needed on a label line ending with a dropdown chevron.
const int DROPDOWN_WIDTH = 12;
const int CHEVRON_EXTRA_WIDTH = 12;

// Large buttons: the padding, the gap between the icon and the label and the
// offset of the chevron following the second label line.
const int LARGE_BUTTON_TOP_PADDING = 4;
const int LARGE_BUTTON_ICON_GAP = 3;
const int LARGE_BUTTON_SIDE_PADDING = 5;
const int LARGE_BUTTON_BOTTOM_PADDING = 3;
const int LARGE_BUTTON_CHEVRON_HORZ_OFFSET = 2;
const int LARGE_BUTTON_CHEVRON_VERT_OFFSET = 1;

// Padding of small and medium buttons and tools.
const int SMALL_BUTTON_PADDING = 4;

// Distance of the ends of the line separating the parts of hybrid buttons and
// tools from the edges of their highlight.
const int SPLIT_LINE_INSET = 2;

// Tools: the inset of their highlight and the distance of the ends of the
// line separating the groups from the edges of the row.
const int TOOL_HIGHLIGHT_INSET = 1;
const int TOOL_GROUP_SEPARATOR_INSET = 3;

// Key tip badges: the padding around the text, the gap separating them from
// their target or from the badge of the main part of hybrid buttons and the
// corner radius.
const int KEY_TIP_HORZ_PADDING = 4;
const int KEY_TIP_VERT_PADDING = 1;
const int KEY_TIP_GAP = 2;
const int KEY_TIP_RADIUS = 3;

// Sizes of the fonts of the tab labels and the panel labels, relative to the
// default tab font and the button label font respectively. Office uses 8pt
// for the panel labels with the default 9pt font: notice that a slightly
// smaller size would give the same font size in pixels at 125% DPI, but a
// size 1px smaller at 100%, due to rounding, which is quite noticeable.
const float TAB_LABEL_FONT_SCALE = 1.1f;
const float PANEL_LABEL_FONT_SCALE = 8.0f / 9.0f;

// Return the width of the glyph lines in logical units. As FromDIP() only
// works with integers, compute it with a precision of a tenth of a pixel.
double GetLineWidth(const wxReadOnlyDC& dc)
{
    const int precision = 10;
    return wxMax(1.0, dc.FromDIP(GLYPH_LINE_WIDTH * precision) /
                        static_cast<double>(precision));
}

// Fill the rectangle with the given colour.
void FillRect(wxDC& dc, const wxRect& rect, const wxColour& colour)
{
    wxDCPenChanger pen(dc, *wxTRANSPARENT_PEN);
    wxDCBrushChanger brush(dc, wxBrush(colour));
    dc.DrawRectangle(rect);
}

// Return true if this panel is the last visible one on its page.
bool IsLastPanel(const wxRibbonPanel* panel)
{
    const wxWindow* const parent = panel->GetParent();
    if ( !wxDynamicCast(parent, wxRibbonPage) )
        return true;

    const wxWindowList& children = parent->GetChildren();
    for ( wxWindowList::compatibility_iterator node = children.GetLast();
          node;
          node = node->GetPrevious() )
    {
        const wxRibbonPanel* const p = wxDynamicCast(node->GetData(), wxRibbonPanel);
        if ( p && p->IsShown() )
            return p == panel;
    }

    return true;
}

// Return true if the character is a combining mark, which must not be
// separated from the preceding character. Only the blocks of combining marks
// are checked, which covers the commonly used ones.
bool IsCombiningMark(wxUniChar ch)
{
    static const struct
    {
        wxUint32 first, last;
    } combiningBlocks[] =
    {
        { 0x0300, 0x036f }, // Combining Diacritical Marks
        { 0x1ab0, 0x1aff }, // Combining Diacritical Marks Extended
        { 0x1dc0, 0x1dff }, // Combining Diacritical Marks Supplement
        { 0x20d0, 0x20ff }, // Combining Diacritical Marks for Symbols
        { 0xfe20, 0xfe2f }, // Combining Half Marks
    };

    const wxUint32 value = ch.GetValue();
    for ( const auto& block : combiningBlocks )
    {
        if ( value >= block.first && value <= block.last )
            return true;
    }

    return false;
}

// How FitLabel() shortens the labels which don't fit.
enum class Shorten
{
    WithEllipsis,   // Replace the end of the label with an ellipsis.
    Truncate        // Just cut off the characters which don't fit.
};

// Return the label shortened to fit into the given width.
wxString FitLabel(wxReadOnlyDC& dc, const wxString& label, int width,
                  Shorten how = Shorten::WithEllipsis)
{
    if ( label.empty() || dc.GetTextExtent(label).GetWidth() <= width )
        return label;

    const wxString ellipsis = wxString::FromUTF8("\xe2\x80\xa6");
    const auto shorten = [&label, &ellipsis, how](size_t len)
    {
        // Don't separate the characters from the combining marks following
        // them nor cut a surrogate pair in half when using UTF-16.
        while ( len > 0 && len < label.length() &&
                    (IsCombiningMark(label[len]) ||
                        (label[len] >= 0xdc00 && label[len] <= 0xdfff)) )
            --len;

        wxString shorter = label.Left(len);
        if ( how == Shorten::WithEllipsis )
        {
            shorter.Trim();
            shorter += ellipsis;
        }
        return shorter;
    };

    // Find the longest prefix fitting (with the ellipsis, if any).
    size_t lo = 0,
           hi = label.length() - 1;
    while ( lo < hi )
    {
        const size_t mid = (lo + hi + 1) / 2;
        if ( dc.GetTextExtent(shorten(mid)).GetWidth() <= width )
            lo = mid;
        else
            hi = mid - 1;
    }

    return lo > 0 ? shorten(lo) : wxString();
}

// Text used for measuring the height of a line of text: an accented capital
// letter and a letter with a descender.
const wchar_t* const LINE_HEIGHT_SAMPLE = L"\u010Cg";

// Return the size of the text in the given font, leaving the DC font unchanged.
//
// Notice that the font is temporarily selected into the DC instead of being
// passed to GetTextExtent(): wxMSW adjusts the font set with SetFont() to the
// DPI of the window, but not the one passed to GetTextExtent(), so the text
// would be measured using the size of the font at the DPI it was created for,
// i.e. the system DPI, which is wrong for the windows on the monitors with a
// different DPI.
wxSize GetTextSize(wxReadOnlyDC& dc, const wxString& text, const wxFont& font)
{
    const wxFont old_font = dc.GetFont();
    dc.SetFont(font);
    const wxSize size = dc.GetTextExtent(text);
    if ( old_font.IsOk() )
        dc.SetFont(old_font);
    return size;
}

// Height of a single line of text in the given font.
int GetLineHeight(wxReadOnlyDC& dc, const wxFont& font)
{
    return GetTextSize(dc, LINE_HEIGHT_SAMPLE, font).GetHeight();
}

// Height of the part of large buttons below their icon: two lines are always
// reserved for the label to give all buttons a consistent height.
int GetLargeButtonLabelAreaHeight(wxReadOnlyDC& dc, const wxFont& font)
{
    return 2 * GetLineHeight(dc, font) + dc.FromDIP(LARGE_BUTTON_BOTTOM_PADDING);
}

// Return true if the window is a panel expanded in a popup or inside one.
bool IsInExpandedPanel(const wxWindow* wnd)
{
    for ( const wxWindow* win = wnd; win; win = win->GetParent() )
    {
        // GetExpandedDummy() is not const, even though it doesn't modify the
        // panel.
        wxRibbonPanel* const panel =
            const_cast<wxRibbonPanel*>(wxDynamicCast(win, wxRibbonPanel));
        if ( panel && panel->GetExpandedDummy() != nullptr )
            return true;

        if ( win->IsTopLevel() )
            break;
    }

    return false;
}

// Return the part of the given key tip target rectangle in which to show its
// badge: for the hybrid buttons and tools with a separate key tip for their
// dropdown part, this is the rectangle of the whole item without that part,
// so that the badges of both parts don't overlap.
wxRect GetKeyTipArea(wxWindow* wnd, const wxRect& rect)
{
    wxRect dropdown;
    if ( wxRibbonButtonBar* const bb = wxDynamicCast(wnd, wxRibbonButtonBar) )
    {
        for ( size_t n = 0; n < bb->GetButtonCount(); ++n )
        {
            const int id = bb->GetItemId(bb->GetItem(n));
            if ( bb->GetItemRect(id) == rect &&
                    !bb->GetDropdownKeyTip(id).empty() )
            {
                dropdown = bb->GetItemDropdownRect(id);
                break;
            }
        }
    }
    else if ( wxRibbonToolBar* const tb = wxDynamicCast(wnd, wxRibbonToolBar) )
    {
        for ( size_t n = 0; n < tb->GetToolCount(); ++n )
        {
            const wxRibbonToolBarToolBase* const tool = tb->GetToolByPos(n);
            if ( !tool )
                continue;

            const int id = tb->GetToolId(tool);
            if ( tb->GetToolRect(id) == rect &&
                    !tb->GetDropdownKeyTip(id).empty() )
            {
                dropdown = tb->GetToolDropdownRect(id);
                break;
            }
        }
    }

    wxRect area = rect;
    if ( !dropdown.IsEmpty() && dropdown != rect )
    {
        // The dropdown part is either below the main one (large buttons) or
        // on its right (all the others).
        if ( dropdown.y > rect.y )
            area.height = dropdown.y - rect.y;
        else if ( dropdown.x > rect.x )
            area.width = dropdown.x - rect.x;
    }

    return area;
}

} // anonymous namespace

// Glyph bitmaps already rendered from SVG, indexed by the glyph, colour, size
// and line width packed into a single key.
//
// This is a class and not a typedef because it is only forward declared in
// the public header, to avoid including <unordered_map> from it, and only a
// class can be forward declared. Deriving from the standard container is safe
// as it is never deleted via a pointer to the base class.
class wxRibbonMSOGlyphCache : public std::unordered_map<wxUint64, wxBitmap>
{
};

// ============================================================================
// wxRibbonMSOArtProvider implementation
// ============================================================================

/* static */
wxRibbonMSOArtProvider::Appearance wxRibbonMSOArtProvider::GetSystemAppearance()
{
    return wxSystemSettings::GetAppearance().IsDark() ? Appearance::Dark
                                                      : Appearance::Light;
}

/* static */
bool wxRibbonMSOArtProvider::IsCurrentAppearance(Appearance appearance)
{
    return !IsHighContrast() && appearance == GetSystemAppearance();
}

void wxRibbonMSOArtProvider::GetMSOColourScheme(wxColour& primary,
                                                wxColour& secondary,
                                                wxColour& tertiary) const
{
    if ( IsHighContrast() )
    {
        primary = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
        secondary = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
        tertiary = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
        return;
    }

    const Appearance appearance = GetSystemAppearance();
    const AppearanceColours& custom = GetAppearanceColours(appearance);
    const bool dark = appearance == Appearance::Dark;

    primary = custom.primary.IsOk() ? custom.primary
                                    : DEFAULT_BACKGROUND_COLOUR.For(dark);

    // The default text and accent colours must suit the background, which
    // may be light even for the dark appearance if it was customized.
    const bool dark_background = IsDarkColour(primary);

    tertiary = custom.tertiary.IsOk() ? custom.tertiary
                                      : DEFAULT_TEXT_COLOUR.For(dark_background);

    if ( custom.secondary.IsOk() )
    {
        secondary = custom.secondary;
    }
    else
    {
        secondary = DEFAULT_ACCENT_COLOUR.For(dark_background);
        if ( m_use_system_accent )
            GetSystemAccentColour(primary, secondary);
    }
}

wxRibbonMSOArtProvider::wxRibbonMSOArtProvider()
    : wxRibbonMSWFlatArtProvider()
{
    m_tab_label_font = m_tab_label_font.Scaled(TAB_LABEL_FONT_SCALE);

    // Panel (group) labels use a smaller font than the buttons in Office.
    m_panel_label_font = m_button_bar_label_font.Scaled(PANEL_LABEL_FONT_SCALE);

    m_tab_separation_size = 4;
    m_page_border_left = 6;
    m_page_border_top = 1;
    m_page_border_right = 6;
    m_page_border_bottom = 5;
    m_panel_x_separation_size = 0;
    m_panel_y_separation_size = 0;
    m_tool_group_separation_size = 7;

    m_glyph_cache = new wxRibbonMSOGlyphCache;
    m_bar_button_size = BAR_BUTTON_SIZE;
    m_tab_ctrl_height = 0;
    m_dpi_scale = 1.0;
    m_use_system_accent = true;

    wxColour primary, secondary, tertiary;
    GetMSOColourScheme(primary, secondary, tertiary);
    wxRibbonMSOArtProvider::SetColourScheme(primary, secondary, tertiary);
}

wxRibbonMSOArtProvider::~wxRibbonMSOArtProvider()
{
    delete m_glyph_cache;
}

wxRibbonArtProvider* wxRibbonMSOArtProvider::Clone() const
{
    wxRibbonMSOArtProvider *copy = new wxRibbonMSOArtProvider;

    // Set the scheme first to initialize all the colours not copied by
    // CloneTo(), which then copies any individually customized ones.
    copy->SetColourScheme(m_primary_scheme_colour, m_secondary_scheme_colour,
                          m_tertiary_scheme_colour);
    CloneTo(copy);

    copy->m_tool_group_separation_size = m_tool_group_separation_size;
    copy->m_bar_button_size = m_bar_button_size;
    copy->m_tab_ctrl_height = m_tab_ctrl_height;
    copy->m_dpi_scale = m_dpi_scale;
    copy->m_use_system_accent = m_use_system_accent;
    copy->m_appearance_colours[0] = m_appearance_colours[0];
    copy->m_appearance_colours[1] = m_appearance_colours[1];

    return copy;
}

void wxRibbonMSOArtProvider::UpdateColoursFromSystem()
{
    wxColour primary, secondary, tertiary;
    GetMSOColourScheme(primary, secondary, tertiary);
    SetColourScheme(primary, secondary, tertiary);

    const Appearance appearance = GetSystemAppearance();
    if ( IsCurrentAppearance(appearance) )
    {
        for ( const auto& kv : GetAppearanceColours(appearance).colours )
            SetColour(kv.first, kv.second);
    }
}

void wxRibbonMSOArtProvider::UseSystemAccentColour(bool use)
{
    m_use_system_accent = use;

    UpdateColoursFromSystem();
}

void wxRibbonMSOArtProvider::SetAppearanceColourScheme(Appearance appearance,
                                                       const wxColour& primary,
                                                       const wxColour& secondary,
                                                       const wxColour& tertiary)
{
    AppearanceColours& custom = GetAppearanceColours(appearance);
    custom.primary = primary;
    custom.secondary = secondary;
    custom.tertiary = tertiary;

    if ( IsCurrentAppearance(appearance) )
        UpdateColoursFromSystem();
}

void wxRibbonMSOArtProvider::GetAppearanceColourScheme(Appearance appearance,
                                                       wxColour* primary,
                                                       wxColour* secondary,
                                                       wxColour* tertiary) const
{
    const AppearanceColours& custom = GetAppearanceColours(appearance);
    if ( primary )
        *primary = custom.primary;
    if ( secondary )
        *secondary = custom.secondary;
    if ( tertiary )
        *tertiary = custom.tertiary;
}

void wxRibbonMSOArtProvider::SetAppearanceColour(Appearance appearance,
                                                 int id,
                                                 const wxColour& colour)
{
    auto& colours = GetAppearanceColours(appearance).colours;

    if ( colour.IsOk() )
    {
        colours[id] = colour;

        if ( IsCurrentAppearance(appearance) )
            SetColour(id, colour);
    }
    else if ( colours.erase(id) && IsCurrentAppearance(appearance) )
    {
        // Recompute the colour from the scheme.
        UpdateColoursFromSystem();
    }
}

wxColour
wxRibbonMSOArtProvider::GetAppearanceColour(Appearance appearance, int id) const
{
    const auto& colours = GetAppearanceColours(appearance).colours;
    const auto it = colours.find(id);
    return it != colours.end() ? it->second : wxColour();
}

void wxRibbonMSOArtProvider::ResetAppearanceColours(Appearance appearance)
{
    GetAppearanceColours(appearance) = AppearanceColours();

    if ( IsCurrentAppearance(appearance) )
        UpdateColoursFromSystem();
}

// ----------------------------------------------------------------------------
// Metrics
// ----------------------------------------------------------------------------

namespace
{

// Return true for the metrics which are sizes and so depend on the DPI.
bool IsSizeMetric(int id)
{
    switch ( id )
    {
        case wxRIBBON_ART_TAB_SEPARATION_SIZE:
        case wxRIBBON_ART_PAGE_BORDER_LEFT_SIZE:
        case wxRIBBON_ART_PAGE_BORDER_TOP_SIZE:
        case wxRIBBON_ART_PAGE_BORDER_RIGHT_SIZE:
        case wxRIBBON_ART_PAGE_BORDER_BOTTOM_SIZE:
        case wxRIBBON_ART_PANEL_X_SEPARATION_SIZE:
        case wxRIBBON_ART_PANEL_Y_SEPARATION_SIZE:
        case wxRIBBON_ART_TOOL_GROUP_SEPARATION_SIZE:
        case wxRIBBON_ART_GALLERY_BITMAP_PADDING_LEFT_SIZE:
        case wxRIBBON_ART_GALLERY_BITMAP_PADDING_RIGHT_SIZE:
        case wxRIBBON_ART_GALLERY_BITMAP_PADDING_TOP_SIZE:
        case wxRIBBON_ART_GALLERY_BITMAP_PADDING_BOTTOM_SIZE:
            return true;
    }

    return false;
}

} // anonymous namespace

void wxRibbonMSOArtProvider::UpdateDPIScale(const wxReadOnlyDC& dc,
                                            const wxWindow* wnd)
{
    // GetMetric() doesn't have any window to use for the DPI scaling, so
    // remember the scale of the window being laid out: the metrics are only
    // used by the ribbon controls when laying out the same window hierarchy.
    //
    // The panels expanded in a popup share the provider with the ribbon bar,
    // but are only shown temporarily, so don't let them change the scale used
    // by the bar if the popup is on a display with a different DPI.
    if ( IsInExpandedPanel(wnd) )
        return;

    // As FromDIP() only works with integers, use a big value for precision.
    const int precision = 1000;
    const double scale = dc.FromDIP(precision) / static_cast<double>(precision);
    if ( scale != m_dpi_scale )
    {
        m_dpi_scale = scale;

        // The glyphs of the old size are not going to be used any more.
        m_glyph_cache->clear();
    }
}

int wxRibbonMSOArtProvider::GetMetric(int id) const
{
    const int value = wxRibbonMSWFlatArtProvider::GetMetric(id);
    return IsSizeMetric(id) ? wxRound(value * m_dpi_scale) : value;
}

void wxRibbonMSOArtProvider::SetMetric(int id, int new_val)
{
    if ( IsSizeMetric(id) )
        new_val = wxRound(new_val / m_dpi_scale);

    wxRibbonMSWFlatArtProvider::SetMetric(id, new_val);
}

// ----------------------------------------------------------------------------
// Glyphs
// ----------------------------------------------------------------------------

void wxRibbonMSOArtProvider::DrawGlyph(wxDC& dc,
                                       Glyph glyph,
                                       const wxPoint2DDouble& centre,
                                       double size,
                                       const wxColour& colour)
{
    static_assert(WXSIZEOF(GLYPH_SVGS) == Glyph_Max,
                  "GLYPH_SVGS must contain an image for every glyph");

    // The size is in logical units, which are only different from pixels in
    // the ports using DPI-independent pixels.
#ifdef wxHAS_DPI_INDEPENDENT_PIXELS
    const double scale = dc.GetContentScaleFactor();
#else
    const double scale = 1.0;
#endif

    const int pixels = wxRound(size * scale);
    if ( pixels <= 0 || pixels > 0xfff )
        return;

    // Use the same line width for all glyphs, whatever their size. The
    // horizontal and vertical lines of some glyphs are on the grid, which
    // corresponds to whole pixels if the grid step in pixels is integer. In
    // this case, make the line width a whole number of pixels for these
    // glyphs and shift odd width lines by half a pixel, so that they cover
    // whole pixels and look crisp, as the Office glyphs do.
    const bool snap = (pixels * GLYPH_GRID_STEP) % GLYPH_SIZE == 0 &&
                      (glyph == Glyph_More ||
                       glyph == Glyph_Launcher ||
                       glyph == Glyph_Pin);
    double line_pixels = GetLineWidth(dc) * scale;
    double offset_pixels = 0.0;
    if ( snap )
    {
        line_pixels = wxMax(1, wxRound(line_pixels));
        if ( static_cast<int>(line_pixels) % 2 )
            offset_pixels = 0.5;
    }

    const wxUint64 key = (static_cast<wxUint64>(glyph) << 60) |
                         (static_cast<wxUint64>(colour.GetRGB() & 0xffffff) << 36) |
                         (static_cast<wxUint64>(pixels) << 24) |
                         (static_cast<wxUint64>(wxRound(scale * 100) & 0xfff) << 12) |
                         static_cast<wxUint64>(wxRound(line_pixels * 100) & 0xfff);
    wxBitmap& bitmap = (*m_glyph_cache)[key];
    if ( !bitmap.IsOk() )
    {
        // Convert from pixels to the units of the SVG images.
        const double units = static_cast<double>(GLYPH_SIZE) / pixels;

        wxString svg = GLYPH_SVGS[glyph];
        svg.Replace("{colour}", colour.GetAsString(wxC2S_HTML_SYNTAX));
        svg.Replace("{width}", wxString::FromCDouble(line_pixels * units, 3));
        svg.Replace("{offset}", wxString::FromCDouble(offset_pixels * units, 3));

        bitmap = wxBitmapBundle::FromSVG(svg.utf8_str(),
                                         wxSize(GLYPH_SIZE, GLYPH_SIZE))
                    .GetBitmap(wxSize(pixels, pixels));
        if ( !bitmap.IsOk() )
        {
            // Don't keep the invalid bitmap in the cache.
            m_glyph_cache->erase(key);
            return;
        }

        bitmap.SetScaleFactor(scale);
    }

    const double half = bitmap.GetLogicalWidth() / 2.0;
    dc.DrawBitmap(bitmap,
                  wxRound(centre.m_x - half), wxRound(centre.m_y - half), true);
}

void wxRibbonMSOArtProvider::SetColourScheme(
                         const wxColour& primary,
                         const wxColour& secondary,
                         const wxColour& tertiary)
{
    m_primary_scheme_colour = primary;
    m_secondary_scheme_colour = secondary;
    m_tertiary_scheme_colour = tertiary;

    // The glyphs in the old colours are not going to be used any more.
    m_glyph_cache->clear();

    const wxColour& background = primary;
    const bool dark = IsDarkColour(background);

    // In high contrast mode, use the colours as they are, without blending
    // them, and outline the highlights instead of filling them.
    const bool high_contrast = IsHighContrast();

    // Mix the given proportion of the second colour into the first one.
    const auto mix = [dark](const wxColour& c1, const wxColour& c2,
                            const MixRatio& ratio)
    {
        return Blend(c1, c2, ratio.For(dark));
    };

    // The page is shown as a slightly lighter "card" on the background.
    const wxColour card = high_contrast ? background
                                        : mix(background, *wxWHITE, MIX_CARD);

    // Use the provided text colour only if it is readable.
    wxColour text = tertiary;
    if ( !text.IsOk() ||
            GetLuminanceDifference(text, card) < MIN_TEXT_LUMINANCE_DIFFERENCE )
    {
        text = DEFAULT_TEXT_COLOUR.For(dark);
    }

    wxColour accent = secondary;
    if ( !accent.IsOk() ||
            GetLuminanceDifference(accent, card) < MIN_ACCENT_LUMINANCE_DIFFERENCE )
    {
        accent = DEFAULT_ACCENT_COLOUR.For(dark);
    }

    wxColour hover, pressed, checked, checked_border, separator, disabled_text;
    wxColour gallery_border, gallery_strong_border;
    if ( high_contrast )
    {
        hover =
        pressed =
        checked = card;
        checked_border = accent;
        separator =
        gallery_border =
        gallery_strong_border = text;
        disabled_text = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
        m_highlight_border_colour = accent;
    }
    else
    {
        hover = mix(card, text, MIX_HOVER);
        pressed = mix(card, text, MIX_PRESSED);
        checked = mix(card, text, MIX_TOGGLED);
        checked_border = mix(card, text, MIX_TOGGLED_BORDER);
        separator = mix(card, text, MIX_SEPARATOR);
        gallery_border = mix(card, text, MIX_GALLERY_BORDER);
        gallery_strong_border = mix(card, text, MIX_GALLERY_STRONG_BORDER);
        disabled_text = mix(text, card, MIX_DISABLED_TEXT);
        m_highlight_border_colour = wxColour();
    }

    // Tabs
    m_tab_ctrl_background_brush = background;
    m_tab_label_colour = text;
    m_tab_hover_label_colour = text;
    m_tab_active_label_colour = text;
    m_tab_active_background_colour = background;
    m_tab_active_background_gradient_colour = background;
    m_tab_hover_background_colour = high_contrast
                                        ? background
                                        : mix(background, text, MIX_TAB_HOVER);
    // The hover gradient colour is used for the hovered tab underline.
    m_tab_hover_background_gradient_colour = high_contrast
                                        ? text
                                        : mix(background, text, MIX_TAB_HOVER_UNDERLINE);
    m_tab_hover_background_top_colour = m_tab_hover_background_colour;
    m_tab_hover_background_top_gradient_colour = m_tab_hover_background_colour;
    // The highlight colour is used for the labels of the highlighted tabs.
    m_tab_highlight_colour = accent;
    m_tab_highlight_gradient_colour = m_tab_highlight_colour;
    m_tab_highlight_top_colour = m_tab_highlight_colour;
    m_tab_highlight_top_gradient_colour = m_tab_highlight_colour;
    m_tab_separator_colour = high_contrast
                                ? text
                                : mix(background, text, MIX_TAB_SEPARATOR);
    m_tab_separator_gradient_colour = m_tab_separator_colour;
    // The tab border colour is used for the active tab underline.
    m_tab_border_pen = wxPen(accent);

    // Page
    m_page_background_colour = card;
    m_page_background_gradient_colour = card;
    m_page_background_top_colour = card;
    m_page_background_top_gradient_colour = card;
    m_page_hover_background_colour = card;
    m_page_hover_background_gradient_colour = card;
    m_page_hover_background_top_colour = card;
    m_page_hover_background_top_gradient_colour = card;
    m_page_border_pen = wxPen(high_contrast
                                ? text
                                : mix(background, text, MIX_PAGE_BORDER));

    // Panels
    m_panel_label_colour = text;
    m_panel_hover_label_colour = text;
    m_panel_minimised_label_colour = text;
    m_panel_label_background_brush = card;
    m_panel_hover_label_background_brush = card;
    m_panel_border_pen = wxPen(separator);
    m_panel_border_gradient_pen = m_panel_border_pen;
    m_panel_hover_border_pen = m_panel_border_pen;
    m_panel_hover_border_gradient_pen = m_panel_border_pen;
    m_panel_minimised_border_pen = m_panel_border_pen;
    m_panel_minimised_border_gradient_pen = m_panel_border_pen;
    m_panel_active_background_colour = pressed;
    m_panel_active_background_gradient_colour = pressed;
    m_panel_active_background_top_colour = pressed;
    m_panel_active_background_top_gradient_colour = pressed;
    m_panel_hover_button_background_brush = hover;
    m_panel_hover_button_border_pen = wxPen(hover);
    SetColour(wxRIBBON_ART_PANEL_BUTTON_FACE_COLOUR, text);
    SetColour(wxRIBBON_ART_PANEL_BUTTON_HOVER_FACE_COLOUR, text);

    // Toggle and help buttons
    m_ribbon_toggle_brush = m_tab_hover_background_colour;
    m_ribbon_toggle_pen = wxPen(m_tab_hover_background_colour);
    SetColour(wxRIBBON_ART_PAGE_TOGGLE_FACE_COLOUR, text);
    SetColour(wxRIBBON_ART_PAGE_TOGGLE_HOVER_FACE_COLOUR, text);

    // Button bars: the "active top" colour is used for toggled buttons.
    m_button_bar_label_colour = text;
    m_button_bar_label_disabled_colour = disabled_text;
    m_button_bar_active_label_colour = text;
    m_button_bar_hover_background_colour = hover;
    m_button_bar_hover_background_gradient_colour = hover;
    m_button_bar_hover_background_top_colour = hover;
    m_button_bar_hover_background_top_gradient_colour = hover;
    m_button_bar_active_background_colour = pressed;
    m_button_bar_active_background_gradient_colour = pressed;
    m_button_bar_active_background_top_colour = checked;
    m_button_bar_active_background_top_gradient_colour = checked;
    m_button_bar_hover_border_pen = wxPen(separator);
    m_button_bar_active_border_pen = wxPen(checked_border);

    // Galleries: the item border is also used for the border of the column of
    // the scroll and extension buttons, as in Office.
    m_gallery_border_pen = wxPen(gallery_border);
    m_gallery_hover_background_brush = card;
    m_gallery_item_border_pen = wxPen(gallery_strong_border);
    m_gallery_button_background_colour = card;
    m_gallery_button_background_gradient_colour = card;
    m_gallery_button_background_top_brush = card;
    m_gallery_button_hover_background_colour = hover;
    m_gallery_button_hover_background_gradient_colour = hover;
    m_gallery_button_hover_background_top_brush = hover;
    m_gallery_button_active_background_colour = pressed;
    m_gallery_button_active_background_gradient_colour = pressed;
    m_gallery_button_active_background_top_brush = pressed;
    m_gallery_button_disabled_background_colour = card;
    m_gallery_button_disabled_background_gradient_colour = card;
    m_gallery_button_disabled_background_top_brush = card;
    SetColour(wxRIBBON_ART_GALLERY_BUTTON_FACE_COLOUR, text);
    SetColour(wxRIBBON_ART_GALLERY_BUTTON_HOVER_FACE_COLOUR, text);
    SetColour(wxRIBBON_ART_GALLERY_BUTTON_ACTIVE_FACE_COLOUR, text);
    SetColour(wxRIBBON_ART_GALLERY_BUTTON_DISABLED_FACE_COLOUR, disabled_text);

    // Tool bars: the "top" colour is used for toggled tools.
    m_toolbar_border_pen = wxPen(separator);
    SetColour(wxRIBBON_ART_TOOLBAR_FACE_COLOUR, text);
    m_tool_background_colour = card;
    m_tool_background_gradient_colour = card;
    m_tool_background_top_colour = checked;
    m_tool_background_top_gradient_colour = checked;
    m_tool_hover_background_colour = hover;
    m_tool_hover_background_gradient_colour = hover;
    m_tool_hover_background_top_colour = hover;
    m_tool_hover_background_top_gradient_colour = hover;
    m_tool_active_background_colour = pressed;
    m_tool_active_background_gradient_colour = pressed;
    m_tool_active_background_top_colour = pressed;
    m_tool_active_background_top_gradient_colour = pressed;

    // Invalidate cached tab separator
    m_cached_tab_separator_visibility = -1.0;
}

// ----------------------------------------------------------------------------
// Tabs
// ----------------------------------------------------------------------------

int wxRibbonMSOArtProvider::GetTabCtrlHeight(
                        wxReadOnlyDC& dc,
                        wxWindow* wnd,
                        const wxRibbonPageTabInfoArray& pages)
{
    UpdateDPIScale(dc, wnd);

    m_bar_button_size = dc.FromDIP(BAR_BUTTON_SIZE);

    if ( pages.GetCount() <= 1 && (m_flags & wxRIBBON_BAR_ALWAYS_SHOW_TABS) == 0 )
    {
        m_tab_ctrl_height = TAB_CTRL_HEIGHT_WITHOUT_TABS;
        return m_tab_ctrl_height;
    }

    int text_height = 0;
    int icon_height = 0;

    if ( m_flags & wxRIBBON_BAR_SHOW_PAGE_LABELS )
        text_height = GetLineHeight(dc, m_tab_label_font);
    if ( m_flags & wxRIBBON_BAR_SHOW_PAGE_ICONS )
    {
        for ( size_t i = 0; i < pages.GetCount(); ++i )
        {
            const wxRibbonPageTabInfo& info = pages.Item(i);
            if ( info.page->GetIcon().IsOk() )
            {
                icon_height = wxMax(icon_height,
                    info.page->GetIcon().GetLogicalHeight());
            }
        }
    }

    // Don't waste any space around the contents and the underline below them,
    // but make sure that the toggle and help buttons fit into the tab row.
    m_tab_ctrl_height = wxMax(text_height, icon_height) +
                        dc.FromDIP(TAB_TOP_PADDING) +
                        dc.FromDIP(TAB_UNDERLINE_TOP_GAP) +
                        dc.FromDIP(TAB_UNDERLINE_HEIGHT) +
                        dc.FromDIP(TAB_UNDERLINE_BOTTOM_GAP);
    if ( m_flags & (wxRIBBON_BAR_SHOW_TOGGLE_BUTTON |
                    wxRIBBON_BAR_SHOW_HELP_BUTTON) )
    {
        m_tab_ctrl_height = wxMax(m_tab_ctrl_height, m_bar_button_size + 2);
    }

    return m_tab_ctrl_height;
}

void wxRibbonMSOArtProvider::GetBarTabWidth(
                        wxReadOnlyDC& dc,
                        wxWindow* wnd,
                        const wxString& label,
                        const wxBitmap& bitmap,
                        int* ideal,
                        int* small_begin_need_separator,
                        int* small_must_have_separator,
                        int* minimum)
{
    UpdateDPIScale(dc, wnd);

    int width = 0;
    int min = 0;
    if ( (m_flags & wxRIBBON_BAR_SHOW_PAGE_LABELS) && !label.empty() )
    {
        // Measure with the font used for the active tab as it is wider.
        wxFont font(m_tab_label_font);
        font.SetWeight(wxFONTWEIGHT_SEMIBOLD);
        width += GetTextSize(dc, label, font).GetWidth();
        min += wxMin(dc.FromDIP(TAB_MIN_LABEL_WIDTH), width);
        if ( bitmap.IsOk() )
        {
            width += dc.FromDIP(TAB_ICON_GAP);
            min += dc.FromDIP(TAB_MIN_ICON_GAP);
        }
    }
    if ( (m_flags & wxRIBBON_BAR_SHOW_PAGE_ICONS) && bitmap.IsOk() )
    {
        width += bitmap.GetLogicalWidth();
        min += bitmap.GetLogicalWidth();
    }

    if ( ideal != nullptr )
        *ideal = width + dc.FromDIP(TAB_IDEAL_HORZ_PADDING);
    if ( small_begin_need_separator != nullptr )
        *small_begin_need_separator = width + dc.FromDIP(TAB_SMALL_HORZ_PADDING);
    if ( small_must_have_separator != nullptr )
        *small_must_have_separator = width + dc.FromDIP(TAB_MIN_HORZ_PADDING);
    if ( minimum != nullptr )
        *minimum = min;
}

void wxRibbonMSOArtProvider::DrawTabCtrlBackground(
                        wxDC& dc,
                        wxWindow* WXUNUSED(wnd),
                        const wxRect& rect)
{
    FillRect(dc, rect, m_tab_ctrl_background_brush.GetColour());
}

void wxRibbonMSOArtProvider::DrawTab(
                 wxDC& dc,
                 wxWindow* WXUNUSED(wnd),
                 const wxRibbonPageTabInfo& tab)
{
    if ( tab.rect.height <= TAB_CTRL_HEIGHT_WITHOUT_TABS )
        return;

    // The tabs never have any background: the labels of the highlighted tabs
    // use the accent colour and the active and hovered tabs get an underline.
    if ( tab.page == nullptr )
        return;

    // The underline is at the bottom of the tab, just above the page, and the
    // contents are centred in the space above it, which normally has exactly
    // their height, but may be bigger, e.g. to fit the toggle button.
    const int underline_height = dc.FromDIP(TAB_UNDERLINE_HEIGHT);
    const int underline_y = tab.rect.GetBottom() + 1 -
                            dc.FromDIP(TAB_UNDERLINE_BOTTOM_GAP) -
                            underline_height;
    const int content_top = tab.rect.y + dc.FromDIP(TAB_TOP_PADDING);
    const int content_height = underline_y - dc.FromDIP(TAB_UNDERLINE_TOP_GAP) -
                               content_top;

    wxBitmap icon;
    if ( m_flags & wxRIBBON_BAR_SHOW_PAGE_ICONS )
        icon = tab.page->GetIcon();

    wxString label;
    if ( m_flags & wxRIBBON_BAR_SHOW_PAGE_LABELS )
        label = tab.page->GetLabel();

    wxFont font(m_tab_label_font);
    if ( tab.active )
        font.SetWeight(wxFONTWEIGHT_SEMIBOLD);
    wxDCFontChanger font_changer(dc, font);

    wxSize text_size;
    if ( !label.empty() )
        text_size = dc.GetTextExtent(label);

    int content_width = text_size.GetWidth();
    if ( icon.IsOk() )
    {
        content_width += icon.GetLogicalWidth();
        if ( !label.empty() )
            content_width += dc.FromDIP(TAB_ICON_GAP);
    }

    const int available = tab.rect.width - dc.FromDIP(2 * TAB_CONTENT_MARGIN);
    int x = tab.rect.x + (tab.rect.width - wxMin(content_width, available)) / 2;

    if ( icon.IsOk() )
    {
        dc.DrawBitmap(icon, x,
            content_top + (content_height - icon.GetLogicalHeight()) / 2, true);
        x += icon.GetLogicalWidth() + dc.FromDIP(TAB_ICON_GAP);
    }

    if ( !label.empty() )
    {
        wxColour colour;
        if ( tab.highlight )
            colour = m_tab_highlight_colour;
        else if ( tab.active )
            colour = m_tab_active_label_colour;
        else if ( tab.hovered )
            colour = m_tab_hover_label_colour;
        else
            colour = m_tab_label_colour;

        wxDCTextColourChanger colour_changer(dc, colour);
        wxDCTextBgModeChanger mode_changer(dc, wxBRUSHSTYLE_TRANSPARENT);

        const int y = content_top + (content_height - text_size.GetHeight()) / 2;
        const int text_width = tab.rect.GetRight() - x -
                               dc.FromDIP(TAB_CONTENT_MARGIN);
        if ( text_width < text_size.GetWidth() )
        {
            // Not enough space when the tabs are squeezed: like Office, show
            // as many whole characters as fit, without any ellipsis.
            const wxString shortened = FitLabel(dc, label, text_width,
                                                Shorten::Truncate);
            dc.DrawText(shortened, x, y);
        }
        else
        {
            dc.DrawText(label, x, y);
        }
    }

    // The active tab is underlined with the accent colour and the hovered one
    // with a grey line. Like in Office, the underline of the active tab gets
    // longer, almost as wide as the tab, when it is hovered.
    if ( tab.active || tab.hovered )
    {
        int width = content_width;
        if ( tab.active && tab.hovered )
            width += 2 * dc.FromDIP(TAB_UNDERLINE_HOVER_EXTENSION);
        width = wxMin(width, available);
        if ( width <= 0 )
            width = tab.rect.width / 2;

        const wxRect underline(tab.rect.x + (tab.rect.width - width) / 2,
                               underline_y, width, underline_height);

        MSOPainter painter(dc, m_highlight_border_colour);
        painter.RoundedRect(underline, underline_height / 2.0,
                            tab.active ? m_tab_border_pen.GetColour()
                                       : m_tab_hover_background_gradient_colour);
    }
}

void wxRibbonMSOArtProvider::DrawTabSeparator(
                        wxDC& dc,
                        wxWindow* WXUNUSED(wnd),
                        const wxRect& rect,
                        double visibility)
{
    if ( visibility <= 0.0 )
        return;
    if ( visibility > 1.0 )
        visibility = 1.0;

    wxDCPenChanger pen(dc, wxPen(Blend(m_tab_ctrl_background_brush.GetColour(),
                                       m_tab_separator_colour, visibility)));

    const int x = rect.x + rect.width / 2;
    dc.DrawLine(x, rect.y + rect.height / 4, x, rect.y + (rect.height * 3) / 4);
}

// ----------------------------------------------------------------------------
// Page
// ----------------------------------------------------------------------------

void wxRibbonMSOArtProvider::DrawPageBackground(
                        wxDC& dc,
                        wxWindow* WXUNUSED(wnd),
                        const wxRect& rect)
{
    FillRect(dc, rect, m_tab_ctrl_background_brush.GetColour());

    MSOPainter painter(dc, m_highlight_border_colour);
    painter.RoundedRect(GetPageCardRect(dc, rect), dc.FromDIP(PAGE_CARD_RADIUS),
                        m_page_background_colour,
                        m_page_border_pen.GetColour());
}

wxRect wxRibbonMSOArtProvider::GetPageCardRect(wxReadOnlyDC& dc,
                                               const wxRect& page_rect) const
{
    const int inset = dc.FromDIP(PAGE_CARD_INSET);
    wxRect card(page_rect);
    card.x += inset;
    card.width -= 2 * inset;
    card.height -= inset;
    return card;
}

wxRect wxRibbonMSOArtProvider::GetPageBackgroundRedrawArea(
                        wxReadOnlyDC& dc,
                        const wxRibbonPage* WXUNUSED(wnd),
                        wxSize page_old_size,
                        wxSize page_new_size)
{
    // Only the part near the right or bottom edge of the card, including its
    // rounded corners, changes when only the width or height changes.
    const int band = dc.FromDIP(PAGE_CARD_INSET) + dc.FromDIP(PAGE_CARD_RADIUS) + 1;
    if ( page_old_size.y == page_new_size.y )
    {
        const int x = wxMax(0, wxMin(page_old_size.x, page_new_size.x) - band);
        return wxRect(x, 0, page_new_size.x - x, page_new_size.y);
    }
    if ( page_old_size.x == page_new_size.x )
    {
        const int y = wxMax(0, wxMin(page_old_size.y, page_new_size.y) - band);
        return wxRect(0, y, page_new_size.x, page_new_size.y - y);
    }

    return wxRect(page_new_size);
}

void wxRibbonMSOArtProvider::FillPageBackground(wxDC& dc,
                                                wxWindow* wnd,
                                                const wxRect& rect)
{
    // Find the page containing this window and its offset from it.
    wxRibbonPage* page = nullptr;
    wxPoint offset;
    for ( wxWindow* win = wnd; win && !win->IsTopLevel(); win = win->GetParent() )
    {
        page = wxDynamicCast(win, wxRibbonPage);
        if ( page )
            break;

        offset += win->GetPosition();
    }

    if ( !page )
    {
        // This happens for the expanded panels shown in a popup.
        FillRect(dc, rect, m_page_background_colour);
        return;
    }

    // Draw the part of the page background, including the card border, under
    // this window: normally it only shows the card colour, but windows may
    // extend beyond the card, e.g. panels taller than the page.
    wxRect page_rect(page->GetSize());
    page->AdjustRectToIncludeScrollButtons(&page_rect);
    page_rect.Offset(-offset);

    // Most windows are entirely inside the card and only need to be filled
    // with its colour, avoid drawing the entire page for them.
    wxRect interior = GetPageCardRect(dc, page_rect);
    interior.Deflate(dc.FromDIP(PAGE_CARD_RADIUS) + 1);
    if ( interior.Contains(rect) )
    {
        FillRect(dc, rect, m_page_background_colour);
        return;
    }

    wxDCClipper clip(dc, rect);
    DrawPageBackground(dc, page, page_rect);
}

void wxRibbonMSOArtProvider::DrawScrollButton(
                        wxDC& dc,
                        wxWindow* WXUNUSED(wnd),
                        const wxRect& rect,
                        long style)
{
    const bool for_tabs =
        (style & wxRIBBON_SCROLL_BTN_FOR_MASK) == wxRIBBON_SCROLL_BTN_FOR_TABS;

    FillRect(dc, rect, for_tabs ? m_tab_ctrl_background_brush.GetColour()
                                : m_page_background_colour);

    wxColour background;
    if ( style & wxRIBBON_SCROLL_BTN_ACTIVE )
        background = m_button_bar_active_background_colour;
    else if ( style & wxRIBBON_SCROLL_BTN_HOVERED )
        background = for_tabs ? m_tab_hover_background_colour
                              : m_button_bar_hover_background_colour;

    {
        MSOPainter painter(dc, m_highlight_border_colour);
        if ( for_tabs )
        {
            if ( background.IsOk() )
            {
                wxRect bg(rect);
                bg.Deflate(dc.FromDIP(SCROLL_BUTTON_INSET),
                           dc.FromDIP(SCROLL_BUTTON_TAB_VERT_INSET));
                painter.Highlight(bg, dc.FromDIP(CORNER_RADIUS), background);
            }
        }
        else
        {
            wxRect bg(rect);
            bg.Deflate(dc.FromDIP(SCROLL_BUTTON_INSET));
            painter.RoundedRect(bg, dc.FromDIP(CORNER_RADIUS),
                background.IsOk() ? background : m_page_background_colour,
                m_page_border_pen.GetColour());
        }
    }

    Glyph glyph;
    switch ( style & wxRIBBON_SCROLL_BTN_DIRECTION_MASK )
    {
        case wxRIBBON_SCROLL_BTN_LEFT:
            glyph = Glyph_ChevronLeft;
            break;
        case wxRIBBON_SCROLL_BTN_RIGHT:
            glyph = Glyph_ChevronRight;
            break;
        case wxRIBBON_SCROLL_BTN_UP:
            glyph = Glyph_ChevronUp;
            break;
        default:
            glyph = Glyph_ChevronDown;
            break;
    }

    DrawGlyph(dc, glyph,
              wxPoint2DDouble(rect.x + rect.width / 2.0, rect.y + rect.height / 2.0),
              dc.FromDIP(GLYPH_SIZE), m_tab_label_colour);
}

wxSize wxRibbonMSOArtProvider::GetScrollButtonMinimumSize(
                        wxReadOnlyDC& dc,
                        wxWindow* wnd,
                        long WXUNUSED(style))
{
    UpdateDPIScale(dc, wnd);

    return dc.FromDIP(wxSize(SCROLL_BUTTON_SIZE, SCROLL_BUTTON_SIZE));
}

// ----------------------------------------------------------------------------
// Panels
// ----------------------------------------------------------------------------

wxSize wxRibbonMSOArtProvider::GetPanelDecorationSize(
                        wxReadOnlyDC& dc,
                        wxPoint* client_offset) const
{
    const int label_height = GetLineHeight(dc, m_panel_label_font);

    const int horz = dc.FromDIP(PANEL_HORZ_PADDING);
    const int top = dc.FromDIP(PANEL_TOP_PADDING);
    const int bottom = dc.FromDIP(PANEL_LABEL_GAP) + label_height +
                       dc.FromDIP(PANEL_BOTTOM_PADDING);

    // Account for 1 pixel of panel padding on both sides of the major axis
    // (see RemovePanelPadding()) and 1 more for the separator line.
    const int extra = 2 * PANEL_PADDING_PIXELS + PANEL_SEPARATOR_PIXELS;
    wxSize size(2 * horz, top + bottom);
    wxPoint offset(horz, top);
    if ( m_flags & wxRIBBON_BAR_FLOW_VERTICAL )
    {
        size.y += extra;
        offset.y += PANEL_PADDING_PIXELS;
    }
    else
    {
        size.x += extra;
        offset.x += PANEL_PADDING_PIXELS;
    }

    if ( client_offset != nullptr )
        *client_offset = offset;

    return size;
}

wxSize wxRibbonMSOArtProvider::GetPanelSize(
                        wxReadOnlyDC& dc,
                        const wxRibbonPanel* wnd,
                        wxSize client_size,
                        wxPoint* client_offset)
{
    UpdateDPIScale(dc, wnd);

    return client_size + GetPanelDecorationSize(dc, client_offset);
}

wxSize wxRibbonMSOArtProvider::GetPanelClientSize(
                        wxReadOnlyDC& dc,
                        const wxRibbonPanel* wnd,
                        wxSize size,
                        wxPoint* client_offset)
{
    UpdateDPIScale(dc, wnd);

    size -= GetPanelDecorationSize(dc, client_offset);
    if ( size.x < 0 )
        size.x = 0;
    if ( size.y < 0 )
        size.y = 0;
    return size;
}

void wxRibbonMSOArtProvider::GetPanelLabelLayout(
                        wxReadOnlyDC& dc,
                        const wxRibbonPanel* wnd,
                        const wxRect& rect,
                        wxRect* label_rect,
                        wxRect* ext_rect,
                        bool* has_separator)
{
    wxRect true_rect(rect);
    RemovePanelPadding(&true_rect);

    // GetExpandedDummy() is not const, even though it doesn't modify the panel.
    const bool separator =
        const_cast<wxRibbonPanel*>(wnd)->GetExpandedDummy() == nullptr &&
        !IsLastPanel(wnd);
    if ( has_separator )
        *has_separator = separator;

    // Space taken by the separator (which is always reserved for consistency).
    if ( m_flags & wxRIBBON_BAR_FLOW_VERTICAL )
        true_rect.height -= PANEL_SEPARATOR_PIXELS;
    else
        true_rect.width -= PANEL_SEPARATOR_PIXELS;

    const int label_height = GetLineHeight(dc, m_panel_label_font);
    const int horz = dc.FromDIP(PANEL_HORZ_PADDING);

    wxRect label(true_rect.x + horz,
                 true_rect.GetBottom() - dc.FromDIP(PANEL_BOTTOM_PADDING) - label_height + 1,
                 true_rect.width - 2 * horz,
                 label_height);

    wxRect ext;
    if ( wnd->HasExtButton() )
    {
        const int size = wxMax(dc.FromDIP(PANEL_EXT_BUTTON_SIZE), label_height);
        ext = wxRect(label.GetRight() - size + 1 + horz / 2,
                     label.y + (label_height - size) / 2,
                     size, size);

        // Keep the label centred by reserving the space on both sides.
        label.Deflate(size, 0);
    }

    if ( label_rect )
        *label_rect = label;
    if ( ext_rect )
        *ext_rect = ext;
}

wxRect wxRibbonMSOArtProvider::GetPanelExtButtonArea(
                        wxReadOnlyDC& dc,
                        const wxRibbonPanel* wnd,
                        wxRect rect)
{
    wxRect ext;
    GetPanelLabelLayout(dc, wnd, rect, nullptr, &ext, nullptr);
    return ext;
}

void wxRibbonMSOArtProvider::DrawPanelBackground(
                        wxDC& dc,
                        wxRibbonPanel* wnd,
                        const wxRect& rect)
{
    FillPageBackground(dc, wnd, rect);

    const bool expanded = wnd->GetExpandedDummy() != nullptr;
    if ( expanded )
    {
        // An expanded panel is shown in its own popup, give it a border.
        MSOPainter painter(dc, m_highlight_border_colour);
        painter.RoundedRect(rect, 0, wxColour(), m_page_border_pen.GetColour());
    }

    wxRect label_rect, ext_rect;
    bool has_separator;
    GetPanelLabelLayout(dc, wnd, rect, &label_rect, &ext_rect, &has_separator);

    // The controls in a disabled panel are drawn as disabled by themselves,
    // but its label and extension button must be drawn as disabled here.
    const bool enabled = wnd->IsEnabled();

    // Label
    wxDCFontChanger font_changer(dc, m_panel_label_font);
    wxDCTextColourChanger colour_changer(dc,
        !enabled ? m_button_bar_label_disabled_colour
                 : wnd->IsHovered() ? m_panel_hover_label_colour
                                    : m_panel_label_colour);
    wxDCTextBgModeChanger mode_changer(dc, wxBRUSHSTYLE_TRANSPARENT);
    const wxString label = FitLabel(dc, wnd->GetLabel(), label_rect.width);
    if ( !label.empty() )
    {
        const wxSize label_size = dc.GetTextExtent(label);
        dc.DrawText(label,
                    label_rect.x + (label_rect.width - label_size.x) / 2,
                    label_rect.y + (label_rect.height - label_size.y) / 2);
    }

    // Extension ("dialog launcher") button
    if ( !ext_rect.IsEmpty() )
    {
        const bool hovered = enabled && wnd->IsExtButtonHovered();

        if ( hovered )
        {
            MSOPainter painter(dc, m_highlight_border_colour);
            painter.Highlight(ext_rect, dc.FromDIP(SMALL_CORNER_RADIUS),
                m_panel_hover_button_background_brush.GetColour());
        }

        DrawGlyph(dc, Glyph_Launcher,
                  wxPoint2DDouble(ext_rect.x + ext_rect.width / 2.0,
                                  ext_rect.y + ext_rect.height / 2.0),
                  dc.FromDIP(GLYPH_SIZE),
                  !enabled ? m_gallery_button_disabled_face_colour
                           : hovered ? m_panel_button_hover_face_colour
                                     : m_panel_button_face_colour);
    }

    // Separator between this panel and the next one
    if ( has_separator )
    {
        wxRect true_rect(rect);
        RemovePanelPadding(&true_rect);

        wxDCPenChanger pen(dc, m_panel_border_pen);
        if ( m_flags & wxRIBBON_BAR_FLOW_VERTICAL )
        {
            const int inset = dc.FromDIP(PANEL_SEPARATOR_INSET_VERTICAL);
            const int y = true_rect.GetBottom();
            dc.DrawLine(true_rect.x + inset, y, true_rect.GetRight() - inset + 1, y);
        }
        else
        {
            const int inset = dc.FromDIP(PANEL_SEPARATOR_INSET);
            const int x = true_rect.GetRight();
            dc.DrawLine(x, true_rect.y + inset, x, true_rect.GetBottom() - inset + 1);
        }
    }
}

wxSize wxRibbonMSOArtProvider::GetMinimisedPanelMinimumSize(
                        wxReadOnlyDC& dc,
                        const wxRibbonPanel* wnd,
                        wxSize* desired_bitmap_size,
                        wxDirection* expanded_panel_direction)
{
    UpdateDPIScale(dc, wnd);

    // The icon slot is 32 DIPs high, as the icons of collapsed groups in
    // Office. Notice that the panel currently doesn't use this size, but just
    // the preferred size of its icon, which is centred in the slot.
    if ( desired_bitmap_size != nullptr )
        *desired_bitmap_size = dc.FromDIP(wxSize(MINIMISED_PANEL_ICON_SIZE,
                                                 MINIMISED_PANEL_ICON_SIZE));

    if ( expanded_panel_direction != nullptr )
    {
        if ( m_flags & wxRIBBON_BAR_FLOW_VERTICAL )
            *expanded_panel_direction = wxEAST;
        else
            *expanded_panel_direction = wxSOUTH;
    }

    const wxSize label_size = GetTextSize(dc, wnd->GetLabel(),
                                          m_button_bar_label_font);
    const int line_height = GetLineHeight(dc, m_button_bar_label_font);
    const int icon = dc.FromDIP(MINIMISED_PANEL_ICON_SIZE);
    const int pad = dc.FromDIP(MINIMISED_PANEL_PADDING);

    if ( m_flags & wxRIBBON_BAR_FLOW_VERTICAL )
    {
        // Icon, label and chevron in a row
        return wxSize(icon + label_size.x + dc.FromDIP(CHEVRON_EXTRA_WIDTH) + 3 * pad,
                      icon + 2 * pad);
    }
    else
    {
        // Icon, label and chevron in a column
        return wxSize(wxMax(icon, label_size.x) + 2 * pad +
                        2 * PANEL_PADDING_PIXELS + PANEL_SEPARATOR_PIXELS,
                      icon + 2 * line_height + 3 * pad);
    }
}

void wxRibbonMSOArtProvider::DrawMinimisedPanel(
                        wxDC& dc,
                        wxRibbonPanel* wnd,
                        const wxRect& rect,
                        wxBitmap& bitmap)
{
    FillPageBackground(dc, wnd, rect);

    wxRect true_rect(rect);
    RemovePanelPadding(&true_rect);

    const bool vertical = (m_flags & wxRIBBON_BAR_FLOW_VERTICAL) != 0;
    const bool has_separator = !IsLastPanel(wnd);

    wxRect button(true_rect);
    if ( vertical )
        button.height -= PANEL_SEPARATOR_PIXELS;
    else
        button.width -= PANEL_SEPARATOR_PIXELS;
    button.Deflate(dc.FromDIP(MINIMISED_PANEL_HIGHLIGHT_INSET));

    const bool enabled = wnd->IsEnabled();
    if ( enabled && (wnd->GetExpandedPanel() != nullptr || wnd->IsHovered()) )
    {
        MSOPainter painter(dc, m_highlight_border_colour);
        painter.Highlight(button, dc.FromDIP(CORNER_RADIUS),
                          wnd->GetExpandedPanel() != nullptr
                            ? m_panel_active_background_colour
                            : m_button_bar_hover_background_colour);
    }

    const wxColour& label_colour = enabled ? m_panel_minimised_label_colour
                                           : m_button_bar_label_disabled_colour;
    // Don't modify the bitmap passed in, it is the panel's cached icon.
    wxBitmap icon_bitmap(bitmap);
    if ( !enabled && icon_bitmap.IsOk() )
        wxBitmap::MakeDisabled(icon_bitmap);

    wxDCFontChanger font_changer(dc, m_button_bar_label_font);
    wxDCTextColourChanger colour_changer(dc, label_colour);
    wxDCTextBgModeChanger mode_changer(dc, wxBRUSHSTYLE_TRANSPARENT);

    // This must be consistent with GetMinimisedPanelMinimumSize(), use the
    // same slot for all panels to keep their labels aligned.
    const int icon = dc.FromDIP(MINIMISED_PANEL_ICON_SIZE);
    const int pad = dc.FromDIP(MINIMISED_PANEL_CONTENT_PADDING);
    const int line_height = GetLineHeight(dc, m_button_bar_label_font);

    const double glyph_size = dc.FromDIP(SMALL_GLYPH_SIZE);

    if ( vertical )
    {
        const int icon_x = button.x + pad;
        if ( icon_bitmap.IsOk() )
        {
            dc.DrawBitmap(icon_bitmap, icon_x + (icon - icon_bitmap.GetLogicalWidth()) / 2,
                button.y + (button.height - icon_bitmap.GetLogicalHeight()) / 2, true);
        }

        const int x = icon_x + icon + pad;
        const wxString label = FitLabel(dc, wnd->GetLabel(),
            button.GetRight() - x - dc.FromDIP(CHEVRON_EXTRA_WIDTH));
        const wxSize label_size = dc.GetTextExtent(label);
        dc.DrawText(label, x, button.y + (button.height - label_size.y) / 2);

        DrawGlyph(dc, Glyph_ChevronRight,
                  wxPoint2DDouble(x + label_size.x +
                                    dc.FromDIP(MINIMISED_PANEL_CHEVRON_GAP) +
                                    glyph_size / 4,
                                  button.y + button.height / 2.0),
                  glyph_size, label_colour);
    }
    else
    {
        const int icon_y = button.y + pad;
        if ( icon_bitmap.IsOk() )
        {
            dc.DrawBitmap(icon_bitmap,
                button.x + (button.width - icon_bitmap.GetLogicalWidth()) / 2,
                icon_y + (icon - icon_bitmap.GetLogicalHeight()) / 2, true);
        }

        const int y = icon_y + icon + pad;
        const wxString label = FitLabel(dc, wnd->GetLabel(), button.width - 2 * pad);
        const wxSize label_size = dc.GetTextExtent(label);
        dc.DrawText(label, button.x + (button.width - label_size.x) / 2, y);

        DrawGlyph(dc, Glyph_ChevronDown,
                  wxPoint2DDouble(button.x + button.width / 2.0,
                                  y + line_height + line_height / 2.0),
                  glyph_size, label_colour);
    }

    if ( has_separator )
    {
        wxDCPenChanger pen(dc, m_panel_border_pen);
        if ( vertical )
        {
            const int inset = dc.FromDIP(PANEL_SEPARATOR_INSET_VERTICAL);
            const int y = true_rect.GetBottom();
            dc.DrawLine(true_rect.x + inset, y, true_rect.GetRight() - inset + 1, y);
        }
        else
        {
            const int inset = dc.FromDIP(PANEL_SEPARATOR_INSET);
            const int x = true_rect.GetRight();
            dc.DrawLine(x, true_rect.y + inset, x, true_rect.GetBottom() - inset + 1);
        }
    }
}

// ----------------------------------------------------------------------------
// Galleries
// ----------------------------------------------------------------------------

void wxRibbonMSOArtProvider::DrawGalleryBackground(
                        wxDC& dc,
                        wxRibbonGallery* wnd,
                        const wxRect& rect)
{
    FillPageBackground(dc, wnd, rect);

    const double radius = dc.FromDIP(CORNER_RADIUS);
    {
        MSOPainter painter(dc, m_highlight_border_colour);
        painter.RoundedRect(rect, radius, m_page_background_colour,
                            m_gallery_border_pen.GetColour());
    }

    // Use the same button rectangles as the gallery uses for hit testing.
    wxRect up_btn, down_btn, ext_btn;
    GetGalleryClientSize(dc, wnd, rect.GetSize(), nullptr,
                         &up_btn, &down_btn, &ext_btn);
    up_btn.Offset(rect.GetPosition());
    down_btn.Offset(rect.GetPosition());
    ext_btn.Offset(rect.GetPosition());

    // The buttons column has rounded corners only on the outer side, so draw
    // the rounded rectangle extending beyond it and clip it.
    const bool vertical = (m_flags & wxRIBBON_BAR_FLOW_VERTICAL) != 0;
    wxRect buttons, buttons_outline;
    if ( vertical )
    {
        buttons = wxRect(rect.x, up_btn.y, rect.width, rect.GetBottom() - up_btn.y + 1);
        buttons_outline = buttons;
        buttons_outline.y -= wxRound(radius);
        buttons_outline.height += wxRound(radius);
    }
    else
    {
        buttons = wxRect(up_btn.x, rect.y, rect.GetRight() - up_btn.x + 1, rect.height);
        buttons_outline = buttons;
        buttons_outline.x -= wxRound(radius);
        buttons_outline.width += wxRound(radius);
    }

    {
        wxDCClipper clip(dc, buttons);
        MSOPainter painter(dc, m_highlight_border_colour);
        painter.RoundedRect(buttons_outline, radius, wxColour(),
                            m_gallery_item_border_pen.GetColour());
    }

    DrawGalleryGlyphButton(dc, up_btn, wnd->GetUpButtonState(),
        vertical ? Glyph_ChevronLeft : Glyph_ChevronUp);
    DrawGalleryGlyphButton(dc, down_btn, wnd->GetDownButtonState(),
        vertical ? Glyph_ChevronRight : Glyph_ChevronDown);
    DrawGalleryGlyphButton(dc, ext_btn, wnd->GetExtensionButtonState(),
        Glyph_More);
}

wxSize wxRibbonMSOArtProvider::GetGallerySize(
                        wxReadOnlyDC& dc,
                        const wxRibbonGallery* wnd,
                        wxSize client_size)
{
    UpdateDPIScale(dc, wnd);

    // Padding on all sides, so that the items stay inside the rounded border,
    // and the buttons column.
    const int pad = dc.FromDIP(GALLERY_PADDING);
    client_size.IncBy(2 * pad, 2 * pad);
    if ( m_flags & wxRIBBON_BAR_FLOW_VERTICAL )
        client_size.IncBy(0, dc.FromDIP(GALLERY_BUTTON_WIDTH));
    else
        client_size.IncBy(dc.FromDIP(GALLERY_BUTTON_WIDTH), 0);
    return client_size;
}

wxSize wxRibbonMSOArtProvider::GetGalleryClientSize(
                        wxReadOnlyDC& dc,
                        const wxRibbonGallery* wnd,
                        wxSize size,
                        wxPoint* client_offset,
                        wxRect* scroll_up_button,
                        wxRect* scroll_down_button,
                        wxRect* extension_button)
{
    UpdateDPIScale(dc, wnd);

    const int bw = dc.FromDIP(GALLERY_BUTTON_WIDTH);
    wxRect scroll_up, scroll_down, extension;
    if ( m_flags & wxRIBBON_BAR_FLOW_VERTICAL )
    {
        // Flow is vertical - put buttons on bottom
        scroll_up = wxRect(0, size.GetHeight() - bw,
                           (size.GetWidth() + 2) / 3, bw);
        scroll_down = wxRect(scroll_up.GetRight() + 1, scroll_up.y,
                             scroll_up.width, bw);
        extension = wxRect(scroll_down.GetRight() + 1, scroll_up.y,
                           size.GetWidth() - 2 * scroll_up.width, bw);
        size.DecBy(0, bw);
    }
    else
    {
        // Flow is horizontal - put buttons on right
        scroll_up = wxRect(size.GetWidth() - bw, 0,
                           bw, (size.GetHeight() + 2) / 3);
        scroll_down = wxRect(scroll_up.x, scroll_up.GetBottom() + 1,
                             bw, scroll_up.height);
        extension = wxRect(scroll_up.x, scroll_down.GetBottom() + 1,
                           bw, size.GetHeight() - 2 * scroll_up.height);
        size.DecBy(bw, 0);
    }

    const int pad = dc.FromDIP(GALLERY_PADDING);
    size.DecBy(2 * pad, 2 * pad);
    size.IncTo(wxSize(0, 0));

    if ( client_offset != nullptr )
        *client_offset = wxPoint(pad, pad);
    if ( scroll_up_button != nullptr )
        *scroll_up_button = scroll_up;
    if ( scroll_down_button != nullptr )
        *scroll_down_button = scroll_down;
    if ( extension_button != nullptr )
        *extension_button = extension;

    return size;
}

void wxRibbonMSOArtProvider::DrawGalleryGlyphButton(wxDC& dc,
                                                    wxRect rect,
                                                    wxRibbonGalleryButtonState state,
                                                    Glyph glyph)
{
    wxColour background;
    wxColour face;
    switch ( state )
    {
        case wxRIBBON_GALLERY_BUTTON_NORMAL:
            face = m_gallery_button_face_colour;
            break;
        case wxRIBBON_GALLERY_BUTTON_HOVERED:
            background = m_gallery_button_hover_background_colour;
            face = m_gallery_button_hover_face_colour;
            break;
        case wxRIBBON_GALLERY_BUTTON_ACTIVE:
            background = m_gallery_button_active_background_colour;
            face = m_gallery_button_active_face_colour;
            break;
        case wxRIBBON_GALLERY_BUTTON_DISABLED:
            face = m_gallery_button_disabled_face_colour;
            break;
    }

    rect.Deflate(dc.FromDIP(GALLERY_BUTTON_INSET));

    if ( background.IsOk() )
    {
        MSOPainter painter(dc, m_highlight_border_colour);
        painter.Highlight(rect, dc.FromDIP(SMALL_CORNER_RADIUS), background);
    }

    // The glyphs only take the middle half of their square, so make it
    // smaller if the button doesn't have enough space for it.
    const double size = wxMin(glyph == Glyph_More ? dc.FromDIP(GLYPH_SIZE)
                                                  : dc.FromDIP(SMALL_GLYPH_SIZE),
                              2.0 * (wxMin(rect.width, rect.height) - 2));

    DrawGlyph(dc, glyph,
              wxPoint2DDouble(rect.x + rect.width / 2.0, rect.y + rect.height / 2.0),
              size, face);
}

void wxRibbonMSOArtProvider::DrawGalleryItemBackground(
                        wxDC& dc,
                        wxRibbonGallery* wnd,
                        const wxRect& rect,
                        wxRibbonGalleryItem* item)
{
    const bool selected = wnd->GetActiveItem() == item ||
                          wnd->GetSelection() == item;
    const bool enabled = wnd->IsEnabled();
    const bool hovered = enabled && wnd->GetHoveredItem() == item;
    if ( !selected && !hovered )
        return;

    // As in Office, the items are marked with a thick inner frame, which is
    // outlined for the selected item. The frame must fit into the bitmap
    // padding to avoid covering the item bitmap.
    MSOPainter painter(dc, m_highlight_border_colour);
    const wxColour& frame = selected && hovered
                                ? m_gallery_button_active_background_colour
                                : m_gallery_button_hover_background_colour;
    if ( selected )
        painter.RoundedRect(rect, 0, frame,
                            enabled ? m_gallery_item_border_pen.GetColour()
                                    : m_gallery_button_disabled_face_colour);
    else
        painter.Highlight(rect, 0, frame);

    const int padding = wxMin(
        wxMin(GetMetric(wxRIBBON_ART_GALLERY_BITMAP_PADDING_LEFT_SIZE),
              GetMetric(wxRIBBON_ART_GALLERY_BITMAP_PADDING_RIGHT_SIZE)),
        wxMin(GetMetric(wxRIBBON_ART_GALLERY_BITMAP_PADDING_TOP_SIZE),
              GetMetric(wxRIBBON_ART_GALLERY_BITMAP_PADDING_BOTTOM_SIZE)));
    wxRect inside(rect);
    inside.Deflate(wxMax(1, wxMin(dc.FromDIP(GALLERY_ITEM_FRAME_WIDTH), padding)));
    painter.RoundedRect(inside, 0, m_page_background_colour);
}

// ----------------------------------------------------------------------------
// Button bars
// ----------------------------------------------------------------------------

void wxRibbonMSOArtProvider::DrawButtonBarBackground(
                        wxDC& dc,
                        wxWindow* wnd,
                        const wxRect& rect)
{
    FillPageBackground(dc, wnd, rect);
}

bool wxRibbonMSOArtProvider::GetButtonBarButtonSize(
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
                        wxRect* dropdown_region)
{
    UpdateDPIScale(dc, wnd);

    const int drop_width = dc.FromDIP(DROPDOWN_WIDTH);
    const int pad = dc.FromDIP(SMALL_BUTTON_PADDING);

    switch ( size & wxRIBBON_BUTTONBAR_BUTTON_SIZE_MASK )
    {
        case wxRIBBON_BUTTONBAR_BUTTON_SMALL:
        case wxRIBBON_BUTTONBAR_BUTTON_MEDIUM:
            {
                // Small bitmap, possibly with the label to the right
                wxSize main_size = bitmap_size_small + wxSize(2 * pad, 2 * pad);
                if ( (size & wxRIBBON_BUTTONBAR_BUTTON_SIZE_MASK) ==
                        wxRIBBON_BUTTONBAR_BUTTON_MEDIUM )
                {
                    int text_size = GetTextSize(dc, label,
                                                m_button_bar_label_font).GetWidth();
                    if ( text_size < text_min_width )
                        text_size = text_min_width;
                    main_size.x += text_size + pad;
                }

                switch ( kind )
                {
                    case wxRIBBON_BUTTON_NORMAL:
                    case wxRIBBON_BUTTON_TOGGLE:
                        *button_size = main_size;
                        *normal_region = wxRect(main_size);
                        *dropdown_region = wxRect(0, 0, 0, 0);
                        break;
                    case wxRIBBON_BUTTON_DROPDOWN:
                        *button_size = main_size + wxSize(drop_width, 0);
                        *normal_region = wxRect(0, 0, 0, 0);
                        *dropdown_region = wxRect(*button_size);
                        break;
                    case wxRIBBON_BUTTON_HYBRID:
                        *button_size = main_size + wxSize(drop_width, 0);
                        *normal_region = wxRect(main_size);
                        *dropdown_region = wxRect(main_size.x, 0,
                            drop_width, main_size.y);
                        break;
                }
            }
            break;

        case wxRIBBON_BUTTONBAR_BUTTON_LARGE:
            {
                // Large bitmap, with label below (possibly split over 2 lines)
                const wxCoord best_width =
                    GetButtonBarButtonTextWidth(dc, label, kind, size);

                const int width = wxMax(bitmap_size_large.x,
                                        wxMax(best_width, text_min_width)) +
                                  2 * dc.FromDIP(LARGE_BUTTON_SIDE_PADDING);
                const int top_height = dc.FromDIP(LARGE_BUTTON_TOP_PADDING) +
                                       bitmap_size_large.y +
                                       dc.FromDIP(LARGE_BUTTON_ICON_GAP);

                *button_size = wxSize(width, top_height +
                    GetLargeButtonLabelAreaHeight(dc, m_button_bar_label_font));

                switch ( kind )
                {
                    case wxRIBBON_BUTTON_NORMAL:
                    case wxRIBBON_BUTTON_TOGGLE:
                        *normal_region = wxRect(*button_size);
                        *dropdown_region = wxRect(0, 0, 0, 0);
                        break;
                    case wxRIBBON_BUTTON_DROPDOWN:
                        *normal_region = wxRect(0, 0, 0, 0);
                        *dropdown_region = wxRect(*button_size);
                        break;
                    case wxRIBBON_BUTTON_HYBRID:
                        *normal_region = wxRect(0, 0, width, top_height);
                        *dropdown_region = wxRect(0, top_height, width,
                            button_size->y - top_height);
                        break;
                }
            }
            break;
    }

    return true;
}

wxCoord wxRibbonMSOArtProvider::GetButtonBarButtonTextWidth(
                        wxReadOnlyDC& dc, const wxString& label,
                        wxRibbonButtonKind kind,
                        wxRibbonButtonBarButtonState size)
{
    const auto text_width = [&dc, this](const wxString& text)
    {
        return GetTextSize(dc, text, m_button_bar_label_font).GetWidth();
    };

    switch ( size & wxRIBBON_BUTTONBAR_BUTTON_SIZE_MASK )
    {
        case wxRIBBON_BUTTONBAR_BUTTON_LARGE:
            {
                wxCoord best_width = text_width(label);
                const int last_line_extra_width =
                    kind == wxRIBBON_BUTTON_NORMAL || kind == wxRIBBON_BUTTON_TOGGLE
                        ? 0
                        : dc.FromDIP(CHEVRON_EXTRA_WIDTH);
                for ( size_t i = 0; i < label.Len(); ++i )
                {
                    if ( wxRibbonCanLabelBreakAtPosition(label, i) )
                    {
                        const int width = wxMax(
                            text_width(label.Left(i)),
                            text_width(label.Mid(i + 1)) +
                                last_line_extra_width);
                        if ( width < best_width )
                            best_width = width;
                    }
                }
                return best_width;
            }

        case wxRIBBON_BUTTONBAR_BUTTON_MEDIUM:
            return text_width(label);
    }

    return 0;
}

void wxRibbonMSOArtProvider::DrawButtonBarButton(
                        wxDC& dc,
                        wxWindow* wnd,
                        const wxRect& rect,
                        wxRibbonButtonKind kind,
                        long state,
                        const wxString& label,
                        const wxBitmap& bitmap_large,
                        const wxBitmap& bitmap_small)
{
    if ( kind == wxRIBBON_BUTTON_TOGGLE )
        kind = wxRIBBON_BUTTON_NORMAL;

    // Disabled buttons don't react to the mouse, so don't highlight them.
    const bool disabled = (state & wxRIBBON_BUTTONBAR_BUTTON_DISABLED) != 0;
    const bool toggled = (state & wxRIBBON_BUTTONBAR_BUTTON_TOGGLED) != 0;
    const bool hovered = !disabled &&
                         (state & wxRIBBON_BUTTONBAR_BUTTON_HOVER_MASK) != 0;
    const bool active = !disabled &&
                        (state & wxRIBBON_BUTTONBAR_BUTTON_ACTIVE_MASK) != 0;
    const double radius = dc.FromDIP(CORNER_RADIUS);

    if ( toggled )
    {
        wxColour background = m_button_bar_active_background_top_colour;
        if ( active )
            background = m_button_bar_active_background_colour;
        else if ( hovered )
            background = Blend(background, m_button_bar_active_background_colour,
                               MIX_HALFWAY);

        MSOPainter painter(dc, m_highlight_border_colour);
        painter.RoundedRect(rect, radius, background,
                            m_button_bar_active_border_pen.GetColour());
    }
    else if ( hovered || active )
    {
        const wxColour& highlight = active ? m_button_bar_active_background_colour
                                           : m_button_bar_hover_background_colour;

        if ( kind == wxRIBBON_BUTTON_HYBRID )
        {
            // Determine the part under the mouse.
            wxRect normal_part(rect), dropdown_part(rect);
            const bool large = (state & wxRIBBON_BUTTONBAR_BUTTON_SIZE_MASK) ==
                                    wxRIBBON_BUTTONBAR_BUTTON_LARGE;
            if ( large )
            {
                // The normal part is above the label area, as in
                // GetButtonBarButtonSize().
                normal_part.height = rect.height -
                    GetLargeButtonLabelAreaHeight(dc, m_button_bar_label_font);
                dropdown_part.y += normal_part.height;
                dropdown_part.height -= normal_part.height;
            }
            else
            {
                normal_part.width -= dc.FromDIP(DROPDOWN_WIDTH);
                dropdown_part.x += normal_part.width;
                dropdown_part.width -= normal_part.width;
            }

            const bool normal_highlighted =
                (state & (wxRIBBON_BUTTONBAR_BUTTON_NORMAL_HOVERED |
                          wxRIBBON_BUTTONBAR_BUTTON_NORMAL_ACTIVE)) != 0;

            {
                MSOPainter painter(dc, m_highlight_border_colour);
                painter.Highlight(rect, radius,
                    Blend(m_page_background_colour,
                          m_button_bar_hover_background_colour, MIX_HALFWAY));
            }
            {
                wxDCClipper clip(dc, normal_highlighted ? normal_part
                                                        : dropdown_part);
                MSOPainter painter(dc, m_highlight_border_colour);
                painter.Highlight(rect, radius, highlight);
            }

            wxDCPenChanger pen(dc, m_button_bar_hover_border_pen);
            const int inset = dc.FromDIP(SPLIT_LINE_INSET);
            if ( large )
            {
                dc.DrawLine(rect.x + inset, dropdown_part.y,
                            rect.GetRight() - inset + 1, dropdown_part.y);
            }
            else
            {
                dc.DrawLine(dropdown_part.x, rect.y + inset,
                            dropdown_part.x, rect.GetBottom() - inset + 1);
            }
        }
        else
        {
            MSOPainter painter(dc, m_highlight_border_colour);
            painter.Highlight(rect, radius, highlight);
        }
    }

    DrawButtonBarButtonContent(dc, wnd, rect, kind, state, label,
                               bitmap_large, bitmap_small);
}

void wxRibbonMSOArtProvider::DrawButtonBarButtonContent(
                        wxDC& dc,
                        wxWindow* WXUNUSED(wnd),
                        const wxRect& rect,
                        wxRibbonButtonKind kind,
                        long state,
                        const wxString& label,
                        const wxBitmap& bitmap_large,
                        const wxBitmap& bitmap_small)
{
    const wxColour colour = GetButtonBarLabelColour(state);
    const bool has_arrow = kind != wxRIBBON_BUTTON_NORMAL;

    wxDCFontChanger font_changer(dc, m_button_bar_label_font);
    wxDCTextColourChanger colour_changer(dc, colour);
    wxDCTextBgModeChanger mode_changer(dc, wxBRUSHSTYLE_TRANSPARENT);

    // Chevron is drawn at the end, after all the text.
    wxPoint2DDouble chevron;
    bool draw_chevron = false;

    switch ( state & wxRIBBON_BUTTONBAR_BUTTON_SIZE_MASK )
    {
        case wxRIBBON_BUTTONBAR_BUTTON_LARGE:
            {
                const int top = rect.y + dc.FromDIP(LARGE_BUTTON_TOP_PADDING);
                dc.DrawBitmap(bitmap_large,
                    rect.x + (rect.width - bitmap_large.GetLogicalWidth()) / 2,
                    top, true);

                int ypos = top + bitmap_large.GetLogicalHeight() +
                           dc.FromDIP(LARGE_BUTTON_ICON_GAP);
                const int avail = rect.width - 2 * dc.FromDIP(LARGE_BUTTON_SIDE_PADDING);
                const int label_height = GetLineHeight(dc, m_button_bar_label_font);

                wxCoord label_w, label_h;
                dc.GetTextExtent(label, &label_w, &label_h);
                if ( label_w <= avail )
                {
                    dc.DrawText(label, rect.x + (rect.width - label_w) / 2, ypos);
                    if ( has_arrow )
                    {
                        // Chevron on its own line below the label.
                        chevron = wxPoint2DDouble(rect.x + rect.width / 2.0,
                            ypos + label_height + label_height / 2.0);
                        draw_chevron = true;
                    }
                    break;
                }

                // Find the last position at which the label can be broken so
                // that its first line fits.
                bool drawn = false;
                for ( size_t breaki = label.Len(); breaki > 0; )
                {
                    --breaki;
                    if ( !wxRibbonCanLabelBreakAtPosition(label, breaki) )
                        continue;

                    const wxString label_top = label.Mid(0, breaki);
                    dc.GetTextExtent(label_top, &label_w, &label_h);
                    if ( label_w > avail )
                        continue;

                    dc.DrawText(label_top, rect.x + (rect.width - label_w) / 2, ypos);
                    ypos += label_height;

                    const int arrow_w = has_arrow ? dc.FromDIP(CHEVRON_EXTRA_WIDTH) : 0;
                    const wxString label_bottom =
                        FitLabel(dc, label.Mid(breaki + 1), avail - arrow_w);
                    dc.GetTextExtent(label_bottom, &label_w, &label_h);
                    const int line_w = label_w + arrow_w;

                    const int x = rect.x + (rect.width - line_w) / 2;
                    dc.DrawText(label_bottom, x, ypos);
                    if ( has_arrow )
                    {
                        chevron = wxPoint2DDouble(
                            x + line_w - dc.FromDIP(CHEVRON_EXTRA_WIDTH) / 2.0 +
                                dc.FromDIP(LARGE_BUTTON_CHEVRON_HORZ_OFFSET),
                            ypos + label_h / 2.0 +
                                dc.FromDIP(LARGE_BUTTON_CHEVRON_VERT_OFFSET));
                        draw_chevron = true;
                    }

                    drawn = true;
                    break;
                }

                if ( !drawn )
                {
                    // The label can't be broken so that its first line fits,
                    // show as much of it as fits on a single line.
                    const wxString fitted = FitLabel(dc, label, avail);
                    dc.GetTextExtent(fitted, &label_w, &label_h);
                    dc.DrawText(fitted, rect.x + (rect.width - label_w) / 2, ypos);
                    if ( has_arrow )
                    {
                        chevron = wxPoint2DDouble(rect.x + rect.width / 2.0,
                            ypos + label_height + label_height / 2.0);
                        draw_chevron = true;
                    }
                }
            }
            break;

        case wxRIBBON_BUTTONBAR_BUTTON_MEDIUM:
        case wxRIBBON_BUTTONBAR_BUTTON_SMALL:
            {
                const int pad = dc.FromDIP(SMALL_BUTTON_PADDING);
                int avail_width = rect.width;
                if ( has_arrow )
                {
                    avail_width -= dc.FromDIP(DROPDOWN_WIDTH);
                    chevron = wxPoint2DDouble(
                        rect.GetRight() + 1 - dc.FromDIP(DROPDOWN_WIDTH) / 2.0,
                        rect.y + rect.height / 2.0);
                    draw_chevron = true;
                }

                if ( (state & wxRIBBON_BUTTONBAR_BUTTON_SIZE_MASK) ==
                        wxRIBBON_BUTTONBAR_BUTTON_MEDIUM )
                {
                    int x = rect.x + pad;
                    dc.DrawBitmap(bitmap_small, x,
                        rect.y + (rect.height - bitmap_small.GetLogicalHeight()) / 2,
                        true);
                    x += bitmap_small.GetLogicalWidth() + pad;

                    const wxString fitted = FitLabel(dc, label,
                        rect.x + avail_width - pad - x);
                    const wxSize label_size = dc.GetTextExtent(fitted);
                    dc.DrawText(fitted, x, rect.y + (rect.height - label_size.y) / 2);
                }
                else
                {
                    dc.DrawBitmap(bitmap_small,
                        rect.x + (avail_width - bitmap_small.GetLogicalWidth()) / 2,
                        rect.y + (rect.height - bitmap_small.GetLogicalHeight()) / 2,
                        true);
                }
            }
            break;
    }

    if ( draw_chevron )
        DrawGlyph(dc, Glyph_ChevronDown, chevron, dc.FromDIP(SMALL_GLYPH_SIZE), colour);
}

// ----------------------------------------------------------------------------
// Tool bars
// ----------------------------------------------------------------------------

void wxRibbonMSOArtProvider::DrawToolBarBackground(
                        wxDC& dc,
                        wxWindow* wnd,
                        const wxRect& rect)
{
    FillPageBackground(dc, wnd, rect);
}

void wxRibbonMSOArtProvider::DrawToolGroupBackground(
                        wxDC& dc,
                        wxWindow* WXUNUSED(wnd),
                        const wxRect& rect)
{
    // Groups have no border, but are separated from the preceding group in
    // the same row by a vertical line drawn in the middle of the gap. The
    // first group of each row is at the start of the row, i.e. at 0, as
    // wxRibbonToolBar doesn't use any padding, and so is not preceded by any
    // gap.
    const int separation = GetMetric(wxRIBBON_ART_TOOL_GROUP_SEPARATION_SIZE);
    if ( rect.x <= separation )
        return;

    const int x = rect.x - (separation + 1) / 2;
    const int inset = dc.FromDIP(TOOL_GROUP_SEPARATOR_INSET);
    wxDCPenChanger pen(dc, m_toolbar_border_pen);
    dc.DrawLine(x, rect.y + inset, x, rect.GetBottom() - inset + 1);
}

wxSize wxRibbonMSOArtProvider::GetToolSize(
                        wxReadOnlyDC& dc,
                        wxWindow* wnd,
                        wxSize bitmap_size,
                        wxRibbonButtonKind kind,
                        bool WXUNUSED(is_first),
                        bool WXUNUSED(is_last),
                        wxRect* dropdown_region)
{
    UpdateDPIScale(dc, wnd);

    const int pad = dc.FromDIP(SMALL_BUTTON_PADDING);
    wxSize size = bitmap_size + wxSize(2 * pad, 2 * pad);

    if ( kind & wxRIBBON_BUTTON_DROPDOWN )
    {
        const int drop_width = dc.FromDIP(DROPDOWN_WIDTH);
        size.x += drop_width;
        if ( dropdown_region )
        {
            if ( kind == wxRIBBON_BUTTON_DROPDOWN )
                *dropdown_region = size;
            else
                *dropdown_region = wxRect(size.x - drop_width, 0,
                                          drop_width, size.y);
        }
    }
    else
    {
        if ( dropdown_region )
            *dropdown_region = wxRect(0, 0, 0, 0);
    }

    return size;
}

void wxRibbonMSOArtProvider::DrawTool(
                wxDC& dc,
                wxWindow* WXUNUSED(wnd),
                const wxRect& rect,
                const wxBitmap& bitmap,
                wxRibbonButtonKind kind,
                long state)
{
    // Disabled tools don't react to the mouse, so don't highlight them.
    const bool disabled = (state & wxRIBBON_TOOLBAR_TOOL_DISABLED) != 0;
    const bool toggled = kind == wxRIBBON_BUTTON_TOGGLE &&
                         (state & wxRIBBON_TOOLBAR_TOOL_TOGGLED);
    const bool hovered = !disabled &&
                         (state & wxRIBBON_TOOLBAR_TOOL_HOVER_MASK) != 0;
    const bool active = !disabled &&
                        (state & wxRIBBON_TOOLBAR_TOOL_ACTIVE_MASK) != 0;
    const double radius = dc.FromDIP(CORNER_RADIUS);
    const int drop_width = dc.FromDIP(DROPDOWN_WIDTH);

    wxRect bg_rect(rect);
    bg_rect.Deflate(dc.FromDIP(TOOL_HIGHLIGHT_INSET));

    if ( toggled )
    {
        wxColour background = m_tool_background_top_colour;
        if ( active )
            background = m_tool_active_background_colour;
        else if ( hovered )
            background = Blend(background, m_tool_active_background_colour,
                               MIX_HALFWAY);

        MSOPainter painter(dc, m_highlight_border_colour);
        painter.RoundedRect(bg_rect, radius, background,
                            m_button_bar_active_border_pen.GetColour());
    }
    else if ( hovered || active )
    {
        const wxColour& highlight = active ? m_tool_active_background_colour
                                           : m_tool_hover_background_colour;

        if ( kind == wxRIBBON_BUTTON_HYBRID )
        {
            wxRect normal_part(bg_rect), dropdown_part(bg_rect);
            normal_part.width -= drop_width;
            dropdown_part.x += normal_part.width;
            dropdown_part.width = drop_width;

            const bool dropdown_highlighted =
                (state & (wxRIBBON_TOOLBAR_TOOL_DROPDOWN_HOVERED |
                          wxRIBBON_TOOLBAR_TOOL_DROPDOWN_ACTIVE)) != 0;

            {
                MSOPainter painter(dc, m_highlight_border_colour);
                painter.Highlight(bg_rect, radius,
                    Blend(m_page_background_colour,
                          m_tool_hover_background_colour, MIX_HALFWAY));
            }
            {
                wxDCClipper clip(dc, dropdown_highlighted ? dropdown_part
                                                          : normal_part);
                MSOPainter painter(dc, m_highlight_border_colour);
                painter.Highlight(bg_rect, radius, highlight);
            }

            const int inset = dc.FromDIP(SPLIT_LINE_INSET);
            wxDCPenChanger pen(dc, m_toolbar_border_pen);
            dc.DrawLine(dropdown_part.x, bg_rect.y + inset,
                        dropdown_part.x, bg_rect.GetBottom() - inset + 1);
        }
        else
        {
            MSOPainter painter(dc, m_highlight_border_colour);
            painter.Highlight(bg_rect, radius, highlight);
        }
    }

    int avail_width = rect.width;
    if ( kind & wxRIBBON_BUTTON_DROPDOWN )
    {
        avail_width -= drop_width;

        DrawGlyph(dc, Glyph_ChevronDown,
                  wxPoint2DDouble(rect.x + avail_width + drop_width / 2.0,
                                  rect.y + rect.height / 2.0),
                  dc.FromDIP(SMALL_GLYPH_SIZE),
                  disabled
                    ? m_button_bar_label_disabled_colour
                    : m_tool_face_colour);
    }

    dc.DrawBitmap(bitmap,
        rect.x + (avail_width - bitmap.GetLogicalWidth()) / 2,
        rect.y + (rect.height - bitmap.GetLogicalHeight()) / 2, true);
}

// ----------------------------------------------------------------------------
// Toggle and help buttons
// ----------------------------------------------------------------------------

wxRect wxRibbonMSOArtProvider::GetBarToggleButtonArea(const wxRect& rect)
{
    // There is no space for the button when the tabs are not shown.
    if ( m_tab_ctrl_height <= TAB_CTRL_HEIGHT_WITHOUT_TABS )
        return wxRect();

    const int size = m_bar_button_size;
    const int margin = size / 4;
    int x = rect.GetWidth() - margin - size;
    if ( m_flags & wxRIBBON_BAR_SHOW_HELP_BUTTON )
        x -= size + margin / 2;

    const int y = m_tab_ctrl_height > size ? (m_tab_ctrl_height - size) / 2 : 1;
    return wxRect(x, y, size, size);
}

wxRect wxRibbonMSOArtProvider::GetRibbonHelpButtonArea(const wxRect& rect)
{
    // There is no space for the button when the tabs are not shown.
    if ( m_tab_ctrl_height <= TAB_CTRL_HEIGHT_WITHOUT_TABS )
        return wxRect();

    const int size = m_bar_button_size;
    const int margin = size / 4;
    const int y = m_tab_ctrl_height > size ? (m_tab_ctrl_height - size) / 2 : 1;
    return wxRect(rect.GetWidth() - margin - size, y, size, size);
}

void wxRibbonMSOArtProvider::DrawToggleButton(wxDC& dc,
                                              wxRibbonBar* wnd,
                                              const wxRect& rect,
                                              wxRibbonDisplayMode mode)
{
    if ( rect.IsEmpty() )
        return;

    wxDCClipper clip(dc, rect);

    FillRect(dc, rect, m_tab_ctrl_background_brush.GetColour());

    const bool hovered = wnd->IsToggleButtonHovered();

    if ( hovered )
    {
        MSOPainter painter(dc, m_highlight_border_colour);
        painter.Highlight(rect, dc.FromDIP(CORNER_RADIUS), m_ribbon_toggle_brush.GetColour());
    }

    Glyph glyph;
    switch ( mode )
    {
        case wxRIBBON_BAR_PINNED:
            // Collapse the ribbon.
            glyph = Glyph_ChevronUp;
            break;

        case wxRIBBON_BAR_MINIMIZED:
            // Expand the ribbon.
            glyph = Glyph_ChevronDown;
            break;

        case wxRIBBON_BAR_EXPANDED:
        default:
            // Pin the ribbon.
            glyph = Glyph_Pin;
            break;
    }

    DrawGlyph(dc, glyph,
              wxPoint2DDouble(rect.x + rect.width / 2.0, rect.y + rect.height / 2.0),
              dc.FromDIP(GLYPH_SIZE),
              hovered ? m_page_toggle_hover_face_colour
                      : m_page_toggle_face_colour);
}

void wxRibbonMSOArtProvider::DrawHelpButton(wxDC& dc,
                                            wxRibbonBar* wnd,
                                            const wxRect& rect)
{
    if ( rect.IsEmpty() )
        return;

    wxDCClipper clip(dc, rect);

    FillRect(dc, rect, m_tab_ctrl_background_brush.GetColour());

    const bool hovered = wnd->IsHelpButtonHovered();
    if ( hovered )
    {
        MSOPainter painter(dc, m_highlight_border_colour);
        painter.Highlight(rect, dc.FromDIP(CORNER_RADIUS),
                          m_ribbon_toggle_brush.GetColour());
    }

    DrawGlyph(dc, Glyph_Help,
              wxPoint2DDouble(rect.x + rect.width / 2.0, rect.y + rect.height / 2.0),
              dc.FromDIP(GLYPH_SIZE),
              hovered ? m_page_toggle_hover_face_colour
                      : m_page_toggle_face_colour);
}

// ----------------------------------------------------------------------------
// Key tips
// ----------------------------------------------------------------------------

bool wxRibbonMSOArtProvider::DrawKeyTip(wxDC& dc,
                                        wxWindow* wnd,
                                        const wxRect& target,
                                        const wxString& keytip)
{
    if ( keytip.empty() )
        return true;

    const wxRect rect = GetKeyTipArea(wnd, target);

    // Use the normal font size for the key tips, not the bigger tab font.
    wxDCFontChanger font_changer(dc, m_button_bar_label_font);
    const wxSize text_size = dc.GetTextExtent(keytip);

    // Don't let the badges of single narrow characters look too thin.
    wxSize badge_size(text_size.x + 2 * dc.FromDIP(KEY_TIP_HORZ_PADDING),
                      text_size.y + 2 * dc.FromDIP(KEY_TIP_VERT_PADDING));
    badge_size.x = wxMax(badge_size.x, badge_size.y);

    // Office puts the badges of large controls at the middle of their bottom
    // edge, while for the small buttons and tools, which are wide and low,
    // the badge is centred on their icon at their left side. The badges of
    // the narrow dropdown parts of the hybrid buttons and tools, which have
    // their own key tips, are wider than them, so start these badges a bit
    // after their left edge, to avoid covering or touching the badge of the
    // main part. As the badges are drawn in the window containing the target,
    // they must stay inside the target if possible, and are only put below it
    // otherwise, e.g. for the small panel extension buttons.
    const bool in_buttons = wxDynamicCast(wnd, wxRibbonButtonBar) ||
                                wxDynamicCast(wnd, wxRibbonToolBar);
    wxPoint pos;
    if ( in_buttons && rect.width >= 2 * rect.height )
        pos.x = rect.x + (rect.height - badge_size.x) / 2;
    else if ( 4 * rect.width < 3 * rect.height )
        pos.x = rect.x + dc.FromDIP(KEY_TIP_GAP);
    else
        pos.x = rect.x + (rect.width - badge_size.x) / 2;

    if ( badge_size.y <= rect.height )
        pos.y = rect.GetBottom() + 1 - badge_size.y;
    else
        pos.y = rect.GetBottom() + 1 + dc.FromDIP(KEY_TIP_GAP);

    if ( wnd )
    {
        const wxSize client = wnd->GetClientSize();
        pos.x = wxMax(0, wxMin(pos.x, client.x - badge_size.x));
        pos.y = wxMax(0, wxMin(pos.y, client.y - badge_size.y));
    }

    const wxRect badge(pos, badge_size);

    // Use the inverted colours of the page, which are readable in all the
    // colour schemes, including the high contrast ones.
    {
        MSOPainter painter(dc);
        painter.RoundedRect(badge, dc.FromDIP(KEY_TIP_RADIUS),
                            m_button_bar_label_colour);
    }

    wxDCTextColourChanger colour_changer(dc, m_page_background_colour);
    dc.DrawText(keytip, badge.x + (badge.width - text_size.x) / 2,
                badge.y + (badge.height - text_size.y) / 2);

    return true;
}

#endif // wxHAS_SVG && wxUSE_GRAPHICS_CONTEXT

#endif // wxUSE_RIBBON
