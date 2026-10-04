"""List the engine types each runtime source file uses without its own includes bringing them in -- as a game build
without a shared PCH sees the file.

    python check_game_includes.py [--modules DreamGUI,DreamGUIRenderer,...] [--engine <Engine dir>] [--rebuild-index]

Why this exists: RunUAT BuildPlugin compiles the plugin's game target with no shared PCH, so a file has only what it
includes itself plus whatever its unity-blob neighbours included first. The editor target hides the difference (its
PCH pulls in nearly the whole engine), and every split regroups the blobs -- so a header that compiled for years
stops compiling when a neighbour leaves. Run this after moving files and before the build, instead of finding out
from a half-hour BuildPlugin.

Every runtime .cpp AND header is checked on its own. Its includes are followed through the plugin and the engine the
way a Win64 game build compiles them: what sits under WITH_EDITOR or WITH_EDITORONLY_DATA is dropped, and so are the
engine's deprecated transitive includes (UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_*), which the latest include order
turns off. A header's .generated.h brings in what every UHT-generated header includes. Each engine type the file
names is then reported as

  undeclared   nothing in reach declares it at all;
  incomplete   something in reach only forward-declares it. A .cpp is reported for any use, since it nearly always
               touches the object; a header only for a use that needs the definition -- a member or base by value,
               a template argument other than a smart pointer's, a scope (X::) or a construction.

Engine macros and globals are checked the same way: a function-like macro the engine defines (a trace or stats
scope, ENQUEUE_RENDER_COMMAND, LOCTEXT) and a global it declares extern (GFrameCounter, GUndo, GEngine) are reported as

  macro        the file uses it, and no header in reach defines it;
  global       the file uses it, and no header in reach declares it.

Two checks need no type at all:

  editor header  a game build of the file includes a header that exists only in the engine's Editor or Developer
                 source, which a game target does not compile;
  editor module  a runtime module's Build.cs depends on an engine Editor module outside a branch that only an
                 editor target takes (an `if` whose condition names Editor, bBuildEditor or WITH_EDITOR).

The output is a list of candidates to read, not a verdict: a name used only through a pointer in a function body, an
opaque enum declared with its underlying type, or a member that happens to share an engine type's name is fine as
it is. The engine index -- definitions (types with a body, enums, aliases, and what the shader-parameter and
delegate macros declare), forward declarations, macros, extern globals, and the editor and developer headers -- is
built once per engine and cached in the temp directory.
"""
import argparse
import hashlib
import json
import os
import pickle
import re
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN = os.path.normpath(os.path.join(HERE, os.pardir, os.pardir))
DEFAULT_ENGINE = os.environ.get('UE_ENGINE_DIR', r'C:\Program Files\Epic Games\UE_5.8\Engine')

INCLUDE = re.compile(r'^\s*#\s*include\s+[<"]([^">]+)[">]', re.M)
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
STRING = re.compile(r'"(?:\\.|[^"\\\n])*"')
DEFINE = [
    re.compile(r'\b(?:class|struct)\s+(?:[A-Z0-9_]+_API\s+)?(?:alignas\([^)]*\)\s*)?([UAFETSI][A-Za-z0-9_]+)\s*(?:<[^;{]*?>)?\s*(?:final\s*)?(?::(?!:)[^;{]*)?\{'),
    re.compile(r'\benum\s+class\s+([A-Z]\w+)\s*(?::\s*\w+\s*)?\{'),
    re.compile(r'\benum\s+(E[A-Z]\w+)\s*(?::\s*\w+\s*)?\{'),
    re.compile(r'\bnamespace\s+(E[A-Z]\w+)\s*\{\s*enum\s+\w+\s*(?::\s*\w+\s*)?\{'),
    re.compile(r'\busing\s+([UAFETSI][A-Z]\w+)\s*='),
    re.compile(r'\btypedef\s+[^;{}]*?\b([UAFETSI][A-Z]\w+)\s*;'),
    re.compile(r'\bBEGIN_(?:GLOBAL_|UNIFORM_BUFFER_)?SHADER_PARAMETER_STRUCT(?:_WITH_CONSTRUCTOR)?\(\s*([FT][A-Za-z0-9_]+)'),
    re.compile(r'\bDECLARE_(?:DYNAMIC_|TS_)?(?:MULTICAST_|SPARSE_)?(?:DELEGATE|EVENT)(?:_[A-Za-z]+)*\(\s*(?:\w+\s*,\s*)?(F[A-Za-z0-9_]+)'),
]
FORWARD = [
    re.compile(r'^\s*(?:class|struct)\s+(?:[A-Z0-9_]+_API\s+)?([UAFETSI][A-Za-z0-9_]+)\s*;', re.M),
    re.compile(r'^\s*enum\s+class\s+([A-Z]\w+)\s*(?::\s*\w+\s*)?;', re.M),
]
# "class UTexture* Texture;" declares UTexture as it goes.
ELABORATED = re.compile(r'\b(?:class|struct)\s+([UAFETSI][A-Za-z0-9_]+)\s*[*&>,)]')
# UHT's reflection mirrors of core types, not where C++ gets them from.
SKIP_HEADERS = ('NoExportTypes.h',)
GLOBALS = {'GEngine': 'Engine/Engine.h'}
# What CoreMinimal.h always brings, and names the index cannot place: the core containers (the index mistakes
# look-alikes elsewhere for them), and the string types and FPlatform* typedefs stamped out by macro-chosen headers.
ALWAYS = {'FString', 'FAnsiString', 'FUtf8String', 'FWideString', 'EPropertyChangeType',
          'TMap', 'TMultiMap', 'TSet', 'TArray', 'TArrayView', 'TSortedMap'}
# What every UHT-generated header includes (UE 5.8).
GENERATED_INCLUDES = ('UObject/ObjectMacros.h', 'UObject/ReflectedTypeAccessors.h', 'Templates/IsUEnumClass.h',
                      'UObject/ScriptMacros.h', 'Templates/NoDestroy.h')
ROOT_DIRS = ('Public', 'Classes', 'Internal')
POINTERS = ('TObjectPtr', 'TWeakObjectPtr', 'TSoftObjectPtr', 'TSoftClassPtr', 'TSubclassOf', 'TScriptInterface', 'TLazyObjectPtr',
            'TSharedPtr', 'TSharedRef', 'TWeakPtr', 'TUniquePtr', 'TStrongObjectPtr', 'TObjectKey', 'TWeakInterfacePtr', 'TNotNull')
# The macros whose value a Win64 game build of this plugin knows; any other condition keeps both branches.
KNOWN = {'WITH_EDITOR': 0, 'WITH_EDITORONLY_DATA': 0, 'UE_EDITOR': 0, 'UE_GAME': 1, 'PLATFORM_WINDOWS': 1,
         'PLATFORM_MAC': 0, 'PLATFORM_LINUX': 0, 'PLATFORM_ANDROID': 0, 'PLATFORM_IOS': 0}
DEPRECATED_ORDER = re.compile(r'\bUE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_\d+_\d+\b')
DIRECTIVE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')
# Bump when the index changes shape, so a cache from an older version of this script is not read.
INDEX_VERSION = 2
MACRO_DEFINE = re.compile(r'^\s*#\s*define\s+([A-Z][A-Z0-9_]*)\s*\(', re.M)
# A function-like use of an upper-case name with an underscore in it: TRACE_CPUPROFILER_EVENT_SCOPE(...), LOCTEXT(...).
MACRO_USE = re.compile(r'\b([A-Z][A-Z0-9]*_[A-Z0-9_]+)\s*\(')
EXTERN_GLOBAL = re.compile(r'\bextern\s+(?:[A-Z0-9_]+_API\s+)?[^;(){}=]*?\b(G[A-Z][A-Za-z0-9_]*)\s*(?:\[[^\]]*\]\s*)?;')
# A global named on its own, not a member reached through ::, . or -> (FDreamGUIObjectVersion::GUID is no global).
GLOBAL_USE = re.compile(r'(?<![:.>\w])(G[A-Z][A-Za-z0-9_]*)\b')
# Reflection and boilerplate macros UHT and the module system read; their own headers come with any reflected type.
SKIP_MACROS = {'GENERATED_BODY', 'GENERATED_UCLASS_BODY', 'GENERATED_USTRUCT_BODY', 'GENERATED_IINTERFACE_BODY',
               'GENERATED_UINTERFACE_BODY', 'IMPLEMENT_MODULE', 'IMPLEMENT_GAME_MODULE', 'IMPLEMENT_PRIMARY_GAME_MODULE',
               'DEFINE_LOG_CATEGORY', 'DECLARE_LOG_CATEGORY_EXTERN', 'UE_LOG', 'UE_CLOG', 'UE_LOGFMT'}


def strip(text):
    return STRING.sub('""', COMMENT.sub('', text))


def evaluate(cond):
    """True, False, or None when the condition names a macro not in KNOWN."""
    c = DEPRECATED_ORDER.sub('0', COMMENT.sub('', cond).strip())
    c = re.sub(r'defined\s*\(\s*(\w+)\s*\)|defined\s+(\w+)', lambda m: '(%s)' % (m.group(1) or m.group(2)), c)
    for name, value in KNOWN.items():
        c = re.sub(r'\b%s\b' % name, str(value), c)
    if re.search(r'[A-Za-z_]\w*', c):
        return None
    c = c.replace('&&', ' and ').replace('||', ' or ').replace('!', ' not ')
    try:
        return bool(eval(c, {'__builtins__': {}}, {}))
    except Exception:
        return None


def game_text(text):
    """text with the lines a game build does not compile blanked out."""
    out, stack, active = [], [], True
    for line in text.split('\n'):
        m = DIRECTIVE.match(line)
        if not m:
            out.append(line if active else '')
            continue
        kind, rest = m.group(1), m.group(2)
        if kind in ('if', 'ifdef', 'ifndef'):
            if kind == 'if':
                v = evaluate(rest)
            else:
                words = rest.split()
                v = bool(KNOWN[words[0]]) if words and words[0] in KNOWN else None
                if v is not None and kind == 'ifndef':
                    v = not v
            # [enclosing branch active, some branch of this #if known taken]
            stack.append([active, v is True])
            active = active and v is not False
        elif kind == 'elif' and stack:
            parent, taken = stack[-1]
            v = evaluate(rest)
            active = parent and not taken and v is not False
            stack[-1][1] = taken or v is True
        elif kind == 'else' and stack:
            parent, taken = stack[-1]
            active = parent and not taken
        elif kind == 'endif' and stack:
            active = stack.pop()[0]
        out.append('')
    return '\n'.join(out)


SKIPPED_DIRS = ('Private', 'ThirdParty', 'Binaries', 'Intermediate', 'Content', 'Resources', 'Shaders', 'Tests',
                'Android', 'Apple', 'IOS', 'Mac', 'Linux', 'Unix', 'TVOS', 'VisionOS', 'Solaris')


def public_roots(bases):
    roots = []
    for base in bases:
        for dirpath, dirnames, _ in os.walk(base):
            roots.extend(os.path.join(dirpath, d) for d in dirnames if d in ROOT_DIRS)
            # Platform directories and the private, test and third-party trees define look-alikes of core names
            # that a Win64 build never sees.
            dirnames[:] = [d for d in dirnames if d not in SKIPPED_DIRS]
    return roots


def build_engine_index(engine):
    roots = public_roots((os.path.join(engine, 'Source', 'Runtime'), os.path.join(engine, 'Plugins')))
    by_rel, defines, forwards, includes, macros, globals_ = {}, {}, {}, {}, {}, {}
    for root in roots:
        for dirpath, _, files in os.walk(root):
            for f in files:
                if not f.endswith(('.h', '.inl')):
                    continue
                full = os.path.join(dirpath, f)
                by_rel.setdefault(os.path.relpath(full, root).replace(os.sep, '/'), full)
                try:
                    text = open(full, encoding='utf-8', errors='replace').read()
                except OSError:
                    continue
                game = game_text(text)
                includes[full] = INCLUDE.findall(game)
                for name in MACRO_DEFINE.findall(game):
                    macros.setdefault(name, set()).add(full)
                for name in EXTERN_GLOBAL.findall(strip(game)):
                    globals_.setdefault(name, set()).add(full)
                if f in SKIP_HEADERS:
                    continue
                body = strip(text)
                for rx in DEFINE:
                    for name in rx.findall(body):
                        defines.setdefault(name, set()).add(full)
                for rx in FORWARD:
                    for name in rx.findall(body):
                        forwards.setdefault(name, set()).add(full)
    # Headers a game target never compiles: those of the engine's Editor and Developer modules, by the path an include
    # names them with, and the Editor modules by name.
    editor_headers = set()
    for root in public_roots((os.path.join(engine, 'Source', 'Editor'), os.path.join(engine, 'Source', 'Developer'))):
        for dirpath, _, files in os.walk(root):
            for f in files:
                if f.endswith(('.h', '.inl')):
                    editor_headers.add(os.path.relpath(os.path.join(dirpath, f), root).replace(os.sep, '/'))
    editor_dir = os.path.join(engine, 'Source', 'Editor')
    editor_modules = set(d for d in os.listdir(editor_dir) if os.path.isdir(os.path.join(editor_dir, d))) if os.path.isdir(editor_dir) else set()
    return by_rel, defines, forwards, includes, macros, globals_, editor_headers, editor_modules


def load_engine_index(engine, rebuild):
    key = hashlib.sha1(os.path.normcase(os.path.abspath(engine)).encode('utf-8')).hexdigest()[:12]
    cache = os.path.join(tempfile.gettempdir(), 'dreamgui_game_includes_v%d_%s.pickle' % (INDEX_VERSION, key))
    if os.path.exists(cache) and not rebuild:
        return pickle.load(open(cache, 'rb'))
    print('indexing %s (once per engine)' % engine)
    index = build_engine_index(engine)
    pickle.dump(index, open(cache, 'wb'))
    return index


def runtime_modules():
    plugin = json.load(open(os.path.join(PLUGIN, 'DreamGUI.uplugin'), encoding='utf-8-sig'))
    return [m['Name'] for m in plugin.get('Modules', []) if m.get('Type') == 'Runtime']


EDITOR_CONDITION = re.compile(r'Editor|bBuildEditor|WITH_EDITOR')
DEPENDENCY_LIST = re.compile(r'(?:Public|Private)DependencyModuleNames\s*\.\s*(?:AddRange|Add)\s*\(')


def build_cs_editor_dependencies(build_cs, editor_modules):
    """(module, line) for each engine Editor module the Build.cs depends on outside an editor-only branch."""
    text = re.sub(r'//[^\n]*|/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), open(build_cs, encoding='utf-8-sig', errors='replace').read(), flags=re.S)
    # The condition of every block that is open at each brace: a block an `if` opens carries its condition.
    found, stack, pending = [], [], None
    i = 0
    while i < len(text):
        m = re.match(r'\bif\s*\(', text[i:])
        if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            depth, j = 0, i + m.end() - 1
            while j < len(text):
                if text[j] == '(':
                    depth += 1
                elif text[j] == ')':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            pending = text[i:j + 1]
            i = j + 1
            continue
        c = text[i]
        if c == '{':
            stack.append(pending or '')
            pending = None
        elif c == '}':
            if stack:
                stack.pop()
        elif c == ';':
            pending = None
        m = DEPENDENCY_LIST.match(text, i)
        if m:
            depth, j = 0, m.end() - 1
            while j < len(text):
                if text[j] == '(':
                    depth += 1
                elif text[j] == ')':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            guarded = any(EDITOR_CONDITION.search(cond) for cond in stack) or (pending and EDITOR_CONDITION.search(pending))
            if not guarded:
                for name in re.findall(r'"([A-Za-z0-9_]+)"', text[m.end():j]):
                    if name in editor_modules:
                        found.append((name, text.count('\n', 0, i) + 1))
            i = j + 1
            continue
        i += 1
    return found


def needs_definition(body, name):
    """The first use of name in a header that needs the complete type, or None."""
    for m in re.finditer(r'\b%s\b' % re.escape(name), body):
        before = body[max(0, m.start() - 60):m.start()]
        after = body[m.end():m.end() + 30]
        if re.search(r'\b(?:class|struct|friend\s+class|friend\s+struct)\s+$', before):
            continue
        if re.match(r'\s*(?:const\s*)?[*&]', after):
            continue
        if re.search(r'\b(%s)\s*<\s*(?:const\s+)?$' % '|'.join(POINTERS), before) and re.match(r'\s*>', after):
            continue
        start = body.rfind('\n', 0, m.start()) + 1
        end = body.find('\n', m.end())
        return body[start:end if end >= 0 else None].strip()[:120]
    return None


def main():
    ap = argparse.ArgumentParser(description='Engine types a PCH-less game build of the plugin cannot see.')
    ap.add_argument('--modules', default=','.join(runtime_modules()))
    ap.add_argument('--engine', default=DEFAULT_ENGINE)
    ap.add_argument('--rebuild-index', action='store_true')
    ns = ap.parse_args()
    by_rel, defines, forwards, engine_includes, macros, extern_globals, editor_headers, editor_modules = load_engine_index(ns.engine, ns.rebuild_index)

    source = os.path.join(PLUGIN, 'Source')
    roots = [os.path.join(source, m, sub) for m in os.listdir(source) for sub in ('Public', 'Private', 'Classes')
             if os.path.isdir(os.path.join(source, m, sub))]
    files = {}
    for dirpath, _, names in os.walk(source):
        for f in names:
            if f.endswith(('.h', '.cpp', '.inl')):
                full = os.path.join(dirpath, f)
                files[full] = game_text(open(full, encoding='utf-8-sig', errors='replace').read())
    # Types the plugin defines itself. A plugin file forward-declaring an ENGINE type does not make it the plugin's.
    plugin_defined = set()
    plugin_macros = set()
    for text in files.values():
        body = strip(text)
        for rx in DEFINE:
            plugin_defined.update(rx.findall(body))
        plugin_macros.update(re.findall(r'^\s*#\s*define\s+([A-Z][A-Z0-9_]*)', text, re.M))

    def resolve(frm, inc):
        for c in [os.path.normpath(os.path.join(os.path.dirname(frm), inc))] + [os.path.normpath(os.path.join(r, inc)) for r in roots]:
            if c in files:
                return c
        return None

    engine_cache = {}

    def engine_closure(header):
        if header not in engine_cache:
            seen, stack = {header}, [header]
            while stack:
                cur = stack.pop()
                for inc in engine_includes.get(cur, ()):
                    nxt = os.path.normpath(os.path.join(os.path.dirname(cur), inc))
                    if nxt not in engine_includes:
                        nxt = by_rel.get(inc)
                    if nxt and nxt not in seen:
                        seen.add(nxt)
                        stack.append(nxt)
            engine_cache[header] = seen
        return engine_cache[header]

    closure_cache = {}

    def closure(f):
        """(plugin files, engine headers) a game build of f reaches."""
        if f not in closure_cache:
            plug, eng, stack = {f}, set(), [f]
            while stack:
                cur = stack.pop()
                for inc in INCLUDE.findall(files.get(cur, '')):
                    p = resolve(cur, inc)
                    if p:
                        if p not in plug:
                            plug.add(p)
                            stack.append(p)
                        continue
                    for e in [by_rel.get(g) for g in GENERATED_INCLUDES] if inc.endswith('.generated.h') else [by_rel.get(inc)]:
                        if e:
                            eng |= engine_closure(e)
            closure_cache[f] = (plug, eng)
        return closure_cache[f]

    modules = set(ns.modules.split(','))
    token = re.compile(r'\b([UAFETSI][A-Z][A-Za-z0-9_]+|GEngine)\b')
    reported = 0
    for f in sorted(files):
        rel = os.path.relpath(f, source).replace(os.sep, '/')
        if rel.split('/')[0] not in modules or not f.endswith(('.cpp', '.h')):
            continue
        body = strip(files[f])
        plug, have = closure(f)
        declared_here = set()
        for pf in plug:
            pbody = strip(files[pf])
            for rx in FORWARD:
                declared_here.update(rx.findall(pbody))
            declared_here.update(ELABORATED.findall(pbody))
        found = []
        for inc in INCLUDE.findall(files[f]):
            if resolve(f, inc) is None and inc not in by_rel and inc in editor_headers:
                found.append(('editor header', inc, 'an Editor or Developer module of the engine', ''))
        for name in sorted(set(MACRO_USE.findall(body))):
            if name in SKIP_MACROS or name in plugin_macros or name.endswith('_API') or name not in macros:
                continue
            if not macros[name] & have:
                where = ' | '.join(sorted(os.path.relpath(d, ns.engine).replace(os.sep, '/') for d in macros[name])[:2])
                found.append(('macro', name, where, ''))
        for name in sorted(set(GLOBAL_USE.findall(body))):
            # An engine global has a lower-case letter in its name (GFrameCounter, GRHISupportsX); an all-capitals name
            # such as GUID is a constant or a type that happens to start with G.
            if name in GLOBALS or name not in extern_globals or name in plugin_defined or name.upper() == name:
                continue
            if not extern_globals[name] & have:
                where = ' | '.join(sorted(os.path.relpath(d, ns.engine).replace(os.sep, '/') for d in extern_globals[name])[:2])
                found.append(('global', name, where, ''))
        for name in sorted(set(token.findall(body))):
            if name in plugin_defined or name in ALWAYS or name.startswith('FPlatform'):
                continue
            if name in GLOBALS:
                header = by_rel.get(GLOBALS[name])
                if header and header not in have:
                    found.append(('undeclared', name, GLOBALS[name], ''))
                continue
            defs = defines.get(name)
            if not defs or defs & have:
                continue
            where = ' | '.join(sorted(os.path.relpath(d, ns.engine).replace(os.sep, '/') for d in defs)[:2])
            if not (name in declared_here or forwards.get(name, set()) & have):
                found.append(('undeclared', name, where, ''))
            elif f.endswith('.cpp'):
                found.append(('incomplete', name, where, ''))
            else:
                use = needs_definition(body, name)
                if use:
                    found.append(('incomplete', name, where, use))
        if found:
            reported += 1
            print(rel)
            for kind, name, where, use in found:
                print('    %-10s %-40s %s%s' % (kind, name, where, ('   <- ' + use) if use else ''))
    for module in sorted(modules):
        build_cs = os.path.join(source, module, module + '.Build.cs')
        if not os.path.isfile(build_cs):
            continue
        dependencies = build_cs_editor_dependencies(build_cs, editor_modules)
        if dependencies:
            reported += 1
            print('%s/%s.Build.cs' % (module, module))
            for name, line in dependencies:
                print('    %-10s %-40s line %d, outside an editor-only branch' % ('editor module', name, line))
    print('%d file(s) to read' % reported)


if __name__ == '__main__':
    main()
