/////////////////////////////////////////////////////////////////////////////
// Name:        src/html/m_fonts.cpp
// Purpose:     wxHtml module for fonts & colors of fonts
// Author:      Vaclav Slavik
// Copyright:   (c) 1999 Vaclav Slavik
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"


#if wxUSE_HTML && wxUSE_STREAMS

#ifndef WX_PRECOMP
    #include "wx/brush.h"
#endif

#include "wx/html/forcelnk.h"
#include "wx/html/m_templ.h"
#include "wx/fontenum.h"
#include "wx/tokenzr.h"
#include "wx/html/styleparams.h"

FORCE_LINK_ME(m_fonts)


TAG_HANDLER_BEGIN(FONT, "FONT" )

    TAG_HANDLER_VARS
        wxArrayString m_Faces;

    TAG_HANDLER_CONSTR(FONT) { }

    TAG_HANDLER_PROC(tag)
    {
        wxColour oldclr = m_WParser->GetActualColor();
        wxColour oldbackclr = m_WParser->GetActualBackgroundColor();
        int oldbackmode = m_WParser->GetActualBackgroundMode();
        int oldsize = m_WParser->GetFontSize();
        int oldbold = m_WParser->GetFontBold();
        int olditalic = m_WParser->GetFontItalic();
        int oldunderlined = m_WParser->GetFontUnderlined();
        wxString oldface = m_WParser->GetFontFace();

        // Load any style parameters
        wxHtmlStyleParams styleParams(tag);

        ApplyStyle(styleParams);

        {
            wxColour clr;
            if (tag.GetParamAsColour(wxT("COLOR"), &clr))
            {
                m_WParser->SetActualColor(clr);
                m_WParser->GetContainer()->InsertCell(new wxHtmlColourCell(clr));
            }
            if (tag.GetParamAsColour(wxT("BGCOLOR"), &clr))
            {
                m_WParser->SetActualBackgroundColor(clr);
                m_WParser->GetContainer()->InsertCell(new wxHtmlColourCell(clr, wxHTML_CLR_BACKGROUND));
            }
        }

        {
            long tmp = 0;
            wxString sizeStr;
            if (tag.GetParamAsString(wxT("SIZE"), &sizeStr) && sizeStr.ToLong(&tmp))
            {
                wxUniChar c = sizeStr[0];
                if (c == wxT('+') || c == wxT('-'))
                    m_WParser->SetFontSize(oldsize+tmp);
                else
                    m_WParser->SetFontSize(tmp);
                m_WParser->GetContainer()->InsertCell(
                    new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
            }
        }

        wxString faces;
        if (tag.GetParamAsString(wxT("FACE"), &faces))
        {
            if (m_Faces.GetCount() == 0)
                m_Faces = wxFontEnumerator::GetFacenames();

            wxStringTokenizer tk(faces, wxT(","));

            while (tk.HasMoreTokens())
            {
                int index;
                if ((index = m_Faces.Index(tk.GetNextToken(), false)) != wxNOT_FOUND)
                {
                    m_WParser->SetFontFace(m_Faces[index]);
                    m_WParser->GetContainer()->InsertCell(new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
                    break;
                }
            }
        }

        ParseInner(tag);

        if ((oldface       != m_WParser->GetFontFace()) ||
            (oldunderlined != m_WParser->GetFontUnderlined()) ||
            (olditalic     != m_WParser->GetFontItalic()) ||
            (oldbold       != m_WParser->GetFontBold()) ||
            (oldsize       != m_WParser->GetFontSize()))
        {

            if (oldface != m_WParser->GetFontFace())
            {
                m_WParser->SetFontFace(oldface);
            }
            if (oldunderlined != m_WParser->GetFontUnderlined())
            {
                m_WParser->SetFontUnderlined(oldunderlined);
            }
            if (olditalic != m_WParser->GetFontItalic())
            {
                m_WParser->SetFontItalic(olditalic);
            }
            if (oldbold != m_WParser->GetFontBold())
            {
                m_WParser->SetFontBold(oldbold);
            }
            if (oldsize != m_WParser->GetFontSize())
            {
                m_WParser->SetFontSize(oldsize);
            }
            m_WParser->GetContainer()->InsertCell(new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        }
        if (oldclr != m_WParser->GetActualColor())
        {
            m_WParser->SetActualColor(oldclr);
            m_WParser->GetContainer()->InsertCell(new wxHtmlColourCell(oldclr));
        }
        if (oldbackmode != m_WParser->GetActualBackgroundMode() ||
            oldbackclr != m_WParser->GetActualBackgroundColor())
        {
            m_WParser->SetActualBackgroundMode(oldbackmode);
            m_WParser->SetActualBackgroundColor(oldbackclr);
            m_WParser->GetContainer()->InsertCell(
                new wxHtmlColourCell(oldbackclr, oldbackmode == wxBRUSHSTYLE_TRANSPARENT ? wxHTML_CLR_TRANSPARENT_BACKGROUND : wxHTML_CLR_BACKGROUND));
        }

        return true;
    }

TAG_HANDLER_END(FONT)


TAG_HANDLER_BEGIN(FACES_U, "U,STRIKE,DEL")

    TAG_HANDLER_CONSTR(FACES_U) { }

    TAG_HANDLER_PROC(tag)
    {
        int underlined = m_WParser->GetFontUnderlined();

        m_WParser->SetFontUnderlined(true);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));

        ParseInner(tag);

        m_WParser->SetFontUnderlined(underlined);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        return true;
    }

TAG_HANDLER_END(FACES_U)




TAG_HANDLER_BEGIN(FACES_B, "B,STRONG")
    TAG_HANDLER_CONSTR(FACES_B) { }

    TAG_HANDLER_PROC(tag)
    {
        int bold = m_WParser->GetFontBold();

        m_WParser->SetFontBold(true);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));

        ParseInner(tag);

        m_WParser->SetFontBold(bold);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        return true;
    }

TAG_HANDLER_END(FACES_B)




TAG_HANDLER_BEGIN(FACES_I, "I,EM,CITE,ADDRESS")
    TAG_HANDLER_CONSTR(FACES_I) { }

    TAG_HANDLER_PROC(tag)
    {
        int italic = m_WParser->GetFontItalic();

        m_WParser->SetFontItalic(true);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));

        ParseInner(tag);

        m_WParser->SetFontItalic(italic);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        return true;
    }

TAG_HANDLER_END(FACES_I)




TAG_HANDLER_BEGIN(FACES_TT, "TT,CODE,KBD,SAMP")
    TAG_HANDLER_CONSTR(FACES_TT) { }

    TAG_HANDLER_PROC(tag)
    {
        int fixed = m_WParser->GetFontFixed();

        m_WParser->SetFontFixed(true);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));

        ParseInner(tag);

        m_WParser->SetFontFixed(fixed);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        return true;
    }

TAG_HANDLER_END(FACES_TT)





TAG_HANDLER_BEGIN(Hx, "H1,H2,H3,H4,H5,H6")
    TAG_HANDLER_CONSTR(Hx) { }

    TAG_HANDLER_PROC(tag)
    {
        int old_size, old_b, old_i, old_u, old_f, old_al;
        wxHtmlContainerCell *c;

        old_size = m_WParser->GetFontSize();
        old_b = m_WParser->GetFontBold();
        old_i = m_WParser->GetFontItalic();
        old_u = m_WParser->GetFontUnderlined();
        old_f = m_WParser->GetFontFixed();
        old_al = m_WParser->GetAlign();

        m_WParser->SetFontBold(true);
        m_WParser->SetFontItalic(false);
        m_WParser->SetFontUnderlined(false);
        m_WParser->SetFontFixed(false);

        if (tag.GetName() == wxT("H1"))
                m_WParser->SetFontSize(7);
        else if (tag.GetName() == wxT("H2"))
                m_WParser->SetFontSize(6);
        else if (tag.GetName() == wxT("H3"))
                m_WParser->SetFontSize(5);
        else if (tag.GetName() == wxT("H4"))
        {
                m_WParser->SetFontSize(5);
                m_WParser->SetFontItalic(true);
                m_WParser->SetFontBold(false);
        }
        else if (tag.GetName() == wxT("H5"))
                m_WParser->SetFontSize(4);
        else if (tag.GetName() == wxT("H6"))
        {
                m_WParser->SetFontSize(4);
                m_WParser->SetFontItalic(true);
                m_WParser->SetFontBold(false);
        }

        c = m_WParser->GetContainer();
        if (c->GetFirstChild() || c->HasId())
        {
            m_WParser->CloseContainer();
            c = m_WParser->OpenContainer();
        }

        c->CopyId(tag);
        c->SetAlign(tag);
        c->InsertCell(new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        c->SetIndent(m_WParser->GetCharHeight(), wxHTML_INDENT_TOP);
        m_WParser->SetAlign(c->GetAlignHor());

        ParseInner(tag);

        m_WParser->SetFontSize(old_size);
        m_WParser->SetFontBold(old_b);
        m_WParser->SetFontItalic(old_i);
        m_WParser->SetFontUnderlined(old_u);
        m_WParser->SetFontFixed(old_f);
        m_WParser->SetAlign(old_al);

        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        m_WParser->CloseContainer();
        m_WParser->OpenContainer();
        c = m_WParser->GetContainer();
        c->SetIndent(m_WParser->GetCharHeight(), wxHTML_INDENT_TOP);

        return true;
    }

TAG_HANDLER_END(Hx)


TAG_HANDLER_BEGIN(BIGSMALL, "BIG,SMALL")
    TAG_HANDLER_CONSTR(BIGSMALL) { }

    TAG_HANDLER_PROC(tag)
    {
        int oldsize = m_WParser->GetFontSize();
        int sz = (tag.GetName() == wxT("BIG")) ? +1 : -1;

        m_WParser->SetFontSize(oldsize + sz);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));

        ParseInner(tag);

        m_WParser->SetFontSize(oldsize);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        return true;
    }

TAG_HANDLER_END(BIGSMALL)




TAGS_MODULE_BEGIN(Fonts)

    TAGS_MODULE_ADD(FONT)
    TAGS_MODULE_ADD(FACES_U)
    TAGS_MODULE_ADD(FACES_I)
    TAGS_MODULE_ADD(FACES_B)
    TAGS_MODULE_ADD(FACES_TT)
    TAGS_MODULE_ADD(Hx)
    TAGS_MODULE_ADD(BIGSMALL)

TAGS_MODULE_END(Fonts)


#endif
