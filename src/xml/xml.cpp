/////////////////////////////////////////////////////////////////////////////
// Name:        src/xml/xml.cpp
// Purpose:     wxXmlDocument - XML parser & data holder class
// Author:      Vaclav Slavik
// Created:     2000/03/05
// Copyright:   (c) 2000 Vaclav Slavik
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#if wxUSE_XML

#include "wx/xml/xml.h"

#ifndef WX_PRECOMP
    #include "wx/intl.h"
    #include "wx/log.h"
    #include "wx/app.h"
#endif

#include "wx/wfstream.h"
#include "wx/datstrm.h"
#include "wx/zstream.h"
#include "wx/strconv.h"
#include "wx/versioninfo.h"

#include <memory>

#include "expat.h" // from Expat

// DLL options compatibility check:
WX_CHECK_BUILD_OPTIONS("wxXML")


wxIMPLEMENT_CLASS(wxXmlDocument, wxObject);


// a private utility used by wxXML
static bool wxIsWhiteOnly(const wxString& buf);


//-----------------------------------------------------------------------------
//  wxXmlNode
//-----------------------------------------------------------------------------

wxXmlNode::wxXmlNode(wxXmlNode *parent,wxXmlNodeType type,
                     const wxString& name, const wxString& content,
                     wxXmlAttribute *attrs, wxXmlNode *next, int lineNo)
    : m_type(type), m_name(name), m_content(content),
      m_attrs(attrs), m_parent(parent),
      m_children(nullptr), m_next(next),
      m_lineNo(lineNo),
      m_noConversion(false)
{
    wxASSERT_MSG ( type != wxXML_ELEMENT_NODE || content.empty(), "element nodes can't have content" );

    if (m_parent)
    {
        if (m_parent->m_children)
        {
            m_next = m_parent->m_children;
            m_parent->m_children = this;
        }
        else
            m_parent->m_children = this;
    }
}

wxXmlNode::wxXmlNode(wxXmlNodeType type, const wxString& name,
                     const wxString& content,
                     int lineNo)
    : m_type(type), m_name(name), m_content(content),
      m_attrs(nullptr), m_parent(nullptr),
      m_children(nullptr), m_next(nullptr),
      m_lineNo(lineNo), m_noConversion(false)
{
    wxASSERT_MSG ( type != wxXML_ELEMENT_NODE || content.empty(), "element nodes can't have content" );
}

wxXmlNode::wxXmlNode(const wxXmlNode& node)
{
    m_next = nullptr;
    m_parent = nullptr;
    DoCopy(node);
}

wxXmlNode::~wxXmlNode()
{
    DoFree();
}

wxXmlNode& wxXmlNode::operator=(const wxXmlNode& node)
{
    if ( &node != this )
    {
        DoFree();
        DoCopy(node);
    }

    return *this;
}

void wxXmlNode::DoFree()
{
    wxXmlNode *c, *c2;
    for (c = m_children; c; c = c2)
    {
        c2 = c->m_next;
        delete c;
    }

    wxXmlAttribute *p, *p2;
    for (p = m_attrs; p; p = p2)
    {
        p2 = p->GetNext();
        delete p;
    }
}

void wxXmlNode::DoCopy(const wxXmlNode& node)
{
    m_type = node.m_type;
    m_name = node.m_name;
    m_content = node.m_content;
    m_lineNo = node.m_lineNo;
    m_noConversion = node.m_noConversion;
    m_children = nullptr;

    wxXmlNode *n = node.m_children;
    while (n)
    {
        AddChild(new wxXmlNode(*n));
        n = n->GetNext();
    }

    m_attrs = nullptr;
    wxXmlAttribute *p = node.m_attrs;
    while (p)
    {
       AddAttribute(p->GetName(), p->GetValue());
       p = p->GetNext();
    }
}

bool wxXmlNode::HasAttribute(const wxString& attrName) const
{
    wxXmlAttribute *attr = GetAttributes();

    while (attr)
    {
        if (attr->GetName() == attrName) return true;
        attr = attr->GetNext();
    }

    return false;
}

bool wxXmlNode::GetAttribute(const wxString& attrName, wxString *value) const
{
    wxCHECK_MSG( value, false, "value argument must not be null" );

    wxXmlAttribute *attr = GetAttributes();

    while (attr)
    {
        if (attr->GetName() == attrName)
        {
            *value = attr->GetValue();
            return true;
        }
        attr = attr->GetNext();
    }

    return false;
}

wxString wxXmlNode::GetAttribute(const wxString& attrName, const wxString& defaultVal) const
{
    wxString tmp;
    if (GetAttribute(attrName, &tmp))
        return tmp;

    return defaultVal;
}

void wxXmlNode::AddChild(wxXmlNode *child)
{
    if (m_children == nullptr)
        m_children = child;
    else
    {
        wxXmlNode *ch = m_children;
        while (ch->m_next) ch = ch->m_next;
        ch->m_next = child;
    }
    child->m_next = nullptr;
    child->m_parent = this;
}

// inserts a new node in front of 'followingNode'
bool wxXmlNode::InsertChild(wxXmlNode *child, wxXmlNode *followingNode)
{
    wxCHECK_MSG( child, false, "cannot insert a null node!" );
    wxCHECK_MSG( child->m_parent == nullptr, false, "node already has a parent" );
    wxCHECK_MSG( child->m_next == nullptr, false, "node already has m_next" );
    wxCHECK_MSG( followingNode == nullptr || followingNode->GetParent() == this,
                 false,
                 "wxXmlNode::InsertChild - followingNode has incorrect parent" );

    // this is for backward compatibility, nullptr was allowed here thanks to
    // the confusion about followingNode's meaning
    if ( followingNode == nullptr )
        followingNode = m_children;

    if ( m_children == followingNode )
    {
        child->m_next = m_children;
        m_children = child;
    }
    else
    {
        wxXmlNode *ch = m_children;
        while ( ch && ch->m_next != followingNode )
            ch = ch->m_next;
        if ( !ch )
        {
            wxFAIL_MSG( "followingNode has this node as parent, but couldn't be found among children" );
            return false;
        }

        child->m_next = followingNode;
        ch->m_next = child;
    }

    child->m_parent = this;
    return true;
}

// inserts a new node right after 'precedingNode'
bool wxXmlNode::InsertChildAfter(wxXmlNode *child, wxXmlNode *precedingNode)
{
    wxCHECK_MSG( child, false, "cannot insert a null node!" );
    wxCHECK_MSG( child->m_parent == nullptr, false, "node already has a parent" );
    wxCHECK_MSG( child->m_next == nullptr, false, "node already has m_next" );
    wxCHECK_MSG( precedingNode == nullptr || precedingNode->m_parent == this, false,
                 "precedingNode has wrong parent" );

    if ( precedingNode )
    {
        child->m_next = precedingNode->m_next;
        precedingNode->m_next = child;
    }
    else // precedingNode == nullptr
    {
        wxCHECK_MSG( m_children == nullptr, false,
                     "null precedingNode only makes sense when there are no children" );

        child->m_next = m_children;
        m_children = child;
    }

    child->m_parent = this;
    return true;
}

bool wxXmlNode::RemoveChild(wxXmlNode *child)
{
    if (m_children == nullptr)
        return false;
    else if (m_children == child)
    {
        m_children = child->m_next;
        child->m_parent = nullptr;
        child->m_next = nullptr;
        return true;
    }
    else
    {
        wxXmlNode *ch = m_children;
        while (ch->m_next)
        {
            if (ch->m_next == child)
            {
                ch->m_next = child->m_next;
                child->m_parent = nullptr;
                child->m_next = nullptr;
                return true;
            }
            ch = ch->m_next;
        }
        return false;
    }
}

void wxXmlNode::AddAttribute(const wxString& name, const wxString& value)
{
    AddAttribute(new wxXmlAttribute(name, value, nullptr));
}

void wxXmlNode::AddAttribute(wxXmlAttribute *attr)
{
    if (m_attrs == nullptr)
        m_attrs = attr;
    else
    {
        wxXmlAttribute *p = m_attrs;
        while (p->GetNext()) p = p->GetNext();
        p->SetNext(attr);
    }
}

bool wxXmlNode::DeleteAttribute(const wxString& name)
{
    wxXmlAttribute *attr;

    if (m_attrs == nullptr)
        return false;

    else if (m_attrs->GetName() == name)
    {
        attr = m_attrs;
        m_attrs = attr->GetNext();
        attr->SetNext(nullptr);
        delete attr;
        return true;
    }

    else
    {
        wxXmlAttribute *p = m_attrs;
        while (p->GetNext())
        {
            if (p->GetNext()->GetName() == name)
            {
                attr = p->GetNext();
                p->SetNext(attr->GetNext());
                attr->SetNext(nullptr);
                delete attr;
                return true;
            }
            p = p->GetNext();
        }
        return false;
    }
}

wxString wxXmlNode::GetNodeContent() const
{
    wxXmlNode *n = GetChildren();

    while (n)
    {
        if (n->GetType() == wxXML_TEXT_NODE ||
            n->GetType() == wxXML_CDATA_SECTION_NODE)
            return n->GetContent();
        n = n->GetNext();
    }
    return wxEmptyString;
}

int wxXmlNode::GetDepth(const wxXmlNode *grandparent) const
{
    const wxXmlNode *n = this;
    int ret = -1;

    do
    {
        ret++;
        n = n->GetParent();
        if (n == grandparent)
            return ret;

    } while (n);

    return wxNOT_FOUND;
}

bool wxXmlNode::IsWhitespaceOnly() const
{
    return wxIsWhiteOnly(m_content);
}



//-----------------------------------------------------------------------------
//  wxXmlDoctype
//-----------------------------------------------------------------------------

void wxXmlDoctype::Clear()
{
    m_rootName.clear();
    m_systemId.clear();
    m_publicId.clear();
}

wxString wxXmlDoctype::GetFullString() const
{
    wxString content;
    if ( !m_rootName.empty() )
    {
        content = m_rootName;

        if ( !m_publicId.empty() )
        {
            content << wxS(" PUBLIC \"") << m_publicId << wxS("\"");
        }

        if ( !m_systemId.empty() )
        {
            if ( m_publicId.empty() )
                content << wxS(" SYSTEM");

            // Prefer to use double quotes, but switch to single ones if a
            // double quote appears inside the string to be quoted.
            wxString quote;
            if ( m_systemId.find('\"') == wxString::npos )
                quote = wxS('"');
            else if ( m_systemId.find('\'') == wxString::npos )
                quote = wxS('\'');
            else // It's an error if we can't use either kind of quotes.
                return wxString();

            content << wxS(' ') << quote << m_systemId << quote;
        }
    }

    return content;
}

bool wxXmlDoctype::IsValid() const
{
    return !GetFullString().empty();
}

//-----------------------------------------------------------------------------
//  wxXmlDocument
//-----------------------------------------------------------------------------

wxXmlDocument::wxXmlDocument()
    : m_version(wxS("1.0")), m_fileEncoding(wxS("UTF-8"))
{
}

wxXmlDocument::wxXmlDocument(const wxString& filename)
              :wxObject()
{
    Load(filename);
}

wxXmlDocument::wxXmlDocument(wxInputStream& stream)
              :wxObject()
{
    Load(stream);
}

wxXmlDocument::wxXmlDocument(const wxXmlDocument& doc)
              :wxObject()
{
    DoCopy(doc);
}

wxXmlDocument& wxXmlDocument::operator=(const wxXmlDocument& doc)
{
    DoCopy(doc);
    return *this;
}

void wxXmlDocument::DoCopy(const wxXmlDocument& doc)
{
    m_version = doc.m_version;
    m_fileEncoding = doc.m_fileEncoding;
    m_doctype = doc.m_doctype;
    m_fileType = doc.m_fileType;
    m_eol = doc.m_eol;

    if (doc.m_docNode)
        m_docNode.reset(new wxXmlNode(*doc.m_docNode));
    else
        m_docNode.reset();
}

bool wxXmlDocument::Load(const wxString& filename, int flags,
                         wxXmlParseError* err)
{
    wxFileInputStream stream(filename);
    if (!stream.IsOk())
        return false;
    return Load(stream, flags, err);
}

bool wxXmlDocument::Save(const wxString& filename, int indentstep) const
{
    wxFileOutputStream stream(filename);
    if (!stream.IsOk())
        return false;
    return Save(stream, indentstep);
}

wxXmlNode *wxXmlDocument::GetRoot() const
{
    wxXmlNode *node = m_docNode.get();
    if (node)
    {
        node = m_docNode->GetChildren();
        while (node != nullptr && node->GetType() != wxXML_ELEMENT_NODE)
            node = node->GetNext();
    }
    return node;
}

wxXmlNode *wxXmlDocument::DetachRoot()
{
    wxXmlNode *node = m_docNode.get();
    if (node)
    {
        node = m_docNode->GetChildren();
        wxXmlNode *prev = nullptr;
        while (node != nullptr && node->GetType() != wxXML_ELEMENT_NODE)
        {
            prev = node;
            node = node->GetNext();
        }
        if (node)
        {
            if (node == m_docNode->GetChildren())
                m_docNode->SetChildren(node->GetNext());

            if (prev)
                prev->SetNext(node->GetNext());

            node->SetParent(nullptr);
            node->SetNext(nullptr);
        }
    }
    return node;
}

void wxXmlDocument::SetRoot(wxXmlNode *root)
{
    if (root)
    {
        wxASSERT_MSG( root->GetType() == wxXML_ELEMENT_NODE,
                      "Can only set an element type node as root" );
    }

    wxXmlNode *node = m_docNode.get();
    if (node)
    {
        node = m_docNode->GetChildren();
        wxXmlNode *prev = nullptr;
        while (node != nullptr && node->GetType() != wxXML_ELEMENT_NODE)
        {
            prev = node;
            node = node->GetNext();
        }
        if (node && root)
        {
            root->SetNext( node->GetNext() );
            wxDELETE(node);
        }
        if (prev)
            prev->SetNext(root);
        else
            m_docNode->SetChildren(root);
    }
    else
    {
        m_docNode.reset(new wxXmlNode(wxXML_DOCUMENT_NODE, wxEmptyString));
        m_docNode->SetChildren(root);
    }
    if (root)
        root->SetParent(m_docNode.get());
}

void wxXmlDocument::SetFileType(wxTextFileType fileType)
{
    m_fileType = fileType;
    m_eol = wxTextBuffer::GetEOL(m_fileType);
}

void wxXmlDocument::AppendToProlog(wxXmlNode *node)
{
    if (!m_docNode)
        m_docNode.reset(new wxXmlNode(wxXML_DOCUMENT_NODE, wxEmptyString));
    if (IsOk())
        m_docNode->InsertChild( node, GetRoot() );
    else
        m_docNode->AddChild( node );
}

//-----------------------------------------------------------------------------
//  wxXmlDocument loading routines
//-----------------------------------------------------------------------------

// returns true if the given string contains only whitespaces
bool wxIsWhiteOnly(const wxString& buf)
{
    for ( wxString::const_iterator i = buf.begin(); i != buf.end(); ++i )
    {
        wxUniChar c = *i;
        if ( c != wxS(' ') && c != wxS('\t') && c != wxS('\n') && c != wxS('\r'))
            return false;
    }
    return true;
}


struct wxXmlParsingContext
{
    wxXmlParsingContext()
        : node(nullptr),
          lastChild(nullptr),
          lastAsText(nullptr),
          doctype(nullptr),
          removeWhiteOnlyNodes(false)
    {}

    XML_Parser parser;
    wxXmlNode *node;                    // the node being parsed
    wxXmlNode *lastChild;               // the last child of "node"
    wxXmlNode *lastAsText;              // the last _text_ child of "node"
    wxString   encoding;
    wxString   version;
    wxXmlDoctype *doctype;
    bool       removeWhiteOnlyNodes;
};

// checks that ctx->lastChild is in consistent state
#define ASSERT_LAST_CHILD_OK(ctx)                                   \
    wxASSERT( ctx->lastChild == nullptr ||                             \
              ctx->lastChild->GetNext() == nullptr );                  \
    wxASSERT( ctx->lastChild == nullptr ||                             \
              ctx->lastChild->GetParent() == ctx->node )

extern "C" {
static void StartElementHnd(void *userData, const char *name, const char **atts)
{
    wxXmlParsingContext *ctx = (wxXmlParsingContext*)userData;
    wxXmlNode *node = new wxXmlNode(wxXML_ELEMENT_NODE,
                                    wxString::FromUTF8Unchecked(name),
                                    wxEmptyString,
                                    XML_GetCurrentLineNumber(ctx->parser));
    const char **a = atts;

    // add node attributes
    while (*a)
    {
        node->AddAttribute(wxString::FromUTF8Unchecked(a[0]), wxString::FromUTF8Unchecked(a[1]));
        a += 2;
    }

    ASSERT_LAST_CHILD_OK(ctx);
    ctx->node->InsertChildAfter(node, ctx->lastChild);
    ctx->lastAsText = nullptr;
    ctx->lastChild = nullptr; // our new node "node" has no children yet

    ctx->node = node;
}

static void EndElementHnd(void *userData, const char* WXUNUSED(name))
{
    wxXmlParsingContext *ctx = (wxXmlParsingContext*)userData;

    // we're exiting the last children of ctx->node->GetParent() and going
    // back one level up, so current value of ctx->node points to the last
    // child of ctx->node->GetParent()
    ctx->lastChild = ctx->node;

    ctx->node = ctx->node->GetParent();
    ctx->lastAsText = nullptr;
}

static void TextHnd(void *userData, const char *s, int len)
{
    wxXmlParsingContext *ctx = (wxXmlParsingContext*)userData;
    wxString str = wxString::FromUTF8Unchecked(s, len);

    if (ctx->lastAsText)
    {
        ctx->lastAsText->SetContent(ctx->lastAsText->GetContent() + str);
    }
    else
    {
        bool whiteOnly = false;
        if (ctx->removeWhiteOnlyNodes)
            whiteOnly = wxIsWhiteOnly(str);

        if (!whiteOnly)
        {
            wxXmlNode *textnode =
                new wxXmlNode(wxXML_TEXT_NODE, wxS("text"), str,
                              XML_GetCurrentLineNumber(ctx->parser));

            ASSERT_LAST_CHILD_OK(ctx);
            ctx->node->InsertChildAfter(textnode, ctx->lastChild);
            ctx->lastChild= ctx->lastAsText = textnode;
        }
    }
}

static void StartCdataHnd(void *userData)
{
    wxXmlParsingContext *ctx = (wxXmlParsingContext*)userData;

    wxXmlNode *textnode =
        new wxXmlNode(wxXML_CDATA_SECTION_NODE, wxS("cdata"), wxS(""),
                      XML_GetCurrentLineNumber(ctx->parser));

    ASSERT_LAST_CHILD_OK(ctx);
    ctx->node->InsertChildAfter(textnode, ctx->lastChild);
    ctx->lastChild= ctx->lastAsText = textnode;
}

static void EndCdataHnd(void *userData)
{
    wxXmlParsingContext *ctx = (wxXmlParsingContext*)userData;

    // we need to reset this pointer so that subsequent text nodes don't append
    // their contents to this one but create new wxXML_TEXT_NODE objects (or
    // not create anything at all if only white space follows the CDATA section
    // and wxXMLDOC_KEEP_WHITESPACE_NODES is not used as is commonly the case)
    ctx->lastAsText = nullptr;
}

static void CommentHnd(void *userData, const char *data)
{
    wxXmlParsingContext *ctx = (wxXmlParsingContext*)userData;

    wxXmlNode *commentnode =
        new wxXmlNode(wxXML_COMMENT_NODE,
                      wxS("comment"), wxString::FromUTF8Unchecked(data),
                      XML_GetCurrentLineNumber(ctx->parser));

    ASSERT_LAST_CHILD_OK(ctx);
    ctx->node->InsertChildAfter(commentnode, ctx->lastChild);
    ctx->lastChild = commentnode;
    ctx->lastAsText = nullptr;
}

static void PIHnd(void *userData, const char *target, const char *data)
{
    wxXmlParsingContext *ctx = (wxXmlParsingContext*)userData;

    wxXmlNode *pinode =
        new wxXmlNode(wxXML_PI_NODE, wxString::FromUTF8Unchecked(target),
                      wxString::FromUTF8Unchecked(data),
                      XML_GetCurrentLineNumber(ctx->parser));

    ASSERT_LAST_CHILD_OK(ctx);
    ctx->node->InsertChildAfter(pinode, ctx->lastChild);
    ctx->lastChild = pinode;
    ctx->lastAsText = nullptr;
}

static void StartDoctypeHnd(void *userData, const char *doctypeName,
                            const char *sysid, const char *pubid,
                            int WXUNUSED(has_internal_subset))
{
    wxXmlParsingContext *ctx = (wxXmlParsingContext*)userData;

    *ctx->doctype = wxXmlDoctype(wxString::FromUTF8Unchecked(doctypeName),
                                 wxString::FromUTF8Unchecked(sysid),
                                 wxString::FromUTF8Unchecked(pubid));
}

static void EndDoctypeHnd(void *WXUNUSED(userData))
{
}

static void DefaultHnd(void *userData, const char *s, int len)
{
    // XML header:
    if (len > 6 && memcmp(s, "<?xml ", 6) == 0)
    {
        wxXmlParsingContext *ctx = (wxXmlParsingContext*)userData;

        wxString buf = wxString::FromUTF8Unchecked(s, (size_t)len);
        int pos;
        pos = buf.Find(wxS("encoding="));
        if (pos != wxNOT_FOUND)
            ctx->encoding = buf.Mid(pos + 10).BeforeFirst(buf[(size_t)pos+9]);
        pos = buf.Find(wxS("version="));
        if (pos != wxNOT_FOUND)
            ctx->version = buf.Mid(pos + 9).BeforeFirst(buf[(size_t)pos+8]);
    }
}

static int UnknownEncodingHnd(void * WXUNUSED(encodingHandlerData),
                              const XML_Char *name, XML_Encoding *info)
{
    // We must build conversion table for expat. The easiest way to do so
    // is to let wxCSConv convert as string containing all characters to
    // wide character representation:
    wxCSConv conv(name);
    char mbBuf[2];
    wchar_t wcBuf[10];
    size_t i;

    mbBuf[1] = 0;
    info->map[0] = 0;
    for (i = 0; i < 255; i++)
    {
        mbBuf[0] = (char)(i+1);
        if (conv.MB2WC(wcBuf, mbBuf, 2) == (size_t)-1)
        {
            // invalid/undefined byte in the encoding:
            info->map[i+1] = -1;
        }
        info->map[i+1] = (int)wcBuf[0];
    }

    info->data = nullptr;
    info->convert = nullptr;
    info->release = nullptr;

    return 1;
}

} // extern "C"

bool wxXmlDocument::Load(wxInputStream& stream, int flags,
                         wxXmlParseError* err)
{
    const size_t BUFSIZE = 16384;
    char buf[BUFSIZE];
    wxXmlParsingContext ctx;
    bool done;
    XML_Parser parser = XML_ParserCreate(nullptr);
    wxXmlNode *root = new wxXmlNode(wxXML_DOCUMENT_NODE, wxEmptyString);

    ctx.encoding = wxS("UTF-8"); // default in absence of encoding=""
    ctx.doctype = &m_doctype;
    ctx.removeWhiteOnlyNodes = (flags & wxXMLDOC_KEEP_WHITESPACE_NODES) == 0;
    ctx.parser = parser;
    ctx.node = root;

    XML_SetUserData(parser, (void*)&ctx);
    XML_SetElementHandler(parser, StartElementHnd, EndElementHnd);
    XML_SetCharacterDataHandler(parser, TextHnd);
    XML_SetCdataSectionHandler(parser, StartCdataHnd, EndCdataHnd);
    XML_SetCommentHandler(parser, CommentHnd);
    XML_SetProcessingInstructionHandler(parser, PIHnd);
    XML_SetDoctypeDeclHandler(parser, StartDoctypeHnd, EndDoctypeHnd);
    XML_SetDefaultHandler(parser, DefaultHnd);
    XML_SetUnknownEncodingHandler(parser, UnknownEncodingHnd, nullptr);

    bool ok = true;
    do
    {
        size_t len = stream.Read(buf, BUFSIZE).LastRead();
        done = (len < BUFSIZE);
        if (!XML_Parse(parser, buf, len, done))
        {
            if (err)
            {
                err->message = XML_ErrorString(XML_GetErrorCode(parser));
                err->line = (int)XML_GetCurrentLineNumber(parser);
                err->column = (int)XML_GetCurrentColumnNumber(parser);
                err->offset = XML_GetCurrentByteIndex(parser);
            }
            else
            {
                wxString error(XML_ErrorString(XML_GetErrorCode(parser)),
                               *wxConvCurrent);
                wxLogError(_("XML parsing error: '%s' at line %d"),
                           error.c_str(),
                           (int)XML_GetCurrentLineNumber(parser));
            }
            ok = false;
            break;
        }
    } while (!done);

    if (ok)
    {
        if (!ctx.version.empty())
            SetVersion(ctx.version);
        if (!ctx.encoding.empty())
            SetFileEncoding(ctx.encoding);
        SetDocumentNode(root);
    }
    else
    {
        delete root;
    }

    XML_ParserFree(parser);

    return ok;

}



//-----------------------------------------------------------------------------
//  wxXmlDocument saving routines
//-----------------------------------------------------------------------------

// helpers for XML generation
namespace
{

// write string to output:
bool OutputString(wxOutputStream& stream,
                  const wxString& str,
                  wxMBConv *convMem,
                  wxMBConv *convFile)
{
    if (str.empty())
        return true;

    wxUnusedVar(convMem);
    if ( !convFile )
        convFile = &wxConvUTF8;

    const wxScopedCharBuffer buf(str.mb_str(*convFile));
    if ( !buf.length() )
    {
        // conversion failed, can't write this string in an XML file in this
        // (presumably non-UTF-8) encoding
        return false;
    }

    stream.Write(buf, buf.length());

    return stream.IsOk();
}

enum EscapingMode
{
    Escape_Text,
    Escape_Attribute
};

// Same as above, but create entities first.
// Translates '<' to "&lt;", '>' to "&gt;" and so on, according to the spec:
// http://www.w3.org/TR/2000/WD-xml-c14n-20000119.html#charescaping
bool OutputEscapedString(wxOutputStream& stream,
                         const wxString& str,
                         wxMBConv *convMem,
                         wxMBConv *convFile,
                         EscapingMode mode)
{
    wxString escaped;
    escaped.reserve(str.length());

    for ( wxString::const_iterator i = str.begin(); i != str.end(); ++i )
    {
        const wxUniChar c = *i;

        switch ( c.GetValue() )
        {
            case wxS('<'):
                escaped.append(wxS("&lt;"));
                break;
            case wxS('>'):
                escaped.append(wxS("&gt;"));
                break;
            case wxS('&'):
                escaped.append(wxS("&amp;"));
                break;
            case wxS('\r'):
                escaped.append(wxS("&#xD;"));
                break;
            default:
                if ( mode == Escape_Attribute )
                {
                    switch ( c.GetValue() )
                    {
                        case wxS('"'):
                            escaped.append(wxS("&quot;"));
                            break;
                        case wxS('\t'):
                            escaped.append(wxS("&#x9;"));
                            break;
                        case wxS('\n'):
                            escaped.append(wxS("&#xA;"));
                            break;
                        default:
                            escaped.append(c);
                    }

                }
                else
                {
                    escaped.append(c);
                }
        }
    }

    return OutputString(stream, escaped, convMem, convFile);
}

bool OutputIndentation(wxOutputStream& stream,
                       int indent,
                       wxMBConv *convMem,
                       wxMBConv *convFile,
                       const wxString& eol)
{
    wxString str(eol);
    str += wxString(indent, wxS(' '));
    return OutputString(stream, str, convMem, convFile);
}

bool OutputNode(wxOutputStream& stream,
                wxXmlNode *node,
                int indent,
                wxMBConv *convMem,
                wxMBConv *convFile,
                int indentstep,
                const wxString& eol)
{
    bool rc;
    switch (node->GetType())
    {
        case wxXML_CDATA_SECTION_NODE:
            rc = OutputString(stream, wxS("<![CDATA["), convMem, convFile) &&
                 OutputString(stream, node->GetContent(), convMem, convFile) &&
                 OutputString(stream, wxS("]]>"), convMem, convFile);
            break;

        case wxXML_TEXT_NODE:
            if (node->GetNoConversion())
            {
                stream.Write(node->GetContent().c_str(), node->GetContent().length());
                rc = true;
            }
            else
                rc = OutputEscapedString(stream, node->GetContent(),
                                     convMem, convFile,
                                     Escape_Text);
            break;

        case wxXML_ELEMENT_NODE:
            rc = OutputString(stream, wxS("<"), convMem, convFile) &&
                 OutputString(stream, node->GetName(), convMem, convFile);

            if ( rc )
            {
                for ( wxXmlAttribute *attr = node->GetAttributes();
                      attr && rc;
                      attr = attr->GetNext() )
                {
                    rc = OutputString(stream,
                                      wxS(" ") + attr->GetName() +  wxS("=\""),
                                      convMem, convFile) &&
                         OutputEscapedString(stream, attr->GetValue(),
                                             convMem, convFile,
                                             Escape_Attribute) &&
                         OutputString(stream, wxS("\""), convMem, convFile);
                }
            }

            if ( node->GetChildren() )
            {
                rc = OutputString(stream, wxS(">"), convMem, convFile);

                wxXmlNode *prev = nullptr;
                for ( wxXmlNode *n = node->GetChildren();
                      n && rc;
                      n = n->GetNext() )
                {
                    if ( indentstep >= 0 && n->GetType() != wxXML_TEXT_NODE )
                    {
                        rc = OutputIndentation(stream, indent + indentstep,
                                               convMem, convFile, eol);
                    }

                    if ( rc )
                        rc = OutputNode(stream, n, indent + indentstep,
                                        convMem, convFile, indentstep, eol);

                    prev = n;
                }

                if ( rc && indentstep >= 0 &&
                        prev && prev->GetType() != wxXML_TEXT_NODE )
                {
                    rc = OutputIndentation(stream, indent, convMem, convFile,
                                           eol);
                }

                if ( rc )
                {
                    rc = OutputString(stream, wxS("</"), convMem, convFile) &&
                         OutputString(stream, node->GetName(),
                                      convMem, convFile) &&
                         OutputString(stream, wxS(">"), convMem, convFile);
                }
            }
            else // no children, output "<foo/>"
            {
                rc = OutputString(stream, wxS("/>"), convMem, convFile);
            }
            break;

        case wxXML_COMMENT_NODE:
            rc = OutputString(stream, wxS("<!--"), convMem, convFile) &&
                 OutputString(stream, node->GetContent(), convMem, convFile) &&
                 OutputString(stream, wxS("-->"), convMem, convFile);
            break;

        case wxXML_PI_NODE:
            rc = OutputString(stream, wxS("<?"), convMem, convFile) &&
                 OutputString(stream, node->GetName(), convMem, convFile) &&
                 OutputString(stream, wxS(" "), convMem, convFile) &&
                 OutputString(stream, node->GetContent(), convMem, convFile) &&
                 OutputString(stream, wxS("?>"), convMem, convFile);
            break;

        default:
            wxFAIL_MSG("unsupported node type");
            rc = false;
    }

    return rc;
}

} // anonymous namespace

bool wxXmlDocument::Save(wxOutputStream& stream, int indentstep) const
{
    if ( !IsOk() )
        return false;

    std::unique_ptr<wxMBConv> convMem, convFile;

    convFile.reset(new wxCSConv(GetFileEncoding()));

    wxString dec = wxString::Format(
                                    wxS("<?xml version=\"%s\" encoding=\"%s\"?>") + m_eol,
                                    GetVersion(), GetFileEncoding()
                                   );
    bool rc = OutputString(stream, dec, convMem.get(), convFile.get());

    if ( rc )
    {
        const wxString doctype = m_doctype.GetFullString();
        if ( !doctype.empty() )
        {
            rc = OutputString(stream,
                              wxS("<!DOCTYPE ") + doctype + wxS(">") + m_eol,
                              convMem.get(), convFile.get());
        }
    }

    wxXmlNode *node = GetDocumentNode();
    if ( node )
        node = node->GetChildren();

    while( rc && node )
    {
        rc = OutputNode(stream, node, 0, convMem.get(),
                        convFile.get(), indentstep, m_eol) &&
             OutputString(stream, m_eol, convMem.get(), convFile.get());
        node = node->GetNext();
    }
    return rc;
}

/*static*/ wxVersionInfo wxXmlDocument::GetLibraryVersionInfo()
{
    return wxVersionInfo("expat",
                         XML_MAJOR_VERSION,
                         XML_MINOR_VERSION,
                         XML_MICRO_VERSION);
}

#endif // wxUSE_XML
