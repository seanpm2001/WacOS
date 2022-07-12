
# regression test for SAX 2.0
# $Id$

from xml.sax import make_parser, ContentHandler, \
                    SAXException, SAXReaderNotAvailable, SAXParseException
try:
    make_parser()
except SAXReaderNotAvailable:
    # don't try to test this module if we cannot create a parser
    raise ImportError("no XML parsers available")
from xml.sax.saxutils import XMLGenerator, escape, XMLFilterBase
from xml.sax.expatreader import create_parser
from xml.sax.xmlreader import InputSource, AttributesImpl, AttributesNSImpl
from cStringIO import StringIO
from test_support import verbose, TestFailed, findfile

# ===== Utilities

tests = 0
fails = 0

def confirm(outcome, name):
    global tests, fails

    tests = tests + 1
    if outcome:
        print "Passed", name
    else:
        print "Failed", name
        fails = fails + 1

# ===========================================================================
#
#   saxutils tests
#
# ===========================================================================

# ===== escape

def test_escape_basic():
    return escape("Donald Duck & Co") == "Donald Duck &amp; Co"

def test_escape_all():
    return escape("<Donald Duck & Co>") == "&lt;Donald Duck &amp; Co&gt;"

def test_escape_extra():
    return escape("Hei p� deg", {"�" : "&aring;"}) == "Hei p&aring; deg"

def test_make_parser():
    try:
        # Creating a parser should succeed - it should fall back
        # to the expatreader
        p = make_parser(['xml.parsers.no_such_parser'])
    except:
        return 0
    else:
        return p


# ===== XMLGenerator

start = '<?xml version="1.0" encoding="iso-8859-1"?>\n'

def test_xmlgen_basic():
    result = StringIO()
    gen = XMLGenerator(result)
    gen.startDocument()
    gen.startElement("doc", {})
    gen.endElement("doc")
    gen.endDocument()

    return result.getvalue() == start + "<doc></doc>"

def test_xmlgen_content():
    result = StringIO()
    gen = XMLGenerator(result)
    
    gen.startDocument()
    gen.startElement("doc", {})
    gen.characters("huhei")
    gen.endElement("doc")
    gen.endDocument()

    return result.getvalue() == start + "<doc>huhei</doc>"

def test_xmlgen_pi():
    result = StringIO()
    gen = XMLGenerator(result)
    
    gen.startDocument()
    gen.processingInstruction("test", "data")
    gen.startElement("doc", {})
    gen.endElement("doc")
    gen.endDocument()

    return result.getvalue() == start + "<?test data?><doc></doc>"

def test_xmlgen_content_escape():
    result = StringIO()
    gen = XMLGenerator(result)
    
    gen.startDocument()
    gen.startElement("doc", {})
    gen.characters("<huhei&")
    gen.endElement("doc")
    gen.endDocument()

    return result.getvalue() == start + "<doc>&lt;huhei&amp;</doc>"

def test_xmlgen_ignorable():
    result = StringIO()
    gen = XMLGenerator(result)
    
    gen.startDocument()
    gen.startElement("doc", {})
    gen.ignorableWhitespace(" ")
    gen.endElement("doc")
    gen.endDocument()

    return result.getvalue() == start + "<doc> </doc>"

ns_uri = "http://www.python.org/xml-ns/saxtest/"

def test_xmlgen_ns():
    result = StringIO()
    gen = XMLGenerator(result)
    
    gen.startDocument()
    gen.startPrefixMapping("ns1", ns_uri)
    gen.startElementNS((ns_uri, "doc"), "ns1:doc", {})
    # add an unqualified name
    gen.startElementNS((None, "udoc"), None, {})
    gen.endElementNS((None, "udoc"), None)
    gen.endElementNS((ns_uri, "doc"), "ns1:doc")
    gen.endPrefixMapping("ns1")
    gen.endDocument()

    return result.getvalue() == start + \
           ('<ns1:doc xmlns:ns1="%s"><udoc></udoc></ns1:doc>' %
                                         ns_uri)

# ===== XMLFilterBase

def test_filter_basic():
    result = StringIO()
    gen = XMLGenerator(result)
    filter = XMLFilterBase()
    filter.setContentHandler(gen)
    
    filter.startDocument()
    filter.startElement("doc", {})
    filter.characters("content")
    filter.ignorableWhitespace(" ")
    filter.endElement("doc")
    filter.endDocument()

    return result.getvalue() == start + "<doc>content </doc>"

# ===========================================================================
#
#   expatreader tests
#
# ===========================================================================

# ===== DTDHandler support

class TestDTDHandler:

    def __init__(self):
        self._notations = []
        self._entities  = []
    
    def notationDecl(self, name, publicId, systemId):
        self._notations.append((name, publicId, systemId))

    def unparsedEntityDecl(self, name, publicId, systemId, ndata):
        self._entities.append((name, publicId, systemId, ndata))

def test_expat_dtdhandler():
    parser = create_parser()
    handler = TestDTDHandler()
    parser.setDTDHandler(handler)

    parser.feed('<!DOCTYPE doc [\n')
    parser.feed('  <!ENTITY img SYSTEM "expat.gif" NDATA GIF>\n')
    parser.feed('  <!NOTATION GIF PUBLIC "-//CompuServe//NOTATION Graphics Interchange Format 89a//EN">\n')
    parser.feed(']>\n')
    parser.feed('<doc></doc>')
    parser.close()

    return handler._notations == [("GIF", "-//CompuServe//NOTATION Graphics Interchange Format 89a//EN", None)] and \
           handler._entities == [("img", None, "expat.gif", "GIF")]

# ===== EntityResolver support

class TestEntityResolver:

    def resolveEntity(self, publicId, systemId):
        inpsrc = InputSource()
        inpsrc.setByteStream(StringIO("<entity/>"))
        return inpsrc

def test_expat_entityresolver():
    parser = create_parser()
    parser.setEntityResolver(TestEntityResolver())
    result = StringIO()
    parser.setContentHandler(XMLGenerator(result))

    parser.feed('<!DOCTYPE doc [\n')
    parser.feed('  <!ENTITY test SYSTEM "whatever">\n')
    parser.feed(']>\n')
    parser.feed('<doc>&test;</doc>')
    parser.close()

    return result.getvalue() == start + "<doc><entity></entity></doc>"
    
# ===== Attributes support

class AttrGatherer(ContentHandler):

    def startElement(self, name, attrs):
        self._attrs = attrs

    def startElementNS(self, name, qname, attrs):
        self._attrs = attrs
        
def test_expat_attrs_empty():
    parser = create_parser()
    gather = AttrGatherer()
    parser.setContentHandler(gather)

    parser.feed("<doc/>")
    parser.close()

    return verify_empty_attrs(gather._attrs)

def test_expat_attrs_wattr():
    parser = create_parser()
    gather = AttrGatherer()
    parser.setContentHandler(gather)

    parser.feed("<doc attr='val'/>")
    parser.close()

    return verify_attrs_wattr(gather._attrs)

def test_expat_nsattrs_empty():
    parser = create_parser(1)
    gather = AttrGatherer()
    parser.setContentHandler(gather)

    parser.feed("<doc/>")
    parser.close()

    return verify_empty_nsattrs(gather._attrs)

def test_expat_nsattrs_wattr():
    parser = create_parser(1)
    gather = AttrGatherer()
    parser.setContentHandler(gather)

    parser.feed("<doc xmlns:ns='%s' ns:attr='val'/>" % ns_uri)
    parser.close()

    attrs = gather._attrs
    
    return attrs.getLength() == 1 and \
           attrs.getNames() == [(ns_uri, "attr")] and \
           attrs.getQNames() == [] and \
           len(attrs) == 1 and \
           attrs.has_key((ns_uri, "attr")) and \
           attrs.keys() == [(ns_uri, "attr")] and \
           attrs.get((ns_uri, "attr")) == "val" and \
           attrs.get((ns_uri, "attr"), 25) == "val" and \
           attrs.items() == [((ns_uri, "attr"), "val")] and \
           attrs.values() == ["val"] and \
           attrs.getValue((ns_uri, "attr")) == "val" and \
           attrs[(ns_uri, "attr")] == "val"

# ===== InputSource support

xml_test_out = open(findfile("test.xml.out")).read()

def test_expat_inpsource_filename():
    parser = create_parser()
    result = StringIO()
    xmlgen = XMLGenerator(result)

    parser.setContentHandler(xmlgen)
    parser.parse(findfile("test.xml"))

    return result.getvalue() == xml_test_out

def test_expat_inpsource_sysid():
    parser = create_parser()
    result = StringIO()
    xmlgen = XMLGenerator(result)

    parser.setContentHandler(xmlgen)
    parser.parse(InputSource(findfile("test.xml")))

    return result.getvalue() == xml_test_out

def test_expat_inpsource_stream():
    parser = create_parser()
    result = StringIO()
    xmlgen = XMLGenerator(result)

    parser.setContentHandler(xmlgen)
    inpsrc = InputSource()
    inpsrc.setByteStream(open(findfile("test.xml")))
    parser.parse(inpsrc)

    return result.getvalue() == xml_test_out


# ===========================================================================
#
#   error reporting
#
# ===========================================================================

def test_expat_inpsource_location():
    parser = create_parser()
    parser.setContentHandler(ContentHandler()) # do nothing
    source = InputSource()
    source.setByteStream(StringIO("<foo bar foobar>"))   #ill-formed
    name = "a file name"
    source.setSystemId(name)
    try:
        parser.parse(source)
    except SAXException, e:
        return e.getSystemId() == name

def test_expat_incomplete():
    parser = create_parser()
    parser.setContentHandler(ContentHandler()) # do nothing
    try:
        parser.parse(StringIO("<foo>"))
    except SAXParseException:
        return 1 # ok, error found
    else:
        return 0


# ===========================================================================
#
#   xmlreader tests
#
# ===========================================================================

# ===== AttributesImpl

def verify_empty_attrs(attrs):
    try:
        attrs.getValue("attr")
        gvk = 0
    except KeyError:
        gvk = 1

    try:
        attrs.getValueByQName("attr")
        gvqk = 0
    except KeyError:
        gvqk = 1

    try:
        attrs.getNameByQName("attr")
        gnqk = 0
    except KeyError:
        gnqk = 1

    try:
        attrs.getQNameByName("attr")
        gqnk = 0
    except KeyError:
        gqnk = 1
        
    try:
        attrs["attr"]
        gik = 0
    except KeyError:
        gik = 1
        
    return attrs.getLength() == 0 and \
           attrs.getNames() == [] and \
           attrs.getQNames() == [] and \
           len(attrs) == 0 and \
           not attrs.has_key("attr") and \
           attrs.keys() == [] and \
           attrs.get("attrs") == None and \
           attrs.get("attrs", 25) == 25 and \
           attrs.items() == [] and \
           attrs.values() == [] and \
           gvk and gvqk and gnqk and gik and gqnk

def verify_attrs_wattr(attrs):
    return attrs.getLength() == 1 and \
           attrs.getNames() == ["attr"] and \
           attrs.getQNames() == ["attr"] and \
           len(attrs) == 1 and \
           attrs.has_key("attr") and \
           attrs.keys() == ["attr"] and \
           attrs.get("attr") == "val" and \
           attrs.get("attr", 25) == "val" and \
           attrs.items() == [("attr", "val")] and \
           attrs.values() == ["val"] and \
           attrs.getValue("attr") == "val" and \
           attrs.getValueByQName("attr") == "val" and \
           attrs.getNameByQName("attr") == "attr" and \
           attrs["attr"] == "val" and \
           attrs.getQNameByName("attr") == "attr"

def test_attrs_empty():
    return verify_empty_attrs(AttributesImpl({}))

def test_attrs_wattr():
    return verify_attrs_wattr(AttributesImpl({"attr" : "val"}))

# ===== AttributesImpl

def verify_empty_nsattrs(attrs):
    try:
        attrs.getValue((ns_uri, "attr"))
        gvk = 0
    except KeyError:
        gvk = 1

    try:
        attrs.getValueByQName("ns:attr")
        gvqk = 0
    except KeyError:
        gvqk = 1

    try:
        attrs.getNameByQName("ns:attr")
        gnqk = 0
    except KeyError:
        gnqk = 1

    try:
        attrs.getQNameByName((ns_uri, "attr"))
        gqnk = 0
    except KeyError:
        gqnk = 1
        
    try:
        attrs[(ns_uri, "attr")]
        gik = 0
    except KeyError:
        gik = 1
        
    return attrs.getLength() == 0 and \
           attrs.getNames() == [] and \
           attrs.getQNames() == [] and \
           len(attrs) == 0 and \
           not attrs.has_key((ns_uri, "attr")) and \
           attrs.keys() == [] and \
           attrs.get((ns_uri, "attr")) == None and \
           attrs.get((ns_uri, "attr"), 25) == 25 and \
           attrs.items() == [] and \
           attrs.values() == [] and \
           gvk and gvqk and gnqk and gik and gqnk

def test_nsattrs_empty():
    return verify_empty_nsattrs(AttributesNSImpl({}, {}))

def test_nsattrs_wattr():
    attrs = AttributesNSImpl({(ns_uri, "attr") : "val"},
                             {(ns_uri, "attr") : "ns:attr"})
    
    return attrs.getLength() == 1 and \
           attrs.getNames() == [(ns_uri, "attr")] and \
           attrs.getQNames() == ["ns:attr"] and \
           len(attrs) == 1 and \
           attrs.has_key((ns_uri, "attr")) and \
           attrs.keys() == [(ns_uri, "attr")] and \
           attrs.get((ns_uri, "attr")) == "val" and \
           attrs.get((ns_uri, "attr"), 25) == "val" and \
           attrs.items() == [((ns_uri, "attr"), "val")] and \
           attrs.values() == ["val"] and \
           attrs.getValue((ns_uri, "attr")) == "val" and \
           attrs.getValueByQName("ns:attr") == "val" and \
           attrs.getNameByQName("ns:attr") == (ns_uri, "attr") and \
           attrs[(ns_uri, "attr")] == "val" and \
           attrs.getQNameByName((ns_uri, "attr")) == "ns:attr"
        

# ===== Main program

def make_test_output():
    parser = create_parser()
    result = StringIO()
    xmlgen = XMLGenerator(result)

    parser.setContentHandler(xmlgen)
    parser.parse(findfile("test.xml"))

    outf = open(findfile("test.xml.out"), "w")
    outf.write(result.getvalue())
    outf.close()

items = locals().items()
items.sort()
for (name, value) in items:
    if name[ : 5] == "test_":
        confirm(value(), name)

print "%d tests, %d failures" % (tests, fails)
if fails != 0:
    raise TestFailed, "%d of %d tests failed" % (fails, tests)
