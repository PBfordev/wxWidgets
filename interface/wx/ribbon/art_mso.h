///////////////////////////////////////////////////////////////////////////////
// Name:        ribbon/art_mso.h
// Purpose:     interface of wxRibbonMSOArtProvider
// Author:      wxWidgets team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

/**
    @class wxRibbonMSOArtProvider

    An art provider which draws a ribbon looking like the one used by the
    current versions of Microsoft Office (Office 365).

    The tabs have no borders and the active one is indicated by an accent
    coloured underline below its label, which becomes almost as wide as the
    tab when it is hovered, while the other tab under the mouse gets a grey
    underline. The tabs of the highlighted pages (see
    wxRibbonBar::AddPageHighlight()) don't have any background either, but
    their labels use the accent colour. The page is drawn as a "card" with
    rounded corners, panels are separated by thin vertical lines and have
    their labels at the bottom, and buttons and tools only get a rounded
    background when they are hovered, pressed or toggled. As in Office, the
    tab labels are just cut off, without an ellipsis, when there is not enough
    space for them. The key tips are shown as small rounded badges using the
    text colour as background, placed at the bottom of large buttons and on
    the icon of small buttons and tools.

    Both light and dark appearances are supported: the default colour scheme
    is chosen depending on wxSystemAppearance::IsDark() and is updated when
    the system colours change. The default scheme uses the accent colour
    selected in the system settings, unless this is disabled with
    UseSystemAccentColour(). Under MSW, only the system colours are used when
    a high contrast theme is active.

    Notice that the colours are recomputed when the system colours change,
    e.g. when switching between the light and dark appearances, so the
    colours set with SetColourScheme() and SetColour() are lost then. To
    customize the colours in a way that persists, and possibly differently
    for the light and dark appearances, use SetAppearanceColourScheme() and
    SetAppearanceColour() instead, e.g.:
    @code
    auto art = new wxRibbonMSOArtProvider;
    using Appearance = wxRibbonMSOArtProvider::Appearance;

    // Use a green accent colour with the default background and text.
    art->SetAppearanceColourScheme(Appearance::Light, wxNullColour, wxColour(0x107c41));
    art->SetAppearanceColourScheme(Appearance::Dark, wxNullColour, wxColour(0x6ccb5f));

    // And lighter labels of disabled buttons in the dark appearance.
    art->SetAppearanceColour(Appearance::Dark,
                             wxRIBBON_ART_BUTTON_BAR_LABEL_DISABLED_COLOUR,
                             wxColour(0x8a8a8a));

    ribbon->SetArtProvider(art);
    @endcode

    The colours passed to SetColourScheme() are interpreted as follows:
    - The primary colour is the background of the tab area. The page itself
      uses a slightly lighter shade of it and its luminance determines whether
      a light or dark appearance is used.
    - The secondary colour is the accent colour used for the active tab
      underline and the labels of the highlighted tabs.
    - The tertiary colour is the text colour. It is only used if it contrasts
      sufficiently with the background, otherwise the default dark or light
      text colour is used.

    As this provider doesn't use gradients and borders in the same way as
    the other ones, some of the colours settable with SetColour() have a
    different meaning for it:
    - @c wxRIBBON_ART_TAB_BORDER_COLOUR is the active tab underline colour.
    - @c wxRIBBON_ART_TAB_HIGHLIGHT_COLOUR is the label colour of the
      highlighted tabs.
    - @c wxRIBBON_ART_TAB_HOVER_BACKGROUND_GRADIENT_COLOUR is the hovered
      tab underline colour.
    - @c wxRIBBON_ART_BUTTON_BAR_ACTIVE_BACKGROUND_TOP_COLOUR and
      @c wxRIBBON_ART_TOOL_BACKGROUND_TOP_COLOUR are the backgrounds of the
      toggled buttons and tools.
    - @c wxRIBBON_ART_GALLERY_ITEM_BORDER_COLOUR is used for both the border
      of the selected gallery item and the border of the gallery buttons.

    Unlike with the other providers, the size metrics (i.e. the values with
    the @c _SIZE suffix) are in DIPs: SetMetric() takes and GetMetric()
    returns the value in pixels for the DPI of the window most recently laid
    out using this provider, but it is stored in DIPs, so that it remains
    correct after a DPI change. Before any window is laid out, the standard
    DPI is used, i.e. the values are the same in pixels and DIPs. Panels
    expanded in a popup don't change the DPI used, so the metrics returned
    while laying them out are for the DPI of the ribbon bar even if the popup
    is shown on a display with a different DPI. Because of rounding, the value
    returned by GetMetric() may differ by one from the value passed to
    SetMetric().

    Notice that this provider draws the tab of the backstage page (see
    wxRibbonBar::SetBackstagePage()) like any other tab only if the special
    styling of this tab is turned off using wxRibbonBar::StyleBackstageTab(),
    which is recommended to get an appearance closer to Microsoft Office.

    This class is declared in the @c wx/ribbon/art_mso.h header and is only
    available if wxWidgets was built with SVG support, i.e. when @c wxHAS_SVG
    is defined, as it uses SVG images for drawing its glyphs, and with
    @c wxUSE_GRAPHICS_CONTEXT set to 1, as it uses wxGraphicsContext for
    drawing antialiased shapes.

    @since 3.3.5

    @library{wxribbon}
    @category{ribbon}

    @see wxRibbonMSWFlatArtProvider, wxRibbonMSWArtProvider
*/
class wxRibbonMSOArtProvider : public wxRibbonMSWFlatArtProvider
{
public:
    /**
        Default constructor.

        Initializes the provider with the default colour scheme corresponding
        to the current system appearance.
    */
    wxRibbonMSOArtProvider();

    /**
        Set whether the system accent colour is used in the default colour
        scheme.

        By default, the accent colour chosen by the user in the system
        settings is used as the secondary colour of the default colour scheme
        if it is available and contrasts sufficiently with the background.
        Under the platforms other than MSW, which don't provide the accent
        colour directly, the system selection colour (see
        wxSYS_COLOUR_HIGHLIGHT) is used instead, as it normally follows the
        accent colour.
        Calling this function with @false makes the provider always use the
        blue accent colour of Microsoft Office instead.

        The system accent colour is not used for the appearances for which
        a secondary colour was set with SetAppearanceColourScheme().

        Notice that this function recomputes all the colours, exactly like
        UpdateColoursFromSystem() does, so the colours set with
        SetColourScheme() or SetColour() are lost.

        @see IsUsingSystemAccentColour()
    */
    void UseSystemAccentColour(bool use = true);

    /**
        Return @true if the system accent colour is used in the default colour
        scheme.

        @see UseSystemAccentColour()
    */
    bool IsUsingSystemAccentColour() const;

    /**
        The appearance to which the colours set by SetAppearanceColourScheme()
        and SetAppearanceColour() apply.

        The appearance is determined by wxSystemAppearance::IsDark(), so it
        follows the appearance selected with wxApp::SetAppearance() too.
    */
    enum class Appearance
    {
        Light,  ///< Light appearance.
        Dark    ///< Dark appearance.
    };

    /**
        Set the colour scheme used for the given appearance.

        The colours have the same meaning as for SetColourScheme(), but
        unlike the colours set by it, they are used every time the colours
        are recomputed when the system colours change, as long as the given
        appearance is used. This allows using different colours for the light
        and dark appearances.

        Any of the colours may be invalid, in which case the default colour
        is used for it, so that it is possible to change e.g. only the accent
        colour. The default text and accent colours are chosen to suit the
        background, i.e. their light variants are used for a dark background
        even when the appearance is light, and vice versa. If the secondary
        colour is invalid, the system accent colour is used for it if
        enabled, see UseSystemAccentColour(). Calling this function with all
        the colours invalid restores the default colour scheme for the given
        appearance.

        If the given appearance is the current one, all the colours are
        recomputed immediately, like by UpdateColoursFromSystem(). As with
        SetColourScheme(), the ribbon bar must be refreshed by the caller to
        show the new colours.

        The colours set by this function are not used when a high contrast
        theme is active under MSW.

        @see GetAppearanceColourScheme(), SetAppearanceColour(),
            ResetAppearanceColours()
    */
    void SetAppearanceColourScheme(Appearance appearance,
                                   const wxColour& primary,
                                   const wxColour& secondary = wxNullColour,
                                   const wxColour& tertiary = wxNullColour);

    /**
        Get the colour scheme set for the given appearance.

        Returns the colours passed to SetAppearanceColourScheme(), which are
        invalid if they were not set. Use GetColourScheme() to get the colours
        actually used.

        Any of the pointers may be null if the corresponding colour is not
        needed.
    */
    void GetAppearanceColourScheme(Appearance appearance,
                                   wxColour* primary,
                                   wxColour* secondary,
                                   wxColour* tertiary) const;

    /**
        Set an individual colour used for the given appearance.

        The @a id is one of the @c wxRIBBON_ART_XXX_COLOUR values accepted by
        SetColour(). Unlike the colours set by SetColour(), the colour set by
        this function overrides the one computed from the colour scheme every
        time the colours are recomputed, as long as the given appearance is
        used.

        If the given appearance is the current one, the colour is used
        immediately. Passing an invalid colour removes the override, and the
        colours are then recomputed immediately if the appearance is the
        current one.

        @see GetAppearanceColour(), SetAppearanceColourScheme(),
            ResetAppearanceColours()
    */
    void SetAppearanceColour(Appearance appearance,
                             int id,
                             const wxColour& colour);

    /**
        Get an individual colour set for the given appearance.

        Returns the colour passed to SetAppearanceColour() or an invalid
        colour if none was set. Use GetColour() to get the colour actually
        used.
    */
    wxColour GetAppearanceColour(Appearance appearance, int id) const;

    /**
        Forget all the colours set for the given appearance.

        Resets the colour scheme set by SetAppearanceColourScheme() and all
        the individual colours set by SetAppearanceColour(), so that the
        default colours are used for the given appearance again. If the
        appearance is the current one, the colours are recomputed
        immediately.
    */
    void ResetAppearanceColours(Appearance appearance);
};
