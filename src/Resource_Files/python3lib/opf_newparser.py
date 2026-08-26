#!/usr/bin/env python
# -*- coding: utf-8 -*-
# vim:ts=4:sw=4:softtabstop=4:smarttab:expandtab

# Copyright (c) 2014 Kevin B. Hendricks, John Schember, and Doug Massay
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of
# conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list
# of conditions and the following disclaimer in the documentation and/or other materials
# provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
# SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
# TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
# WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import sys, os, codecs
from collections import OrderedDict
from hrefutils import urlencodepart, urldecodepart

# encode to make xml safe
def xmlencode(data):
    if data is None:
        return ''
    newdata = xmldecode(data)
    newdata = newdata.replace('&', '&amp;')
    newdata = newdata.replace('<', '&lt;')
    newdata = newdata.replace('>', '&gt;')
    newdata = newdata.replace('"', '&quot;')
    return newdata

#decode xml encoded strings
def xmldecode(data):
    if data is None:
        return ''
    newdata = data
    newdata = newdata.replace('&quot;', '"')
    newdata = newdata.replace('&gt;', '>')
    newdata = newdata.replace('&lt;', '<')
    newdata = newdata.replace('&amp;', '&')
    return newdata

SPECIAL_HANDLING_TAGS = OrderedDict([
    ('?xml', ('xmlheader', -1)),
    ('!--',  ('comment', -3)),
    ('!DOCTYPE', ('doctype', -1))
    ])

SPECIAL_HANDLING_TYPES = ['xmlheader', 'doctype', 'comment']

_OPF_PARENT_TAGS = ['package', 'metadata', 'dc-metadata', 'x-metadata', 'manifest', 'spine', 'tours', 'guide', 'bindings']

# Comments are not part of the opf data model but must survive the parse /
# rebuild round trip.  Each one is remembered as (section, index, text):
#
#   'prolog'   - before the package tag,          index unused (0)
#   'package'  - directly inside the package tag, index is the _PKGSTAGE_*
#                value in effect when the comment was seen
#   'metadata' | 'manifest' | 'spine' | 'guide' | 'bindings'
#              - inside that section, index is the number of entries already
#                parsed there, ie. the comment goes in front of entry index
#                (index == len(section) means it sits at the section end)
#   'epilog'   - after the closing package tag,   index unused (0)
#
# Keep this anchoring scheme, and the indenting used to write the comments back
# out, identical to OPFParser in src/Parsers/OPFParser.cpp.  The very same opf
# travels back and forth between both implementations, so any mismatch makes
# comments drift on every save.

_PKGSTAGE_BEFORE_METADATA = 0
_PKGSTAGE_AFTER_METADATA = 1
_PKGSTAGE_AFTER_MANIFEST = 2
_PKGSTAGE_AFTER_SPINE = 3
_PKGSTAGE_AFTER_GUIDE = 4
_PKGSTAGE_AFTER_BINDINGS = 5

_PKGSTAGE_FOR_END_TAG = {
    'metadata': _PKGSTAGE_AFTER_METADATA,
    'manifest': _PKGSTAGE_AFTER_MANIFEST,
    'spine': _PKGSTAGE_AFTER_SPINE,
    'guide': _PKGSTAGE_AFTER_GUIDE,
    'bindings': _PKGSTAGE_AFTER_BINDINGS,
    }

class Opf_Parser(object):

    def __init__(self, opfdata):
        self.opf = opfdata
        self.opos = 0
        self.package = None
        self.metadata_attr = None
        self.metadata = []
        self.manifest = []
        self.spine_attr = None
        self.spine=[]
        self.guide=[]
        self.bindings=[]
        self.comments=[]
        self.ns_remap = False
        self._pkg_stage = _PKGSTAGE_BEFORE_METADATA
        self._seen_package_end = False
        self._parseData()


    # OPF tag iterator
    def _opf_tag_iter(self):
        tcontent = last_tattr = None
        prefix = []
        while True:
            text, tag = self._parseopf()
            if text is None and tag is None:
                break
            if text is not None:
                tcontent = text.rstrip(" \r\n")
            else: # we have a tag
                ttype, tname, tattr = self._parsetag(tag)
                # comments are yielded as they are found and must not disturb
                # the text content being collected for the enclosing tag
                if ttype == 'comment':
                    yield ".".join(prefix), '!--', tattr, None
                    continue
                # remap opf namespace on tags if needed
                if tname.startswith('opf:'):
                    self.ns_remap = True
                    tname = tname[4:]
                if ttype == "begin":
                    tcontent = None
                    prefix.append(tname)
                    if tname in _OPF_PARENT_TAGS:
                        yield ".".join(prefix), tname, tattr, tcontent
                    else:
                        last_tattr = tattr
                else: # single or end
                    if ttype == "end":
                        prefix.pop()
                        # track which top level sections have already been
                        # closed so package level comments can be put back
                        # between the same pair of sections
                        if tname in _PKGSTAGE_FOR_END_TAG:
                            self._pkg_stage = max(self._pkg_stage, _PKGSTAGE_FOR_END_TAG[tname])
                        elif tname == 'package':
                            self._seen_package_end = True
                        tattr = last_tattr
                        if tattr is None:
                            tattr = OrderedDict()
                        last_tattr = None
                    elif ttype == 'single':
                        tcontent = None
                    if ttype == 'single' or (ttype == 'end' and tname not in _OPF_PARENT_TAGS):
                        yield ".".join(prefix), tname, tattr, tcontent
                    tcontent = None

    # remember a comment along with where in the opf it was found
    def _add_comment(self, prefix, special):
        ctext = '<!--' + special + '-->'
        if 'metadata' in prefix:
            self.comments.append(('metadata', len(self.metadata), ctext))
        elif 'manifest' in prefix:
            self.comments.append(('manifest', len(self.manifest), ctext))
        elif 'spine' in prefix:
            self.comments.append(('spine', len(self.spine), ctext))
        elif 'guide' in prefix:
            self.comments.append(('guide', len(self.guide), ctext))
        elif 'bindings' in prefix:
            self.comments.append(('bindings', len(self.bindings), ctext))
        elif 'package' in prefix:
            self.comments.append(('package', self._pkg_stage, ctext))
        elif self._seen_package_end:
            self.comments.append(('epilog', 0, ctext))
        else:
            self.comments.append(('prolog', 0, ctext))

    # now parse the OPF to extract manifest, spine , and metadata
    def _parseData(self):
        cnt = 0
        for prefix, tname, tattr, tcontent in self._opf_tag_iter():
            # comments
            if tname == '!--':
                self._add_comment(prefix, tattr.get('special', ''))
                continue
            # package
            if tname == "package":
                ver = tattr.pop("version", "2.0")
                uid = tattr.pop("unique-identifier","bookid")
                if self.ns_remap:
                    if "xmlns:opf" in tattr:
                        tattr.pop("xmlns:opf")
                        tattr["xmlns"] = "http://www.idpf.org/2007/opf"
                self.package = (ver, uid, tattr)
                continue
            # metadata
            if tname == "metadata":
                if self.ns_remap:
                    if not "xmlns:opf" in tattr:
                        tattr["xmlns:opf"] = "http://www.idpf.org/2007/opf"
                self.metadata_attr = tattr
                continue
            if tname in ["meta", "link"] or tname.startswith("dc:") and "metadata" in prefix:
                self.metadata.append((tname, tcontent, tattr))
                continue
            # manifest
            if tname == "item" and  "manifest" in prefix:
                nid = "xid%03d" %  cnt
                cnt += 1
                id = tattr.pop("id",nid)
                # must keep all hrefs in encoded) form
                # if relative, then no fragments so decode and then encode for safety
                href = tattr.pop("href","")
                if href.find(':') == -1:
                    href = urldecodepart(href)
                    href = urlencodepart(href)
                mtype = tattr.pop("media-type","")
                self.manifest.append((id, href, mtype, tattr))
                continue
            # spine
            if tname == "spine":
                self.spine_attr = tattr
                continue
            if tname == "itemref" and "spine" in prefix:
                idref = tattr.pop("idref","")
                self.spine.append((idref, tattr))
                continue
            # guide
            if tname == "reference" and  "guide" in prefix:
                type = tattr.pop("type","")
                title = tattr.pop("title","")
                # must keep all hrefs in quoted (encoded) form
                href = tattr.pop("href","")
                self.guide.append((type, title, href))
                continue
            # bindings
            if tname in ["mediaType", "mediatype"] and "bindings" in prefix:
                mtype = tattr.pop("media-type","")
                handler = tattr.pop("handler","")
                self.bindings.append((mtype, handler))
                continue

    # parse and return either leading text or the next tag
    def _parseopf(self):
        p = self.opos
        if p >= len(self.opf):
            return None, None
        if self.opf[p] != '<':
            res = self.opf.find('<',p)
            if res == -1 :
                res = len(self.opf)
            self.opos = res
            return self.opf[p:res], None
        # handle comment as a special case
        if self.opf[p:p+4] == '<!--':
            te = self.opf.find('-->',p+1)
            if te != -1:
                te = te+2
        else:
            te = self.opf.find('>',p+1)
            ntb = self.opf.find('<',p+1)
            if ntb != -1 and ntb < te:
                self.opos = ntb
                return self.opf[p:ntb], None
        self.opos = te + 1
        return None, self.opf[p:te+1]

    # parses tag to identify:  [tname, ttype, tattr]
    #    tname: tag name,    ttype: tag type ('begin', 'end' or 'single');
    #    tattr: dictionary of tag atributes
    def _parsetag(self, s):
        n = len(s)
        p = 1
        tname = None
        ttype = None
        tattr = OrderedDict()
        while p < n and s[p:p+1] == ' ' : p += 1
        if s[p:p+1] == '/':
            ttype = 'end'
            p += 1
            while p < n and s[p:p+1] == ' ' : p += 1
        b = p
        # handle comment special case as there may be no spaces to
        # delimit name begin or end
        if s[b:].startswith('!--'):
            p = b+3
            tname = '!--'
            ttype, backstep = SPECIAL_HANDLING_TAGS[tname]
            # keep the comment body verbatim so it round trips unchanged
            tattr['special'] = s[p:backstep]
            return ttype, tname, tattr
        while p < n and s[p:p+1] not in ('>', '/', ' ', '"', "'","\r","\n") : p += 1
        tname=s[b:p].lower()
        # remove redundant opf: namespace prefixes on opf tags
        # if tname.startswith("opf:"):
        #    tname = tname[4:]
        # more special cases
        if tname == '!doctype':
            tname = '!DOCTYPE'
        if tname in SPECIAL_HANDLING_TAGS:
            ttype, backstep = SPECIAL_HANDLING_TAGS[tname]
            tattr['special'] = s[p:backstep]
        if ttype is None:
            # parse any attributes of begin or single tags
            while s.find('=',p) != -1 :
                while p < n and s[p:p+1] == ' ' : p += 1
                b = p
                while p < n and s[p:p+1] != '=' : p += 1
                aname = s[b:p].lower()
                aname = aname.rstrip(' ')
                p += 1
                while p < n and s[p:p+1] == ' ' : p += 1
                if s[p:p+1] in ('"', "'") :
                    qt = s[p:p+1]
                    p = p + 1
                    b = p
                    # try to work around missing end quotes
                    while p < n and s[p:p+1] not in ['>', '<', qt] : p += 1
                    val = s[b:p]
                    p += 1
                else :
                    b = p
                    while p < n and s[p:p+1] not in ('>', '/', ' ') : p += 1
                    val = s[b:p]
                tattr[aname] = val
        if ttype is None:
            ttype = 'begin'
            if s.find('/',p) >= 0:
                ttype = 'single'
        return ttype, tname, tattr

    def xlate_dict(self, attr):
        keylist = []
        vallist = []
        if attr is not None:
            keylist = list(attr.keys());
            for key in keylist:
                vallist.append(attr[key])
        return (keylist, vallist)

    def get_package(self):
        (ver, uid, attr) = self.package
        (keylist, vallist) = self.xlate_dict(attr)
        return (ver, uid, keylist, vallist)

    def get_metadata_attr(self):
        (keylist, vallist) = self.xlate_dict(self.metadata_attr)
        return (keylist, vallist)

    def get_metadata(self):
        metadata = []
        for (mname, mcontent, attr) in self.metadata:
            (keylist, vallist) = self.xlate_dict(attr)
            metadata.append((mname, mcontent, keylist, vallist))
        return metadata

    def get_manifest(self):
        manlist = []
        for (id, href, mtype, attr) in self.manifest:
            (keylist, vallist) = self.xlate_dict(attr)
            manlist.append((id, href, mtype, keylist, vallist))
        return manlist

    def get_spine_attr(self):
        (keylist, vallist) = self.xlate_dict(self.spine_attr)
        return (keylist, vallist)

    def get_spine(self):
        spine = []
        for (idref, attr) in self.spine:
            (keylist, vallist) = self.xlate_dict(attr)
            spine.append((idref, keylist, vallist))
        return spine

    def get_guide(self):
        guide = []
        for (gtype, gtitle,  ghref) in self.guide:
            guide.append((gtype, gtitle, ghref))
        return guide

    def get_bindings(self):
        bindings = []
        for (mtype, handler) in self.bindings:
            bindings.append((mtype, handler))
        return bindings

    def convert_package_to_xml(self):
        xmlres = []
        (ver, uid, attr) = self.package
        xmlres.append('<package version="%s" unique-identifier="%s"' % (ver, uid))
        for key in attr:
            val = attr[key]
            val = xmlencode(val)
            xmlres.append(' %s="%s"' % (key, val))
        xmlres.append('>\n')
        return "".join(xmlres)

    def convert_metadata_attr_to_xml(self):
        xmlres = []
        attr = self.metadata_attr
        xmlres.append('  <metadata')
        for key in attr:
            val= attr[key]
            val= xmlencode(val)
            xmlres.append(' %s="%s"' % (key, val))
        xmlres.append('>\n')
        return "".join(xmlres)

    def convert_metadata_entries_to_xml(self):
        xmlres = []
        for i, (mname, mcontent, attr) in enumerate(self.metadata):
            xmlres.append(self.comments_to_xml('metadata', i, '    '))
            xmlres.append('    <%s' % mname)
            for key in attr:
                val= attr[key]
                val= xmlencode(val)
                xmlres.append(' %s="%s"' % (key, val))
            if mcontent is None or mcontent == "":
                xmlres.append('/>\n')
            else:
                content= xmlencode(mcontent)
                xmlres.append('>%s</%s>\n' % (content, mname))
        xmlres.append(self.comments_to_xml('metadata', len(self.metadata), '    '))
        return "".join(xmlres)

    def convert_manifest_entries_to_xml(self):
        xmlres = []
        for i, (id, href, mtype, attr) in enumerate(self.manifest):
            xmlres.append(self.comments_to_xml('manifest', i, '    '))
            # all hrefs should be kept in quoted (encoded) form
            xmlres.append('    <item id="%s" href="%s" media-type="%s"' % (id, href, mtype))
            for key in attr:
                val= attr[key]
                val= xmlencode(val)
                xmlres.append(' %s="%s"' % (key, val))
            xmlres.append('/>\n')
        xmlres.append(self.comments_to_xml('manifest', len(self.manifest), '    '))
        return "".join(xmlres)

    def convert_spine_attr_to_xml(self):
        xmlres = []
        attr = self.spine_attr
        xmlres.append('  <spine')
        if attr is not None:
            for key in attr:
                val= attr[key]
                val= xmlencode(val)
                xmlres.append(' %s="%s"' % (key, val))
        xmlres.append('>\n')
        return "".join(xmlres)

    def convert_spine_entries_to_xml(self):
        xmlres=[]
        for i, (idref, attr) in enumerate(self.spine):
            xmlres.append(self.comments_to_xml('spine', i, '    '))
            xmlres.append('    <itemref idref="%s"' % idref)
            if attr is not None:
                for key in attr:
                    val= attr[key]
                    val= xmlencode(val)
                    xmlres.append(' %s="%s"' % (key, val))
            xmlres.append('/>\n')
        xmlres.append(self.comments_to_xml('spine', len(self.spine), '    '))
        return "".join(xmlres)

    def convert_guide_entries_to_xml(self):
        xmlres=[]
        for i, (gtype, gtitle, ghref) in enumerate(self.guide):
            xmlres.append(self.comments_to_xml('guide', i, '    '))
            # all hrefs should already be in quoted (encoded) form
            xmlres.append('    <reference type="%s" title="%s" href="%s"/>\n' % (gtype, gtitle, ghref))
        xmlres.append(self.comments_to_xml('guide', len(self.guide), '    '))
        return "".join(xmlres)

    def convert_binding_entries_to_xml(self):
        xmlres=[]
        for i, (mtype, handler) in enumerate(self.bindings):
            xmlres.append(self.comments_to_xml('bindings', i, '    '))
            xmlres.append('  <mediaType media-type="%s" handler="%s"/>\n' % (mtype, handler))
        xmlres.append(self.comments_to_xml('bindings', len(self.bindings), '    '))
        return "".join(xmlres)

    def has_comments(self, section):
        for (csection, cindex, ctext) in self.comments:
            if csection == section:
                return True
        return False

    def comments_to_xml(self, section, index, indent):
        xmlres=[]
        for (csection, cindex, ctext) in self.comments:
            if csection == section and cindex == index:
                xmlres.append(indent + ctext + '\n')
        return "".join(xmlres)

    def rebuild_opfxml(self):
        xmlres=[]
        xmlres.append('<?xml version="1.0" encoding="utf-8"?>\n')
        xmlres.append(self.comments_to_xml('prolog', 0, ''))
        xmlres.append(self.convert_package_to_xml())
        xmlres.append(self.comments_to_xml('package', _PKGSTAGE_BEFORE_METADATA, '  '))
        xmlres.append(self.convert_metadata_attr_to_xml())
        xmlres.append(self.convert_metadata_entries_to_xml())
        xmlres.append('  </metadata>\n')
        xmlres.append(self.comments_to_xml('package', _PKGSTAGE_AFTER_METADATA, '  '))
        xmlres.append('  <manifest>\n')
        xmlres.append(self.convert_manifest_entries_to_xml())
        xmlres.append('  </manifest>\n')
        xmlres.append(self.comments_to_xml('package', _PKGSTAGE_AFTER_MANIFEST, '  '))
        xmlres.append(self.convert_spine_attr_to_xml())
        xmlres.append(self.convert_spine_entries_to_xml())
        xmlres.append('  </spine>\n')
        xmlres.append(self.comments_to_xml('package', _PKGSTAGE_AFTER_SPINE, '  '))
        (opfver, uid, attr) = self.package
        if len(self.guide) > 0 or self.has_comments('guide'):
            xmlres.append('  <guide>\n')
            xmlres.append(self.convert_guide_entries_to_xml())
            xmlres.append('  </guide>\n')
        xmlres.append(self.comments_to_xml('package', _PKGSTAGE_AFTER_GUIDE, '  '))
        if (len(self.bindings) > 0 or self.has_comments('bindings')) and opfver.startswith('3'):
            xmlres.append('  <bindings>\n')
            xmlres.append(self.convert_binding_entries_to_xml())
            xmlres.append('  </bindings>\n')
        xmlres.append(self.comments_to_xml('package', _PKGSTAGE_AFTER_BINDINGS, '  '))
        xmlres.append('</package>\n')
        xmlres.append(self.comments_to_xml('epilog', 0, ''))
        return "".join(xmlres)


def parseopf(opfdata):
    opfparser = Opf_Parser(opfdata)
    return opfparser


def main():
    argv = sys.argv
    if len(argv) < 2:
        sys.exit(0)

    if not os.path.exists(argv[1]):
        sys.exit(0)

    with open(argv[1], 'rb') as f:
        data = f.read()
        data = data.decode('utf-8')

    op = parseopf(data)
    # print(op.get_package())
    # print(op.get_metadata_attr())
    # print(op.get_metadata())
    # print(op.get_manifest())
    # print(op.get_spine_attr())
    # print(op.get_spine())
    # print(op.get_guide())
    # print(op.get_bindings())
    print(op.rebuild_opfxml())
    return 0

if __name__ == '__main__':
    sys.exit(main())
