#!/usr/bin/env python3
"""Translate explicit C format markers using Clang's target-aware type AST.

Only build-tree outputs are written. Expressions are evaluated once, in source
order, using block-local argument slots. No target binary is executed.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys


MARKERS = {"HX_PRINT", "HX_PRINTLN", "HX_EPRINTLN", "HX_FORMAT",
           "HX_FPRINT", "HX_FPRINTLN", "HX_FFORMAT"}


class CodegenError(Exception):
    pass


def quoted_end(text, start):
    quote = text[start]
    p = start + 1
    while p < len(text):
        if text[p] == "\\":
            p += 2
        elif text[p] == quote:
            return p + 1
        else:
            p += 1
    raise CodegenError("unterminated C string/character literal")


def masked(text):
    result = list(text)
    p = 0
    while p < len(text):
        start = p
        if text.startswith("//", p):
            p = text.find("\n", p)
            if p < 0:
                p = len(text)
        elif text.startswith("/*", p):
            end = text.find("*/", p + 2)
            if end < 0:
                raise CodegenError("unterminated comment")
            p = end + 2
        elif text[p] in "\"'":
            p = quoted_end(text, p)
        else:
            p += 1
            continue
        result[start:p] = ["\n" if c == "\n" else " "
                           for c in text[start:p]]
    return "".join(result)


def arguments(text, start):
    clean = masked(text)
    stack = [")"]
    result = []
    begin = start + 1
    p = begin
    while p < len(text):
        c = clean[p]
        if c in "([{":
            stack.append({"(": ")", "[": "]", "{": "}"}[c])
        elif c in ")]}":
            if not stack or c != stack.pop():
                raise CodegenError("unbalanced argument expression")
            if not stack:
                result.append(text[begin:p].strip())
                return result, p + 1
        elif c == "," and len(stack) == 1:
            result.append(text[begin:p].strip())
            begin = p + 1
        p += 1
    raise CodegenError("unterminated format call")


def literal(text):
    """Decode narrow C literals, including Clang's canonical octal bytes."""
    data = bytearray()
    p = 0
    escapes = {"n": 10, "r": 13, "t": 9, "b": 8, "f": 12,
               "v": 11, "a": 7, "\\": 92, '"': 34, "'": 39, "?": 63}
    found = False
    while p < len(text):
        if text[p].isspace():
            p += 1
            continue
        if text[p] != '"':
            raise CodegenError("this marker requires a narrow string literal")
        found = True
        end = quoted_end(text, p)
        p += 1
        while p < end - 1:
            c = text[p]
            p += 1
            if c != "\\":
                data.extend(c.encode("utf-8"))
                continue
            c = text[p]
            p += 1
            if c in escapes:
                data.append(escapes[c])
            elif c in "01234567":
                digits = c
                while p < end - 1 and len(digits) < 3 and text[p] in "01234567":
                    digits += text[p]
                    p += 1
                value = int(digits, 8)
                if value > 255:
                    raise CodegenError("escape exceeds one UTF-8 byte")
                data.append(value)
            elif c == "x":
                begin = p
                while p < end - 1 and text[p] in "0123456789abcdefABCDEF":
                    p += 1
                if begin == p or int(text[begin:p], 16) > 255:
                    raise CodegenError("invalid hexadecimal byte escape")
                data.append(int(text[begin:p], 16))
            elif c in "uU":
                size = 4 if c == "u" else 8
                data.extend(chr(int(text[p:p + size], 16)).encode("utf-8"))
                p += size
            elif c == "\n":
                pass
            else:
                raise CodegenError("unsupported C escape: \\" + c)
        p = end
    if not found:
        raise CodegenError("missing format string")
    return data.decode("utf-8", errors="strict")


def c_quote(text):
    body = ""
    for c in text.encode("utf-8"):
        if c in (34, 92):
            body += "\\" + chr(c)
        elif 32 <= c < 127:
            body += chr(c)
        else:
            body += "\\%03o" % c
    return '"' + body + '"'


def split_field(field):
    clean = masked(field)
    stack = []
    for p, c in enumerate(clean):
        if c in "([{":
            stack.append(c)
        elif c in ")]}":
            if not stack:
                raise CodegenError("unbalanced interpolation expression")
            stack.pop()
        elif not stack and (c == ":" or
                            (c == "!" and field[p + 1:p + 2] in ("s", "r"))):
            return field[:p].strip(), field[p:]
    return field.strip(), ""


def interpolate(pattern):
    values = []

    def field_id(expression):
        if not expression:
            raise CodegenError("empty interpolation expression")
        index = len(values)
        values.append(expression)
        return str(index)

    result = ""
    p = 0
    while p < len(pattern):
        if pattern[p:p + 2] in ("{{", "}}"):
            result += pattern[p:p + 2]
            p += 2
            continue
        if pattern[p] != "{":
            result += pattern[p]
            p += 1
            continue
        begin = p + 1
        p = begin
        depth = 0
        while p < len(pattern):
            if pattern[p] in "\"'":
                p = quoted_end(pattern, p)
                continue
            if pattern[p] == "{":
                depth += 1
            elif pattern[p] == "}":
                if not depth:
                    break
                depth -= 1
            p += 1
        if p == len(pattern):
            raise CodegenError("unclosed interpolation field")
        expression, spec = split_field(pattern[begin:p])
        index = field_id(expression)
        spec = re.sub(r"\{([^{}]*)\}",
                      lambda m: "{" + field_id(m[1]) + "}", spec)
        result += "{" + index + spec + "}"
        p += 1
    return result, values


def collect(text):
    clean = masked(text)
    matches = re.finditer(r"\b(HX_[A-Z]+)\s*\(", clean)
    calls, registrations = [], []
    until = 0
    for match in matches:
        kind = match[1]
        if kind not in MARKERS and kind != "HX_FORMATTER":
            continue
        if match.start() < until:
            raise CodegenError("nested format markers are not supported")
        line_start = clean.rfind("\n", 0, match.start()) + 1
        if clean[line_start:match.start()].lstrip().startswith("#"):
            raise CodegenError("place format markers in C function bodies, "
                               "not macro definitions")
        args, end = arguments(text, clean.index("(", match.start()))
        until = end
        call = {"start": match.start(), "end": end,
                "line": text.count("\n", 0, match.start()) + 1,
                "kind": kind, "output": None, "names": []}
        if kind == "HX_FORMATTER":
            if len(args) != 2 or not all(args):
                raise CodegenError("HX_FORMATTER needs a type and callback")
            call.update(type=args[0], callback=args[1])
            registrations.append(call)
            continue
        if kind in ("HX_FORMAT", "HX_FFORMAT"):
            call["output"] = args.pop(0)
        if not args:
            raise CodegenError("missing format argument")
        call["pattern"] = args.pop(0)
        if kind.startswith("HX_F") and kind != "HX_FORMAT":
            if args:
                raise CodegenError("interpolation takes only its format string")
            pattern, args = interpolate(literal(call["pattern"]))
            call["pattern"] = c_quote(pattern)
        expressions = []
        for arg in args:
            named = re.fullmatch(r"HX_NAMED\s*\((.*)\)", arg, re.S)
            name = None
            if named:
                pieces, finish = arguments(arg, arg.index("("))
                if len(pieces) != 2 or finish != len(arg):
                    raise CodegenError("HX_NAMED needs name and value")
                name, arg = literal(pieces[0]), pieces[1]
            expressions.append(arg)
            call["names"].append(name)
        if len(expressions) > 1024:
            raise CodegenError("at most 1024 format arguments are supported")
        call["expressions"] = expressions
        calls.append(call)
    return calls, registrations


def replace(text, edits):
    for start, end, value in sorted(edits, reverse=True):
        text = text[:start] + value + text[end:]
    return text


def type_name(node):
    while node.get("kind") in ("ImplicitCastExpr", "ParenExpr"):
        child = node.get("inner", [{}])[0]
        if child.get("type", {}).get("qualType", "").startswith("enum "):
            break
        node = child
    info = node.get("type", {})
    raw = info.get("qualType", "")
    return raw, info.get("desugaredQualType", raw)


def normalized(value):
    return re.sub(r"\b(const|volatile|restrict)\b", "", value).strip()


def wrapper(raw, canonical, expression, registered):
    raw, canonical = normalized(raw), normalized(canonical)
    for key in (raw, canonical):
        if key in registered:
            typ, callback = registered[key]
            return (f"hx_fmt_custom(&({typ}[]){{({expression})}}[0], "
                    f"{callback})", "custom")
    if canonical == "hx_format_arg" or raw == "hx_format_arg":
        return f"({expression})", "dynamic"
    if canonical == "hx_string_view" or raw == "hx_string_view":
        return f"hx_fmt_view(({expression}))", "text"
    if canonical == "hx_string" or raw == "hx_string":
        return ("hx_fmt_view(hx_string_view_of(&(hx_string[])"
                f"{{({expression})}}[0]))", "text")
    if re.fullmatch(r"char\s*(\*|\[[^]]*\])", canonical):
        return f"hx_fmt_str(({expression}))", "text"
    if canonical == "_Bool":
        return f"hx_fmt_bool(({expression}))", "bool"
    if canonical == "char":
        return f"hx_fmt_char((unsigned char)({expression}))", "char"
    if canonical in ("float", "double", "long double"):
        func = {"float": "f32", "double": "f64", "long double": "real"}
        return f"hx_fmt_{func[canonical]}(({expression}))", "real"
    if re.fullmatch(r"(unsigned |signed )?(short|int|long|long long|char)"
                    r"( int)?", canonical):
        func = "u64" if canonical.startswith("unsigned") else "i64"
        return f"hx_fmt_{func}(({expression}))", "integer"
    if canonical.endswith("*") and "(" not in canonical:
        return f"hx_fmt_pointer((const void *)({expression}))", "pointer"
    raise CodegenError(f"type {raw!r} needs HX_FORMATTER(type, callback)")


def validate(pattern, kinds, names):
    """Check fixed built-in fields; custom callbacks own their spec language."""
    automatic, mode = 0, None
    by_name = {}
    for i, name in enumerate(names):
        if name is not None:
            if name in by_name:
                raise CodegenError(f"duplicate named argument {name!r}")
            by_name[name] = i

    def resolve(field):
        nonlocal automatic, mode
        if not field:
            if mode == "manual":
                raise CodegenError("mixed automatic and manual indices")
            mode = "automatic"
            index = automatic
            automatic += 1
        elif field.isascii() and field.isdecimal():
            if mode == "automatic":
                raise CodegenError("mixed automatic and manual indices")
            mode = "manual"
            index = int(field)
        else:
            if field not in by_name:
                raise CodegenError(f"unknown named argument {field!r}")
            index = by_name[field]
        if index >= len(kinds):
            raise CodegenError(f"missing argument {index}")
        return kinds[index]

    p = 0
    while p < len(pattern):
        if pattern[p:p + 2] in ("{{", "}}"):
            p += 2
            continue
        if pattern[p] == "}":
            raise CodegenError("unmatched closing brace")
        if pattern[p] != "{":
            p += 1
            continue
        begin, depth = p + 1, 0
        p = begin
        while p < len(pattern):
            if pattern[p] == "{":
                depth += 1
            elif pattern[p] == "}":
                if not depth:
                    break
                depth -= 1
            p += 1
        if p == len(pattern):
            raise CodegenError("unclosed format field")
        field = pattern[begin:p]
        p += 1
        match = re.fullmatch(r"([^!:]*)(![sr])?(?::(.*))?", field, re.S)
        if not match:
            raise CodegenError("invalid conversion or format field")
        kind = resolve(match[1])
        spec = match[3] or ""
        if kind in ("custom", "dynamic"):
            continue
        if match[2]:
            kind = "text"

        def width(m):
            if resolve(m[1]) not in ("integer", "dynamic"):
                raise CodegenError("dynamic width/precision needs an integer")
            return "1"

        spec = re.sub(r"\{([^{}]*)\}", width, spec)
        rule = (r"(?:(.[<>=^])|([<>=^]))?([+ -])?(#)?(0)?"
                r"([0-9]+)?([,_])?(?:\.([0-9]+))?([a-zA-Z%?])?")
        parsed = re.fullmatch(rule, spec, re.S)
        if not parsed:
            raise CodegenError(f"invalid format specification {spec!r}")
        typ = parsed[9] or ""
        valid = {"integer": "dbBoxXc", "real": "eEfFgGaA%",
                 "text": "s?", "bool": "s?dbBoxXc", "char": "cdbBoxX",
                 "pointer": "p"}
        if typ and typ not in valid[kind]:
            raise CodegenError(f"format {typ!r} is invalid for {kind}")
        if kind == "bool" and typ in ("", "s", "?"):
            kind = "text"
        alignment = (parsed[1] or parsed[2] or " ")[-1]
        if kind == "text":
            if alignment == "=" or any(parsed[i] for i in (3, 4, 5, 7)):
                raise CodegenError("numeric flags are invalid for text")
        if kind in ("integer", "char", "pointer", "bool") and parsed[8]:
            raise CodegenError("precision is invalid for this argument type")
        character = typ == "c" or (kind == "char" and not typ)
        if character and any(parsed[i] for i in (3, 4, 5, 7)):
            raise CodegenError("numeric flags are invalid for characters")
        if parsed[7] == "," and typ in ("b", "B", "o", "x", "X"):
            raise CodegenError("comma grouping needs decimal digits")


def config_flags(path):
    sections, current = {}, None
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1]
            sections[current] = []
        elif current and line:
            sections[current].append(line)
    flags = []
    for inc in sections.get("includes", []):
        flags.extend(["-I", inc])
    for define in sections.get("definitions", []):
        flags.append("-D" + define)
    for line in sections.get("flags", []):
        flags.extend(shlex.split(line, posix=os.name != "nt"))
    flags.extend(sections.get("options", []))
    return flags, sections


def run(args):
    source = Path(args.input).resolve()
    output = Path(args.output).resolve()
    if source == output:
        raise CodegenError("generated output must differ from source")
    state_file = output.with_suffix(".state.json")
    roots = [source, Path(args.config).resolve(), Path(__file__).resolve(),
             Path(args.clang).resolve()]

    def signature(paths):
        return {str(p): [p.stat().st_mtime_ns, p.stat().st_size]
                for p in paths}

    if output.exists() and state_file.exists():
        try:
            previous = json.loads(state_file.read_text(encoding="utf-8"))
            paths = [Path(p) for p in previous]
            if all(str(p) in previous for p in roots):
                if signature(paths) == previous:
                    return
        except (OSError, ValueError):
            pass
    text = source.read_text(encoding="utf-8")
    if re.search(r"\bhx_format_generated_\w*", masked(text)):
        raise CodegenError("hx_format_generated_ is reserved by this tool")
    calls, registrations = collect(text)
    flags, settings = config_flags(args.config)
    target = settings.get("target", [])
    if target:
        flags = ["--target=" + target[0]] + flags
    if settings.get("sysroot"):
        flags.append("--sysroot=" + settings["sysroot"][0])
    # MSVC's diagnostics/codegen switches are not Clang driver input.
    cleaned = []
    pending = iter(flags)
    for flag in pending:
        if flag in ("-I", "-isystem", "-iquote", "-include", "-imacros"):
            cleaned.extend([flag, next(pending)])
            continue
        if flag.startswith(("/D", "/I")):
            cleaned.append("-" + flag[1:])
        elif flag.startswith("/FI"):
            cleaned.extend(["-include", flag[3:]])
        elif flag.startswith("/"):
            continue
        elif not flag.startswith(("-W", "-O", "-g", "-fsanitize=")):
            cleaned.append(flag.strip('"'))
    flags = cleaned + ["-std=c99", "-Wno-everything", "-iquote",
                       str(source.parent), "-x", "c"]
    if target and "windows-msvc" in target[0]:
        flags += ["-fms-extensions", "-fms-compatibility"]
        runtime = (settings.get("msvc_runtime") or ["MultiThreaded"])[0]
        if runtime:
            flags.append("-D_MT=1")
            if "DLL" in runtime:
                flags.append("-D_DLL=1")
            if "Debug" in runtime:
                flags.append("-D_DEBUG=1")
    edits = []
    for i, call in enumerate(calls):
        call["tag"] = i
        values = ([call["output"]] if call["output"] else [])
        values += call["expressions"]
        expr = f"hx_os_format_probe({i}, {call['pattern']}"
        expr += "".join(", (" + value + ")" for value in values) + ")"
        edits.append((call["start"], call["end"], expr))
    for i, reg in enumerate(registrations):
        reg["alias"] = f"hx_os_fmt_registered_{i}"
        edits.append((reg["start"], reg["end"],
                      f"typedef {reg['type']} {reg['alias']}"))
    prefix = ('#include <hx_format.h>\n'
              'extern hx_os_status hx_os_format_probe(unsigned, '
              'const char *, ...);\n'
              f'#line 1 {c_quote(str(source).replace(chr(92), "/"))}\n')
    analysis = prefix + replace(text, edits)
    output.parent.mkdir(parents=True, exist_ok=True)
    probe = output.with_suffix(".probe.c")
    ast_file = output.with_suffix(".ast.json")
    depfile = output.with_suffix(".d")
    probe.write_text(analysis, encoding="utf-8", newline="\n")
    command = [args.clang] + flags + ["-Xclang", "-ast-dump=json",
               "-fsyntax-only", "-MD", "-MF", str(depfile),
               "-MT", str(output), str(probe)]
    with ast_file.open("wb") as stream:
        proc = subprocess.run(command, stdout=stream, stderr=subprocess.PIPE,
                              timeout=120, check=False)
    if proc.returncode:
        raise CodegenError(proc.stderr.decode("utf-8", errors="replace"))
    if ast_file.stat().st_size > 128 * 1024 * 1024:
        raise CodegenError("translation-unit AST exceeds 128 MiB")
    ast = json.loads(ast_file.read_text(encoding="utf-8"))
    registered = {normalized(r["type"]): (r["type"], r["callback"])
                  for r in registrations}

    def original_offset(offset):
        position = len(analysis.encode("utf-8")[:offset].decode("utf-8"))
        position -= len(prefix)
        delta = 0
        for start, end, replacement in sorted(edits):
            new_start = start + delta
            if position < new_start:
                break
            if position < new_start + len(replacement):
                raise CodegenError("format marker inside a macro/initializer")
            delta += len(replacement) - (end - start)
        return position - delta

    found = {}

    def visit(node, scope=None):
        if (node.get("kind") == "CompoundStmt" and
                "offset" in node.get("range", {}).get("begin", {})):
            scope = node
        if node.get("kind") == "TypedefDecl":
            for reg in registrations:
                if node.get("name") == reg["alias"]:
                    raw, typ = type_name(node)
                    registered[normalized(typ)] = (reg["type"], reg["callback"])
        if node.get("kind") == "CallExpr":
            inner = node.get("inner", [])
            ref = inner[0] if inner else {}
            while ref.get("inner"):
                ref = ref["inner"][0]
            name = ref.get("referencedDecl", {}).get("name")
            if name == "hx_os_format_probe":
                tag = inner[1]
                while tag.get("inner"):
                    tag = tag["inner"][0]
                if not scope:
                    raise CodegenError("format markers need a function body")
                found[int(tag["value"])] = (inner[2:], scope)
        for child in node.get("inner", []):
            visit(child, scope)

    visit(ast)
    declarations = {}
    generated = []
    for call in calls:
        if call["tag"] not in found:
            # Leave inactive #if branches for their own configuration.
            continue
        nodes, scope = found[call["tag"]]
        format_node = nodes[0]
        nodes = nodes[1 + bool(call["output"]):]
        tag = call["tag"]
        stem = f"hx_format_generated_{tag}"
        slots = f"{stem}_args"
        decl = f"\nhx_format_arg {slots}[{max(1, len(nodes))}];\n"
        decl += f"const char *{stem}_pattern;\n"
        expressions = []
        if call["output"]:
            decl += f"hx_string *{stem}_out;\n"
            expressions.append(f"{stem}_out = ({call['output']})")
        expressions.append(f"{stem}_pattern = ({call['pattern']})")
        kinds = []
        for i, (node, expr) in enumerate(zip(nodes, call["expressions"])):
            raw, canonical = type_name(node)
            wrapped, kind = wrapper(raw, canonical, expr, registered)
            if call["names"][i] is not None:
                name = c_quote(call['names'][i])
                wrapped = f"hx_fmt_named({name}, {wrapped})"
            expressions.append(f"{slots}[{i}] = {wrapped}")
            kinds.append(kind)
        while format_node.get("kind") in ("ImplicitCastExpr", "ParenExpr"):
            format_node = format_node["inner"][0]
        if format_node.get("kind") == "StringLiteral":
            try:
                validate(literal(format_node["value"]), kinds, call["names"])
            except CodegenError as exc:
                raise CodegenError(f"{source}:{call['line']}: {exc}") from exc
        view = f"hx_string_view_from_cstr({stem}_pattern)"
        if call["output"]:
            last = f"hx_format({stem}_out, {view}, {slots}, {len(nodes)}, NULL)"
        else:
            dest = ("HX_PRINT_STDERR" if call["kind"] == "HX_EPRINTLN"
                    else "HX_PRINT_STDOUT")
            newline = int(call["kind"].endswith("LN"))
            last = (f"hx_print_args({dest}, {newline}, {view}, {slots}, "
                    f"{len(nodes)}, NULL)")
        expressions.append(last)
        generated.append((call["start"], call["end"],
                          "(" + ", ".join(expressions) + ")"))
        location = scope["range"]["begin"]
        if "offset" not in location:
            raise CodegenError("format markers expanded by macros unsupported")
        offset = original_offset(location["offset"]) + 1
        declarations[offset] = declarations.get(offset, "") + decl
    for offset, declaration in declarations.items():
        line = text.count("\n", 0, offset) + 1
        path = c_quote(source.as_posix())
        declaration += f'#line {line} {path}\n'
        generated.append((offset, offset, declaration))
    result = ("/* Generated by hx_format_codegen; edit original source. */\n"
              '#include <hx_format.h>\n'
              f'#line 1 {c_quote(str(source).replace(chr(92), "/"))}\n'
              + replace(text, generated))
    temporary = output.with_suffix(".new.c")
    temporary.write_text(result, encoding="utf-8", newline="\n")
    temporary.replace(output)
    dependencies = depfile.read_text(encoding="utf-8").replace("\\\n", " ")
    separator = dependencies.find(": ")
    if separator < 0:
        raise CodegenError("Clang did not emit a usable dependency file")
    paths = set(roots)
    for part in re.findall(r"(?:\\.|[^\s])+", dependencies[separator + 2:]):
        name = re.sub(r"\\([ #:\\])", r"\1", part).replace("$$", "$")
        path = Path(name).resolve()
        if path != probe:
            paths.add(path)
    state_file.write_text(json.dumps(signature(paths), sort_keys=True),
                          encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--configuration", default="default")
    parser.add_argument("--active", default="")
    args = parser.parse_args()
    if (args.active or "default") != args.configuration:
        return 0
    try:
        run(args)
    except (CodegenError, OSError, ValueError,
            subprocess.TimeoutExpired) as exc:
        print(f"hx-format: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
