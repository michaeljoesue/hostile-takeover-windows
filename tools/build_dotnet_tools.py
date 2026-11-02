#!/usr/bin/env python3
"""Build Hostile Takeover's C# tools for .NET Framework 4.x with Mono's mcs.

The tools were written for .NET 1.1-3.5 and Visual Studio 2003-2008 on
Windows. Windows 11 ships .NET Framework 4.8 but not the old runtimes, and
the mix of project formats can't all be read by xbuild. This script reads the
file lists straight out of each .csproj (both the VS2003 and MSBuild
formats), matches file names case-insensitively, compiles .resx resources and
builds each tool with mcs. The results run on stock Windows 10/11 and on Mono.

Usage: build_dotnet_tools.py OUTDIR [tool ...]
"""

import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))

# tool name -> project file (relative to ROOT). The data build uses all but
# the editor (mgui); mcl and mgui share m.dll's sources via m/m.csproj.
PROJECTS = {
    'acrunch': 'acrunch/acrunch.csproj',
    'bcr2': 'bcr2/bcr2.csproj',
    'packpdb2': 'packpdb2/packpdb2.csproj',
    'palbin': 'palbin/palbin.csproj',
    'paltool': 'paltool/PalTool.csproj',
    'shadowmap': 'shadowmap/shadowmap.csproj',
    'stringtable': 'StringTable/StringTable.csproj',
    'ppini': 'ppini/ppini.csproj',
    'markversion': 'MarkVersion/MarkVersion.csproj',
    'm': 'm/m.csproj',
    'mcl': 'm/mcl/mcl.csproj',
    'mgui': 'm/mgui/mgui.csproj',
}

FRAMEWORK_REFS = {'System', 'System.Data', 'System.Drawing', 'System.Xml',
                  'System.Windows.Forms', 'System.Design',
                  'System.Runtime.Serialization.Formatters.Soap'}


def find_case_insensitive(base, rel):
    """Resolve rel (with \\ or /) under base, ignoring case per component."""
    path = base
    for part in re.split(r'[\\/]+', rel):
        if part in ('', '.'):
            continue
        if part == '..':
            path = os.path.dirname(path)
            continue
        cand = os.path.join(path, part)
        if os.path.exists(cand):
            path = cand
            continue
        matches = [e for e in os.listdir(path) if e.lower() == part.lower()]
        if not matches:
            return None
        path = os.path.join(path, matches[0])
    return path


def strip_ns(tag):
    return tag.split('}', 1)[-1]


def parse_project(projpath):
    """Return dict(name, kind, sources, resources, refs, projrefs, unsafe, rootns)."""
    base = os.path.dirname(projpath)
    tree = ET.parse(projpath)
    root = tree.getroot()
    info = dict(sources=[], resources=[], refs=set(), projrefs=[],
                unsafe=False, rootns=None, name=None, kind='Exe')
    if strip_ns(root.tag) == 'VisualStudioProject':
        # VS2003 format
        for el in root.iter():
            tag = strip_ns(el.tag)
            if tag == 'Settings':
                info['name'] = el.get('AssemblyName')
                info['kind'] = el.get('OutputType', 'Exe')
                info['rootns'] = el.get('RootNamespace')
            elif tag == 'Config' and el.get('AllowUnsafeBlocks', '').lower() == 'true':
                info['unsafe'] = True
            elif tag == 'Reference':
                if el.get('Project'):
                    info['projrefs'].append(el.get('Name'))
                elif el.get('AssemblyName') in FRAMEWORK_REFS:
                    info['refs'].add(el.get('AssemblyName'))
                elif el.get('HintPath'):
                    info['refs'].add(('hint', el.get('HintPath'), el.get('AssemblyName')))
            elif tag == 'File':
                # Linked (shared) files keep their real location in Link
                rel = el.get('Link') or el.get('RelPath')
                action = el.get('BuildAction')
                if action == 'Compile' and rel.lower().endswith('.cs'):
                    info['sources'].append(rel)
                elif action == 'EmbeddedResource':
                    info['resources'].append((rel, el.get('DependentUpon')))
    else:
        for el in root.iter():
            tag = strip_ns(el.tag)
            if tag == 'AssemblyName' and el.text:
                info['name'] = el.text
            elif tag == 'OutputType' and el.text:
                info['kind'] = el.text
            elif tag == 'RootNamespace' and el.text:
                info['rootns'] = el.text
            elif tag == 'AllowUnsafeBlocks' and (el.text or '').lower() == 'true':
                info['unsafe'] = True
            elif tag == 'Compile':
                info['sources'].append(el.get('Include'))
            elif tag == 'EmbeddedResource':
                dep = None
                for child in el:
                    if strip_ns(child.tag) == 'DependentUpon':
                        dep = child.text
                info['resources'].append((el.get('Include'), dep))
            elif tag == 'Reference':
                name = el.get('Include').split(',')[0]
                hint = None
                for child in el:
                    if strip_ns(child.tag) == 'HintPath':
                        hint = child.text
                if name in FRAMEWORK_REFS:
                    info['refs'].add(name)
                elif hint:
                    info['refs'].add(('hint', hint, name))
            elif tag == 'ProjectReference':
                info['projrefs'].append(el.get('Include'))
    info['base'] = base
    return info


def resource_name(info, rel, dep):
    """Manifest resource name, following MSBuild/VS conventions."""
    fname = os.path.basename(rel.replace('\\', '/'))
    stem, ext = os.path.splitext(fname)
    if ext.lower() == '.resx':
        # Forms: name the resource after the class in the dependent .cs file
        cs = dep or (stem + '.cs')
        cspath = find_case_insensitive(info['base'], os.path.join(os.path.dirname(rel.replace('\\', '/')), cs))
        if cspath:
            text = open(cspath, encoding='utf-8', errors='replace').read()
            ns = re.search(r'^\s*namespace\s+([\w.]+)', text, re.M)
            cls = re.search(r'\bclass\s+(\w+)', text)
            if cls:
                return (ns.group(1) + '.' if ns else '') + cls.group(1) + '.resources'
        return ((info['rootns'] + '.') if info['rootns'] else '') + stem + '.resources'
    # Non-resx embedded files: RootNamespace.folder.file
    relparts = [p for p in re.split(r'[\\/]+', rel) if p not in ('', '.', '..')]
    return '.'.join(([info['rootns']] if info['rootns'] else []) + relparts)


def build(tool, outdir, built):
    if tool in built:
        return built[tool]
    projpath = os.path.join(ROOT, PROJECTS[tool])
    info = parse_project(projpath)
    objdir = os.path.join(outdir, 'obj', tool)
    os.makedirs(objdir, exist_ok=True)

    args = ['mcs', '-nologo', '-warn:0', '-optimize+', '-sdk:4.5']
    kind = info['kind'].lower()
    target = {'exe': 'exe', 'winexe': 'winexe', 'library': 'library'}[kind]
    ext = '.dll' if target == 'library' else '.exe'
    # Libraries keep their assembly name; programs are named after the tool,
    # in lower case, as the data makefile expects.
    outname = ((info['name'] or tool) if target == 'library' else tool) + ext
    args.append('-target:' + target)
    args.append('-out:' + os.path.join(outdir, outname))
    if info['unsafe'] or tool in ('acrunch', 'palbin', 'paltool', 'bcr2', 'shadowmap', 'm'):
        args.append('-unsafe')
    for r in sorted(r for r in info['refs'] if isinstance(r, str)):
        args.append('-r:' + r + '.dll')
    for r in info['refs']:
        if isinstance(r, tuple):
            _, hint, name = r
            # Third-party DLL from bin/ (e.g. SharpZipLib)
            src = os.path.join(ROOT, 'bin', name + '.dll')
            if os.path.exists(src):
                shutil.copy(src, outdir)
                args.append('-r:' + os.path.join(outdir, name + '.dll'))
    for pr in info['projrefs']:
        dep = None
        prl = pr.replace('\\', '/').lower()
        for t, p in PROJECTS.items():
            if prl.endswith(os.path.basename(p).lower()) or prl == t.lower():
                dep = t
        if dep is None:
            sys.exit('%s: unknown project reference %s' % (tool, pr))
        args.append('-r:' + build(dep, outdir, built))
    for rel in info['sources']:
        path = find_case_insensitive(info['base'], rel)
        if path is None:
            sys.exit('%s: source not found: %s' % (tool, rel))
        args.append(path)
    for rel, dep in info['resources']:
        path = find_case_insensitive(info['base'], rel)
        if path is None:
            print('%s: warning: resource not found: %s' % (tool, rel))
            continue
        name = resource_name(info, rel, dep)
        if path.lower().endswith('.resx'):
            res = os.path.join(objdir, name)
            subprocess.check_call(['resgen', '-usesourcepath', path, res],
                                  stdout=subprocess.DEVNULL)
            args.append('-resource:%s,%s' % (res, name))
        else:
            args.append('-resource:%s,%s' % (path, name))
    print('==> %s -> %s' % (tool, outname))
    subprocess.check_call(args)
    result = os.path.join(outdir, outname)
    built[tool] = result
    return result


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    outdir = os.path.abspath(sys.argv[1])
    os.makedirs(outdir, exist_ok=True)
    tools = sys.argv[2:] or [t for t in PROJECTS if t != 'm']
    built = {}
    for t in tools:
        build(t, outdir, built)
    shutil.rmtree(os.path.join(outdir, 'obj'), ignore_errors=True)


if __name__ == '__main__':
    main()
