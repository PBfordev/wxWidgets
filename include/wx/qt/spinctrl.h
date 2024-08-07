/////////////////////////////////////////////////////////////////////////////
// Name:        wx/qt/spinctrl.h
// Author:      Peter Most, Mariano Reingart
// Copyright:   (c) 2010 wxWidgets dev team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_QT_SPINCTRL_H_
#define _WX_QT_SPINCTRL_H_

class QSpinBox;
class QDoubleSpinBox;

// Take advantage of the Qt compile time polymorphy and use a template to avoid
// copy&paste code for the usage of QSpinBox/QDoubleSpinBox.

template < typename T, typename Widget >
class WXDLLIMPEXP_CORE wxSpinCtrlQt : public wxSpinCtrlBase
{
public:
    wxSpinCtrlQt() = default;

    wxSpinCtrlQt( wxWindow *parent, wxWindowID id, const wxString& value,
        const wxPoint& pos, const wxSize& size, long style,
        T min, T max, T initial, T inc,
        const wxString& name );

    bool Create( wxWindow *parent, wxWindowID id, const wxString& value,
        const wxPoint& pos, const wxSize& size, long style,
        T min, T max, T initial, T inc,
        const wxString& name );

    virtual wxString GetTextValue() const override;
    virtual void SetValue(const wxString&) override {}

    virtual void SetSnapToTicks(bool snap_to_ticks) override;
    virtual bool GetSnapToTicks() const override;

    virtual void SetSelection(long from, long to) override;

    virtual void SetValue(T val);
    void SetRange(T minVal, T maxVal);
    void SetIncrement(T inc);

    T GetValue() const;
    T GetMin() const;
    T GetMax() const;
    T GetIncrement() const;

protected:
    // QSpinBox / QDoubleSpinBox
    Widget* GetQtSpinBox() const { return static_cast<Widget*>(this->GetHandle()); }

};

class WXDLLIMPEXP_CORE wxSpinCtrl : public wxSpinCtrlQt< int, QSpinBox >
{
    using BaseType = wxSpinCtrlQt< int, QSpinBox >;

public:
    wxSpinCtrl() = default;

    wxSpinCtrl(wxWindow *parent,
               wxWindowID id = wxID_ANY,
               const wxString& value = wxEmptyString,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = wxSP_ARROW_KEYS,
               int min = 0, int max = 100, int initial = 0,
               const wxString& name = wxT("wxSpinCtrl"));

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                const wxString& value = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxSP_ARROW_KEYS,
                int min = 0, int max = 100, int initial = 0,
                const wxString& name = wxT("wxSpinCtrl"));
    virtual int GetBase() const override { return m_base; }
    virtual bool SetBase(int base) override;
    virtual void SetValue(const wxString & val) override;
    virtual void SetValue(int val) override { BaseType::SetValue(val); }

private:
    int m_base = 10;

    wxDECLARE_DYNAMIC_CLASS(wxSpinCtrl);
};

class WXDLLIMPEXP_CORE wxSpinCtrlDouble : public wxSpinCtrlQt< double, QDoubleSpinBox >
{
    using BaseType = wxSpinCtrlQt< double, QDoubleSpinBox >;

public:
    wxSpinCtrlDouble() = default;

    wxSpinCtrlDouble(wxWindow *parent,
                     wxWindowID id = wxID_ANY,
                     const wxString& value = wxEmptyString,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     long style = wxSP_ARROW_KEYS,
                     double min = 0, double max = 100, double initial = 0,
                     double inc = 1,
                     const wxString& name = wxT("wxSpinCtrlDouble"));

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                const wxString& value = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxSP_ARROW_KEYS,
                double min = 0, double max = 100, double initial = 0,
                double inc = 1,
                const wxString& name = wxT("wxSpinCtrlDouble"));

    void SetDigits(unsigned digits);
    unsigned GetDigits() const;

    virtual int GetBase() const override { return 10; }
    virtual bool SetBase(int WXUNUSED(base)) override { return false; }
    virtual void SetValue(const wxString & val) override;
    virtual void SetValue(double val) override { BaseType::SetValue(val); }

private:
    wxDECLARE_DYNAMIC_CLASS( wxSpinCtrlDouble );
};

#endif // _WX_QT_SPINCTRL_H_
