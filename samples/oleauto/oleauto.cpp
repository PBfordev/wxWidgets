/////////////////////////////////////////////////////////////////////////////
// Name:        oleauto.cpp
// Purpose:     OLE Automation wxWidgets sample
// Author:      Julian Smart
// Created:     08/12/98
// Copyright:   (c) Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"


// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWidgets headers
#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif

#include "wx/msw/ole/automtn.h"

#ifndef __WINDOWS__
#error "Sorry, this sample works under Windows only."
#endif

// ----------------------------------------------------------------------------
// resources
// ----------------------------------------------------------------------------
// the application icon
#ifndef wxHAS_IMAGES_IN_RESOURCES
    #include "../sample.xpm"
#endif

// ----------------------------------------------------------------------------
// private classes
// ----------------------------------------------------------------------------

// Define a new application type, each program should derive a class from wxApp
class MyApp : public wxApp
{
public:
    // override base class virtuals
    // ----------------------------

    // this one is called on application startup and is a good place for the app
    // initialization (doing it here and not in the ctor allows to have an error
    // return: if OnInit() returns false, the application terminates)
    virtual bool OnInit() override;
};

// Define a new frame type: this is going to be our main frame
class MyFrame : public wxFrame
{
public:
    // ctor(s)
    MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size);

    // event handlers (these functions should _not_ be virtual)
    void OnQuit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnTest(wxCommandEvent& event);
    void OnTestNamedArgs(wxCommandEvent& event);
    void OnTestNamedArgsScripting(wxCommandEvent& event);

private:
    // any class wishing to process wxWidgets events must use this macro
    wxDECLARE_EVENT_TABLE();
};

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
    // menu items
    OleAuto_Quit = 1,
    OleAuto_About,
    OleAuto_Test,
    OleAuto_TestNamedArgs,
    OleAuto_TestNamedArgsScripting,

    // controls start here (the numbers are, of course, arbitrary)
    OleAuto_Text = 1000
};

// ----------------------------------------------------------------------------
// event tables and other macros for wxWidgets
// ----------------------------------------------------------------------------

// the event tables connect the wxWidgets events with the functions (event
// handlers) which process them. It can be also done at run-time, but for the
// simple menu events like this the static method is much simpler.
wxBEGIN_EVENT_TABLE(MyFrame, wxFrame)
    EVT_MENU(OleAuto_Quit,  MyFrame::OnQuit)
    EVT_MENU(OleAuto_About, MyFrame::OnAbout)
    EVT_MENU(OleAuto_Test, MyFrame::OnTest)
    EVT_MENU(OleAuto_TestNamedArgs, MyFrame::OnTestNamedArgs)
    EVT_MENU(OleAuto_TestNamedArgsScripting, MyFrame::OnTestNamedArgsScripting)
wxEND_EVENT_TABLE()

// Create a new application object: this macro will allow wxWidgets to create
// the application object during program execution (it's better than using a
// static object for many reasons) and also declares the accessor function
// wxGetApp() which will return the reference of the right type (i.e. MyApp and
// not wxApp)
wxIMPLEMENT_APP(MyApp);

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// the application class
// ----------------------------------------------------------------------------

// `Main program' equivalent: the program execution "starts" here
bool MyApp::OnInit()
{
    if ( !wxApp::OnInit() )
        return false;

    // Create the main application window
    MyFrame *frame = new MyFrame("OleAuto wxWidgets App",
                                 wxPoint(50, 50), wxSize(450, 340));

    // Show it
    frame->Show(true);

    // success: wxApp::OnRun() will be called which will enter the main message
    // loop and the application will run. If we returned false here, the
    // application would exit immediately.
    return true;
}

// ----------------------------------------------------------------------------
// main frame
// ----------------------------------------------------------------------------

// frame constructor
MyFrame::MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
       : wxFrame(nullptr, wxID_ANY, title, pos, size)
{
    // set the frame icon
    SetIcon(wxICON(sample));

    // create a menu bar
    wxMenu *menuFile = new wxMenu;

    menuFile->Append(OleAuto_Test, "&Test Excel Automation...");
    menuFile->Append(OleAuto_TestNamedArgs, "Test Named Arguments (&Excel)");
    menuFile->Append(OleAuto_TestNamedArgsScripting, "Test Named Arguments (&Scripting Runtime)");
    menuFile->Append(OleAuto_About, "&About");
    menuFile->AppendSeparator();
    menuFile->Append(OleAuto_Quit, "E&xit");

    // now append the freshly created menu to the menu bar...
    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append(menuFile, "&File");

    // ... and attach this menu bar to the frame
    SetMenuBar(menuBar);

#if wxUSE_STATUSBAR
    // create a status bar just for fun (by default with 1 pane only)
    CreateStatusBar(2);
    SetStatusText("Welcome to wxWidgets!");
#endif // wxUSE_STATUSBAR
}


// event handlers

void MyFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
    // true is to force the frame to close
    Close(true);
}

void MyFrame::OnAbout(wxCommandEvent& WXUNUSED(event))
{
    wxMessageBox("This is an OLE Automation sample",
                 "About OleAuto", wxOK | wxICON_INFORMATION, this);
}

/* Tests OLE automation by making the active Excel cell bold,
 * and changing the text.
 */
void MyFrame::OnTest(wxCommandEvent& WXUNUSED(event))
{
    wxMessageBox("Excel will be started if it is not running after you have pressed OK button."
        "\nThe active cell should then say 'wxWidgets automation test!' in bold.",
        "Excel start");

    wxAutomationObject excelObject;
    if ( !excelObject.GetInstance("Excel.Application") )
    {
        wxLogError("Could not create Excel object.");
        return;
    }

    // Ensure that Excel is visible
    if (!excelObject.PutProperty("Visible", true))
    {
        wxLogError("Could not make Excel object visible");
    }
    const wxVariant workbooksCountVariant = excelObject.GetProperty("Workbooks.Count");
    if (workbooksCountVariant.IsNull())
    {
        wxLogError("Could not get workbooks count");
        return;
    }
    const long workbooksCount = workbooksCountVariant;
    if (workbooksCount == 0)
    {
        const wxVariant workbook = excelObject.CallMethod("Workbooks.Add");
        if (workbook.IsNull())
        {
            wxLogError("Could not create new Workbook");
            return;
        }
    }

    if (!excelObject.PutProperty("ActiveCell.Value", "wxWidgets automation test!"))
    {
        wxLogError("Could not set active cell value.");
        return;
    }
    if (!excelObject.PutProperty("ActiveCell.Font.Bold", wxVariant(true)) )
    {
        wxLogError("Could not put Bold property to active cell.");
        return;
    }
}

void MyFrame::OnTestNamedArgs(wxCommandEvent& WXUNUSED(event))
{
    wxAutomationObject excelObject;
    if ( !excelObject.GetInstance("Excel.Application") )
    {
        wxLogError("Could not create Excel object.");
        return;
    }

    // Application.ConvertFormula(Formula, FromReferenceStyle, ToReferenceStyle)
    // converts "=A1" to "=RC" when called correctly.
    const long xlA1 = 1, xlR1C1 = -4150;
    wxString allPositional, allNamed, positionalAndNamed;
    bool putUnnamed, putNamed;

    {
        wxLogNull noLog; // avoid message boxes from failed calls

        allPositional = excelObject.CallMethod("ConvertFormula",
                            "=A1", xlA1, xlR1C1).MakeString();

        allNamed = excelObject.CallMethod("ConvertFormula",
                            wxVariant("=A1", "Formula"),
                            wxVariant(xlA1, "FromReferenceStyle"),
                            wxVariant(xlR1C1, "ToReferenceStyle")).MakeString();

        // Bug: fails, the name of the last argument is not passed to
        // GetIDsOfNames() when it is preceded by a positional argument.
        positionalAndNamed = excelObject.CallMethod("ConvertFormula",
                            "=A1", xlA1,
                            wxVariant(xlR1C1, "ToReferenceStyle")).MakeString();

        putUnnamed = excelObject.PutProperty("Visible", wxVariant(true));

        // Bug: fails, the value name is passed to GetIDsOfNames() as if it
        // were a parameter name.
        putNamed = excelObject.PutProperty("Visible", wxVariant(true, "Visible"));
    }

    wxLogMessage("ConvertFormula, all positional: '%s'\n"
                 "ConvertFormula, all named: '%s'\n"
                 "ConvertFormula, positional followed by named: '%s'\n"
                 "PutProperty, unnamed value: %s\n"
                 "PutProperty, named value: %s",
                 allPositional, allNamed, positionalAndNamed,
                 putUnnamed ? "OK" : "FAILED",
                 putNamed ? "OK" : "FAILED");
}

// Same as OnTestNamedArgs() but using Microsoft Scripting Runtime objects
// which are part of Windows, so Excel is not needed.
void MyFrame::OnTestNamedArgsScripting(wxCommandEvent& WXUNUSED(event))
{
    wxAutomationObject fso, dict;
    if ( !fso.CreateInstance("Scripting.FileSystemObject")
         || !dict.CreateInstance("Scripting.Dictionary") )
    {
        wxLogError("Could not create Scripting Runtime objects.");
        return;
    }

    // FileSystemObject.BuildPath(Path, Name) returns "C:\Dir\File.txt" here.
    wxString allPositional, allNamed, positionalAndNamed;
    bool putUnnamed, putNamed;

    {
        wxLogNull noLog; // avoid message boxes from failed calls

        allPositional = fso.CallMethod("BuildPath",
                            "C:\\Dir", "File.txt").MakeString();

        allNamed = fso.CallMethod("BuildPath",
                            wxVariant("C:\\Dir", "Path"),
                            wxVariant("File.txt", "Name")).MakeString();

        // Bug: fails, the name of the last argument is not passed to
        // GetIDsOfNames() when it is preceded by a positional argument.
        positionalAndNamed = fso.CallMethod("BuildPath",
                            "C:\\Dir",
                            wxVariant("File.txt", "Name")).MakeString();

        // Dictionary.CompareMode can be changed while the dictionary is empty.
        putUnnamed = dict.PutProperty("CompareMode", wxVariant(1L));

        // Bug: fails, the value name is passed to GetIDsOfNames() as if it
        // were a parameter name.
        putNamed = dict.PutProperty("CompareMode", wxVariant(0L, "CompareMode"));
    }

    wxLogMessage("BuildPath, all positional: '%s'\n"
                 "BuildPath, all named: '%s'\n"
                 "BuildPath, positional followed by named: '%s'\n"
                 "PutProperty, unnamed value: %s\n"
                 "PutProperty, named value: %s",
                 allPositional, allNamed, positionalAndNamed,
                 putUnnamed ? "OK" : "FAILED",
                 putNamed ? "OK" : "FAILED");
}
