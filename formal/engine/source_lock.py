"""Byte fingerprints and lexical locating checks for a conservative C++ subset.

Directives may continue; backslash, optional horizontal whitespace and newline
are splices. Splices elsewhere are rejected, even in comments or literals.
Declaration/class-head template angles must balance;
ambiguous angle operators need parentheses. Function bodies exclude nested
classes and plain lambdas: capture, optional parameters, optional mutable,
constexpr/consteval, noexcept (with optional operand), trailing return, body.
Lambda attributes, template heads, constraints and other specifier positions,
coroutine keywords and body-level requires-expressions are unsupported.
Scope decisions use allowlists: a definition tail accepts only cv/ref qualifiers,
noexcept with an optional balanced operand, override/final, a balanced trailing
return, a supported requires-clause and validated attributes, then body, ctor
initializers or try. Other tokens never establish a definition. Constraints are
unparenthesized qualified concept names (optional template arguments), or
requires-expressions, joined by &&/||; declaration-level requires-expressions
are supported. Parenthesized clauses are rejected throughout the source.
Attributes accept one identifier, optional ::identifier, and optional arguments
of identifiers/literals/commas/balanced parentheses only. Digraphs are rejected
in code (including directives), honoring the standard <:: lexical exception.
Unsupported constructs fail closed with their physical line.
Class heads accept class/struct/union, validated attributes, one plain ASCII
identifier, optional final and an allowlisted base list before the body. Other
body-introducing heads fail closed. Include header-names are opaque tokens.
References omit namespaces and require the exact complete class path, whether
explicitly qualified or implied by nesting. Any enclosing function/block body
excludes a candidate, including local classes. Exact class paths in different
namespaces are ambiguous regardless of the fragment and report candidate lines.
Declarator names are complete: constructors, destructors and operator-function-ids
are distinct. Declaration prefixes accept type/specifier words, qualified/template
types, attributes and balanced decltype/explicit/alignas operands; initializer
lists and expression prefixes never establish a definition. Specifier scans skip
balanced operands. Inline friends are nonmembers and are excluded from member
references. Ordinary free functions use ::f with the same namespace ambiguity
rule; this subset has no friend-reference syntax. Defaulted/deleted
functions and nested lambdas have no independently referenceable body.
Extend this subset deliberately. SHA-256 covers source bytes with only CRLF
converted to LF; every edit, including comments, requires lock qualification.
"""

import hashlib
import json
import re
import platform
import time
from functools import lru_cache

VERSION = 4
SOURCE_DIGEST_MODE = "sha256-crlf-to-lf-bytes-v4"
IDENTIFIER = r"[A-Za-z_][A-Za-z_0-9]*"
# Keywords are not identifier tokens. Only the listed type keywords may appear
# in a trailing type; this also preserves the plain-lambda specifier boundary.
KEYWORDS = frozenset("""
    alignas alignof and and_eq asm auto bitand bitor bool break case catch char
    char8_t char16_t char32_t class compl concept const consteval constexpr
    constinit const_cast continue co_await co_return co_yield decltype default
    delete do double dynamic_cast else enum explicit export extern false float
    for friend goto if inline int long mutable namespace new noexcept not not_eq
    nullptr operator or or_eq private protected public register reinterpret_cast
    requires return short signed sizeof static static_assert static_cast struct
    switch template this thread_local throw true try typedef typeid typename union
    unsigned using virtual void volatile wchar_t while xor xor_eq
""".split())
TYPE_KEYWORDS = frozenset("""
    auto bool char char8_t char16_t char32_t const decltype double float int long
    short signed template typename unsigned void volatile wchar_t
""".split())

RAW = r'(?:u8|u|U|L)?R"(?P<delim>[^\s()\\]{0,16})\(.*?\)(?P=delim)"'
PP_NUMBER = r"(?:[0-9]|\.[0-9])(?:[eEpP][+-]|'[A-Za-z_0-9]|[A-Za-z_0-9.])*"
# Raw-range discovery precedes splicing, so pp-numbers may span continuations.
SPLICE_UNIT = r"\\[ \t]*\n"
SPLICE = r"(?:" + SPLICE_UNIT + r")*"
CONTINUED_NUMBER = (
    r"(?:[0-9]|\." + SPLICE + r"[0-9])" + SPLICE
    + r"(?:(?:[eEpP]" + SPLICE + r"[+-]|'" + SPLICE + r"[A-Za-z_0-9]|[A-Za-z_0-9.])" + SPLICE + r")*"
)
LITERAL = r'''(?:u8|u|U|L)?(?:"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')'''
HEADER_NAME = r'^[ \t]*\#[ \t]*include[ \t]*(?P<header><[^>\n]+>|"[^"\n]+")'
TOKEN = re.compile(
    HEADER_NAME + "|" + CONTINUED_NUMBER + "|" + RAW + "|" + LITERAL + "|" + IDENTIFIER
    + r"|//(?:" + SPLICE_UNIT + r"|[^\n])*|/\*.*?\*/", re.S | re.M
)
CPP_TOKEN = re.compile(
    r"[A-Za-z_][A-Za-z_0-9]*|" + PP_NUMBER
    + r"|%:%:|<%|%>|<:(?!:(?:[^:>]|$))|:>|%:|<=>|<<=|>>=|->\*|\.\.\.|::|->|\.\*|&&|\|\||==|!=|<=|>=|<<|>>"
    r"|\+\+|--|[+*/%&|^!<>=-]=|##|[^\s]"
)


def spliced(text):
    """Perform line splicing outside raw literals, retaining physical offsets."""
    text = text.replace("\r\n", "\n")
    # Check physical lines before lexing: a split raw prefix cannot hide a splice.
    def mask(match):
        if re.fullmatch(CONTINUED_NUMBER + "|" + IDENTIFIER, match[0]):
            return match[0]
        if match.group("header") is not None:
            prefix = match.start("header") - match.start()
            return match[0][:prefix] + re.sub(r"[^\n]", " ", match["header"])
        return re.sub(r"[^\n]", " ", match[0])

    masked_lines = TOKEN.sub(mask, text).splitlines(keepends=True)
    directive = False
    for line_number, (line, masked) in enumerate(zip(text.splitlines(keepends=True), masked_lines), 1):
        directive = directive or masked.lstrip().startswith("#")
        continued = re.search(SPLICE_UNIT + r"$", line) is not None
        if continued and not directive:
            raise ValueError("unsupported line continuation outside preprocessing directive at line %d" % line_number)
        directive = directive and continued
    raw_ranges = []
    for match in TOKEN.finditer(text):
        if re.fullmatch(RAW, match[0], re.S):
            raw_ranges.append((match.start(), match.end()))
    chars, offsets = [], []
    i, raw_index = 0, 0
    while i < len(text):
        if raw_index < len(raw_ranges) and i == raw_ranges[raw_index][0]:
            end = raw_ranges[raw_index][1]
            chars.append(text[i:end])
            offsets.extend(range(i, end))
            i = end
            raw_index += 1
        elif text[i] == "\\" and (splice := re.match(SPLICE_UNIT, text[i:])):
            i += splice.end()
        else:
            chars.append(text[i])
            offsets.append(i)
            i += 1
    return "".join(chars), offsets


def normalize(data):
    """Normalize source bytes for hashing: CRLF to LF, with no other changes."""
    return data.replace(b"\r\n", b"\n")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def tokens(text):
    """Return lexical tokens and physical offsets, masking literals and directives."""
    value, offsets = spliced(text)
    hidden = list(value)
    literals = {}
    for m in TOKEN.finditer(value):
        if re.fullmatch(PP_NUMBER + "|" + IDENTIFIER, m[0]):
            continue
        header = m.group("header") is not None
        if not header and not m[0].startswith(("//", "/*")):
            literals[m.start()] = m.end()
        start = m.start("header") if header else m.start()
        for i in range(start, m.end()):
            if hidden[i] != "\n":
                hidden[i] = " "
    masked = "".join(hidden)
    # Inspect directives too, but never literal contents or comments.
    for m in CPP_TOKEN.finditer(masked):
        if m[0] in ("<%", "%>", "<:", ":>", "%:", "%:%:"):
            line = text.replace("\r\n", "\n")[:offsets[m.start()]].count("\n") + 1
            raise ValueError("unsupported digraph %s at line %d" % (m[0], line))
    for m in re.finditer(r"^[ \t]*#[^\n]*", masked, re.M):
        literals = {i: end for i, end in literals.items() if not m.start() <= i < m.end()}
        for i in range(m.start(), m.end()):
            hidden[i] = " "
    matches = [(m[0], offsets[m.start()]) for m in CPP_TOKEN.finditer("".join(hidden))]
    # Opaque literal tokens prevent matching through or within literal contents.
    matches.extend(("\x00literal", offsets[i]) for i in literals)
    return sorted(matches, key=lambda item: item[1])


def structure(values):
    pairs, parents, stack = {}, {}, []
    for i, value in enumerate(values):
        parents[i] = next((j for j in reversed(stack) if values[j] == "{"), None)
        if value in ("{", "(", "["):
            stack.append(i)
        elif value in ("}", ")", "]"):
            if not stack or values[stack[-1]] != {"}": "{", ")": "(", "]": "["}[value]:
                raise Unsupported("unbalanced C++ delimiters", i)
            start = stack.pop()
            pairs[start] = i
    if stack:
        raise Unsupported("unbalanced C++ delimiters", stack[-1])
    return pairs, parents


class Unsupported(ValueError):
    def __init__(self, construct, index):
        self.construct, self.index = construct, index
        super().__init__(construct)


def template_end(values, pairs, start):
    """Skip template arguments, including braced operands; reject uncertain angles."""
    depth, j = 1, start + 1
    while j < len(values) and depth:
        value = values[j]
        if value == "operator":
            j += 2  # An operator name does not open or close template arguments.
            continue
        if value in ("(", "[", "{"):
            j = pairs[j] + 1
            continue
        punctuation = ("<", ">", ">>", "::", ",", "...", "*", "&", "&&", "||", "+", "-", "/", "%", "|",
                       "^", "!", "~", "?", ":", "=", ".", "->")
        allowed = value in punctuation + ("\x00literal",) or re.fullmatch(IDENTIFIER + "|" + PP_NUMBER, value)
        if not allowed:
            raise Unsupported("ambiguous template arguments", j)
        depth += {"<": 1, ">": -1, ">>": -2}.get(value, 0)
        if depth < 0:
            raise Unsupported("ambiguous template arguments", j)
        j += 1
    if depth:
        raise Unsupported("unbalanced template arguments", start)
    return j


def class_bodies(values, pairs):
    """Accept only plain ASCII class heads; reject unhandled body introducers."""
    bodies = {}

    def unhandled(start, index):
        # A class keyword may instead be a template parameter, elaborated type
        # or forward declaration. Stop at its enclosing declaration boundary;
        # never silently leave an unsupported class body searchable.
        j = index
        while j < len(values) and values[j] not in (";", ",", ">", ">>", ")"):
            if values[j] == "{":
                raise Unsupported("class head (expected plain ASCII identifier and allowlisted suffix)", start)
            if values[j] == "<":
                j = template_end(values, pairs, j)
            elif values[j] in ("(", "["):
                j = pairs[j] + 1
            else:
                j += 1

    for i, value in enumerate(values[:-1]):
        if value not in ("class", "struct", "union") or (i and values[i - 1] == "enum"):
            continue
        j = i + 1
        while values[j:j + 2] == ["[", "["]:
            j = attribute_end(values, pairs, j)
        if j == len(values) or not re.fullmatch(IDENTIFIER, values[j]) or values[j] in KEYWORDS:
            unhandled(i, j)
            continue
        name = values[j]
        j += 1
        if values[j:j + 1] == ["final"]:
            j += 1
        if values[j:j + 1] == [":"]:
            j += 1
            while j < len(values):
                while values[j:j + 1] in (["public"], ["protected"], ["private"], ["virtual"]):
                    j += 1
                j = qualified_name_end(values, pairs, j)
                if values[j:j + 1] == ["..."]:
                    j += 1
                if values[j:j + 1] != [","]:
                    break
                j += 1
            if values[j:j + 1] != ["{"]:
                raise Unsupported("class head base list", i)
        if values[j:j + 1] == ["{"]:
            bodies[j] = (name, pairs[j])
        else:
            unhandled(i, j)
    return bodies


def lambda_body(values, pairs, start):
    """Recognize only the documented plain lambda suffix; reject other forms."""
    previous = values[start - 1] if start else ""
    operand = previous in (")", "]", "\x00literal") or re.fullmatch(IDENTIFIER + "|" + PP_NUMBER, previous)
    operand = operand and previous not in ("return", "throw", "case", "else", "do", "sizeof")
    j = pairs[start] + 1
    if j < len(values) and values[j] == "(":
        j = pairs[j] + 1
    # Subscripts/calls end before punctuation such as ';' or a condition's ')'.
    # A lambda suffix may instead follow a control condition's ')' or sizeof;
    # preceding-token heuristics alone must not leave its body searchable.
    suffixes = ("{", "mutable", "constexpr", "consteval", "noexcept", "->", "<", "static", "requires")
    attribute = j + 1 < len(values) and values[j:j + 2] == ["[", "["]
    if operand and j < len(values) and values[j] not in suffixes and not attribute:
        return None
    if j < len(values) and values[j] == "mutable":
        j += 1
    if j < len(values) and values[j] in ("constexpr", "consteval"):
        j += 1
    if j < len(values) and values[j] == "noexcept":
        j += 1
        if j < len(values) and values[j] == "(":
            j = pairs[j] + 1
    if j < len(values) and values[j] == "->":
        j = trailing_return_end(values, pairs, j + 1, attributes=False)
    if j < len(values) and values[j] == "{":
        return j
    raise Unsupported("unsupported lambda form", start)


def attribute_end(values, pairs, start):
    """Accept exactly [[name[::name][(identifier/literal/comma/paren...)]]]."""
    end = pairs[start]
    j = start + 2
    if j >= end or not re.fullmatch(IDENTIFIER, values[j]):
        raise Unsupported("attribute name", start)
    j += 1
    if j < end and values[j] == "::":
        j += 1
        if j >= end or not re.fullmatch(IDENTIFIER, values[j]):
            raise Unsupported("attribute namespace", j)
        j += 1
    if j < end and values[j] == "(":
        finish = pairs[j]
        for k in range(j + 1, finish):
            value = values[k]
            allowed = value in ("(", ")", ",", "\x00literal") or re.fullmatch(IDENTIFIER + "|" + PP_NUMBER, value)
            if not allowed or value in ("co_await", "co_yield", "co_return"):
                raise Unsupported("attribute argument", k)
        j = finish + 1
    if j != end - 1 or values[j] != "]":
        raise Unsupported("attribute specifier", start)
    return end + 1


def qualified_name_end(values, pairs, start):
    """Allow an identifier path with optional template arguments on each part."""
    j = start + (values[start:start + 1] == ["::"])
    while j < len(values) and re.fullmatch(IDENTIFIER, values[j]):
        j += 1
        if j < len(values) and values[j] == "<":
            j = template_end(values, pairs, j)
        if values[j:j + 1] != ["::"]:
            return j
        j += 1
    raise Unsupported("qualified name", start)


def requires_end(values, pairs, start):
    """Allow concept names or requires-expressions joined by && or ||."""
    j = start + 1
    while j < len(values):
        if values[j] == "(":
            raise Unsupported("ambiguous trailing requires parentheses", j)
        if values[j] == "requires":
            j += 1
            if values[j:j + 1] == ["("]:
                j = pairs[j] + 1
            if values[j:j + 1] != ["{"]:
                raise Unsupported("requires-expression", j - 1)
            j = pairs[j] + 1
        else:
            j = qualified_name_end(values, pairs, j)
        if values[j:j + 1] not in (["&&"], ["||"]):
            return j
        j += 1
    raise Unsupported("requires-clause", start)


def trailing_return_end(values, pairs, start, attributes=True):
    """Allow type names, pointer/ref punctuation and balanced type operands."""
    j = start
    while j < len(values):
        value = values[j]
        if value in ("requires", "override", "final", "try"):
            break
        type_word = value in TYPE_KEYWORDS or (re.fullmatch(IDENTIFIER, value) and value not in KEYWORDS)
        if type_word or value in ("::", "*", "&", "&&", "..."):
            j += 1
        elif value == "<":
            j = template_end(values, pairs, j)
        elif value == "(":
            # Type operands may contain calls/operators, but no body or initializer.
            punctuation = ("(", ")", "::", "->", ".", ".*", "->*", ",", "...", "*", "&", "&&", "||",
                           "+", "-", "/", "%", "|", "^", "!", "~", "<", ">", "<<", ">>", "<=", ">=",
                           "==", "!=", "?", ":", "++", "--")
            for k in range(j + 1, pairs[j]):
                token = values[k]
                allowed = token in punctuation + ("\x00literal",) or re.fullmatch(IDENTIFIER + "|" + PP_NUMBER, token)
                if not allowed:
                    raise Unsupported("trailing return operand", k)
            j = pairs[j] + 1
        elif attributes and values[j:j + 2] == ["[", "["]:
            j = attribute_end(values, pairs, j)
        else:
            break
    if j == start:
        raise Unsupported("trailing return type", start)
    return j


def initializer_body(values, pairs, start):
    """Accept only named, balanced mem-initializers separated by commas."""
    j = start
    while j < len(values):
        j = qualified_name_end(values, pairs, j)
        if values[j:j + 1] not in (["("], ["{"]):
            return None
        j = pairs[j] + 1
        if values[j:j + 1] == ["..."]:
            j += 1
        if values[j:j + 1] == ["{"]:
            return j
        if values[j:j + 1] != [","]:
            return None
        j += 1
    return None


def function_body(values, pairs, after, constructor):
    """Only allowlisted declarator tails can establish a definition."""
    j = after
    while j < len(values):
        value = values[j]
        if value in ("const", "volatile", "&", "&&", "override", "final"):
            j += 1
        elif value == "noexcept":
            j += 1
            if values[j:j + 1] == ["("]:
                j = pairs[j] + 1
        elif values[j:j + 2] == ["[", "["]:
            j = attribute_end(values, pairs, j)
        elif value == "->":
            j = trailing_return_end(values, pairs, j + 1)
        elif value == "requires":
            j = requires_end(values, pairs, j)
        elif value == "try":
            j += 1
            if values[j:j + 1] == [":"] and constructor:
                return initializer_body(values, pairs, j + 1)
            return j if values[j:j + 1] == ["{"] else None
        elif value == ":" and constructor:
            return initializer_body(values, pairs, j + 1)
        elif value == "{":
            return j
        else:
            return None
    return None


def namespace_bodies(values):
    """Map namespace openers to their complete head (aliases have no body)."""
    bodies = {}
    for i, value in enumerate(values):
        if value != "namespace":
            continue
        j, names = i + 1, []
        while j < len(values) and values[j] not in ("{", ";", "="):
            if not re.fullmatch(IDENTIFIER, values[j]) or values[j] in KEYWORDS:
                raise Unsupported("namespace head", j)
            names.append(values[j])
            j += 1
            if values[j:j + 1] != ["::"]:
                break
            j += 1
        if values[j:j + 1] == ["{"]:
            bodies[j] = tuple(names) or ("(anonymous)",)
        elif values[j:j + 1] not in (["="], [";"]):
            raise Unsupported("namespace head", min(j, len(values) - 1))
    return bodies


def enclosing_paths(values, pairs, parents, classes, namespaces):
    """Classify every brace ancestor; unrecognized braces are body scopes."""
    paths = {None: ()}
    for start in sorted(pairs):
        if values[start] != "{":
            continue
        if start in namespaces:
            tail = tuple(("namespace", name) for name in namespaces[start])
        elif start in classes:
            tail = (("class", classes[start][0]),)
        else:
            tail = (("body", str(start)),)
        paths[start] = paths[parents[start]] + tail
    return paths


def definition_scope(values, start, path, namespace_paths, class_paths):
    """Resolve the entire declarator qualifier, never a suffix of it."""
    namespaces = tuple(name for kind, name in path if kind == "namespace")
    classes = tuple(name for kind, name in path if kind == "class")
    qualifiers, j = [], start - 1
    while j >= 0 and values[j] == "::":
        if j == 0 or values[j - 1] in KEYWORDS:
            break  # Leading global qualifier, possibly after a return type.
        if not re.fullmatch(IDENTIFIER, values[j - 1]):
            raise Unsupported("qualified definition scope", j)
        qualifiers.insert(0, values[j - 1])
        j -= 2
    absolute = j >= 0 and values[j] == "::"
    if j >= 0 and values[j] in (".", "->"):
        return None
    if absolute:
        namespaces, classes = (), ()
    qualifiers = tuple(qualifiers)
    if classes or not qualifiers:
        return namespaces, classes + qualifiers
    # Explicit namespace prefixes may appear in out-of-class definitions.
    # Resolve from the nearest namespace, respecting a class of the same name.
    for depth in range(len(namespaces), -1, -1):
        base = namespaces[:depth]
        if (base, qualifiers[:1]) in class_paths:
            return base, qualifiers
        for length in range(len(qualifiers), 0, -1):
            if base + qualifiers[:length] in namespace_paths:
                return base + qualifiers[:length], qualifiers[length:]
    return namespaces, qualifiers


@lru_cache(maxsize=256)
def analyze(text):
    """Validate the entire source subset, including definitions without references."""
    stream = tokens(text)
    values = [v for v, _ in stream]
    try:
        pairs, parents = structure(values)
        for i, value in enumerate(values[:-1]):
            if values[i:i + 2] == ["[", "["]:
                attribute_end(values, pairs, i)
        classes = class_bodies(values, pairs)
        namespaces = namespace_bodies(values)
        paths = enclosing_paths(values, pairs, parents, classes, namespaces)
        declarations = set(classes) | set(namespaces) | {None}
        requirements = set()
        for i, value in enumerate(values):
            if value in ("co_await", "co_yield", "co_return"):
                raise Unsupported("coroutine keyword " + value, i)
            if value == "requires":
                if parents[i] not in declarations | requirements:
                    form = "requires-expression" if values[i + 1] in ("{", "(") else "requires-clause"
                    raise Unsupported(form + " in function body", i)
                j = i + 1
                # Expression positions are explicitly listed. Qualifiers and
                # attributes cannot turn a trailing clause into an expression.
                previous = values[i - 1] if i else ""
                expression = previous in ("=", "(", "requires")
                if previous in ("&&", "||") and i >= 2:
                    operand = values[i - 2]
                    expression = operand in (">", ">>", "}") or (
                        re.fullmatch(IDENTIFIER, operand) and operand not in ("const", "volatile", "noexcept")
                    )
                if values[j:j + 1] == ["("]:
                    if not expression:
                        raise Unsupported("ambiguous trailing requires parentheses", i)
                    j = pairs[j] + 1
                    if values[j:j + 1] != ["{"]:
                        raise Unsupported("requires-expression", i)
                if values[j:j + 1] == ["{"]:
                    requirements.add(j)
                else:
                    end = requires_end(values, pairs, i)
                    following = values[end] if end < len(values) else ""
                    if following not in ("{", ";", ":", "try", "::", "[") and not re.fullmatch(IDENTIFIER, following):
                        raise Unsupported("requires-clause tail", min(end, len(values) - 1))
        expressions = set()
        for start, finish in pairs.items():
            if values[start] in ("(", "["):
                expressions.update(range(start + 1, finish))
        # At declaration level every '<' must be a balanced template opener.
        # Parenthesized comparisons/shifts are opaque; operator names and ->
        # are distinct tokens. Mark the whole range, including candidate names.
        j = 0
        while j < len(values):
            if j in expressions or parents[j] not in declarations:
                j += 1
                continue
            if values[j] == "operator":
                j += 2
            elif values[j] == "<":
                end = template_end(values, pairs, j)
                expressions.update(range(j, end))
                j = end
            elif values[j] in (">", ">>", "<=", ">=", "<<", "<<=", ">>=", "<=>"):
                raise Unsupported("ambiguous declaration angles", j)
            else:
                j += 1
        lambdas = {}
        i = 0
        while i < len(values):
            value = values[i]
            if value == "[" and values[i + 1] == "[":
                i = pairs[i] + 1  # Declaration/statement attributes, not captures.
                continue
            if value == "[":
                body = lambda_body(values, pairs, i)
                if body is not None:
                    lambdas[body] = pairs[body]
            i += 1
        return stream, values, pairs, parents, classes, namespaces, expressions, lambdas, paths
    except Unsupported as exc:
        line = text.replace("\r\n", "\n")[:stream[exc.index][1]].count("\n") + 1
        raise ValueError("unsupported %s at line %d" % (exc.construct, line)) from None


def declarator_name(values, pairs, opening):
    """Return the complete name before a parameter list, including operator ids."""
    end = opening
    # Search only this declaration head; nested operands have already been
    # excluded by analyze(). A conversion type may itself contain :: tokens.
    boundary = opening - 1
    reverse = {finish: start for start, finish in pairs.items()}
    while boundary >= 0 and values[boundary] not in (";", "{", "}", ":"):
        if values[boundary] in (")", "]"):
            boundary = reverse[boundary] - 1
            continue
        if values[boundary] == "=" and values[boundary - 1:boundary] != ["operator"]:
            break
        if values[boundary] == "operator":
            name = values[boundary + 1:end]
            punctuation = ("+", "-", "*", "/", "%", "^", "&", "|", "~", "!", "=", "<", ">",
                           "+=", "-=", "*=", "/=", "%=", "^=", "&=", "|=", "<<", ">>", "<<=", ">>=",
                           "==", "!=", "<=", ">=", "<=>", "&&", "||", "++", "--", ",", "->*", "->")
            if (name in (["(", ")"], ["[", "]"], ["new"], ["delete"],
                         ["new", "[", "]"], ["delete", "[", "]"])
                    or len(name) == 1 and name[0] in punctuation):
                return boundary, values[boundary:end]
            # Conversion-function-ids use the allowlisted type grammar. No
            # suffix of a conversion type is an ordinary declarator name.
            if name and trailing_return_end(values[:end], pairs, boundary + 1, attributes=False) == end:
                return boundary, values[boundary:end]
            return None
        boundary -= 1
    start = end - 1
    if start < 0 or not re.fullmatch(IDENTIFIER, values[start]) or values[start] in KEYWORDS:
        return None
    if start and values[start - 1] == "~":
        start -= 1
    return start, values[start:end]


def reference_name(function):
    """Split class qualification from a complete (possibly conversion) name."""
    names = [v for v, _ in tokens(function)]
    if "operator" in names:
        start = names.index("operator")
    else:
        start = len(names) - 1
        if start and names[start - 1] == "~":
            start -= 1
    scope = names[:start]
    if not scope or scope[-1] != "::":
        raise ValueError("references require a class path followed by :: and a complete declarator name")
    scope = scope[:-1]
    if any(not re.fullmatch(IDENTIFIER, v) for v in scope[::2]) or any(v != "::" for v in scope[1::2]):
        raise ValueError("invalid reference class path")
    return tuple(scope[::2]), names[start:]


def declaration_start(values, reverse, expressions, start):
    """Scan specifiers backwards, skipping complete balanced operands."""
    j = start - 1
    while j >= 0:
        if values[j] in (")", "]") or values[j] == "}" and j in expressions:
            j = reverse[j] - 1
        elif values[j] in (";", "{", "}", ":"):
            break
        else:
            j -= 1
    return j + 1


def declaration_prefix(values, pairs, begin, start, typeless):
    """Allow declaration specifiers/types, never calls or mem-initializers."""
    # A colon starts an initializer list unless it is a class access label.
    if begin and values[begin - 1] == ":" and values[begin - 2:begin - 1] not in (
            ["public"], ["protected"], ["private"]):
        return False
    # Remove the explicit namespace/class qualifier from the declarator head.
    end = start
    while end and values[end - 1] == "::":
        end -= 1
        if end and re.fullmatch(IDENTIFIER, values[end - 1]) and values[end - 1] not in KEYWORDS:
            end -= 1
        else:
            break
    if end == begin:
        return typeless
    j = begin
    specifiers = ("inline", "static", "extern", "virtual", "friend", "constexpr", "consteval", "explicit")
    while j < end:
        value = values[j]
        if value == "friend":
            return False  # Friends are nonmembers; this subset has no reference syntax for them.
        if value == "template" and values[j + 1:j + 2] == ["<"]:
            j = template_end(values, pairs, j + 1)
        elif value == "requires":
            j = requires_end(values, pairs, j)
        elif values[j:j + 2] == ["[", "["]:
            j = attribute_end(values, pairs, j)
        elif value == "(" and j > begin and values[j - 1] in ("decltype", "explicit", "alignas"):
            j = pairs[j] + 1
        elif value == "<":
            j = template_end(values, pairs, j)
        elif (value in TYPE_KEYWORDS or value in specifiers + ("alignas", "::", "*", "&", "&&", "...")
              or re.fullmatch(IDENTIFIER, value) and value not in KEYWORDS):
            j += 1
        else:
            return False
    return j == end


def definition_candidates(text, function):
    """Locate body openers for exactly this reference, before fragment selection."""
    stream, values, pairs, parents, classes, namespaces, expression_tokens, lambdas, paths = analyze(text)
    scope_names, name_tokens = reference_name(function)
    namespace_paths = set()
    for start in namespaces:
        path = paths[start]
        if all(kind == "namespace" for kind, _ in path):
            names = tuple(name for _, name in path)
            namespace_paths.update(names[:length] for length in range(1, len(names) + 1))
    class_paths = {(tuple(name for kind, name in path if kind == "namespace"),
                    tuple(name for kind, name in path if kind == "class"))
                   for start, path in paths.items() if start in classes and not any(k == "body" for k, _ in path)}
    bodies = {}
    reverse = {finish: start for start, finish in pairs.items()}
    for end, value in enumerate(values):
        if value != "(" or end in expression_tokens:
            continue
        path = paths[parents[end]]
        if any(kind == "body" for kind, _ in path):
            continue
        try:
            declarator = declarator_name(values, pairs, end)
            if declarator is None:
                continue
            i, complete_name = declarator
            if i in expression_tokens or complete_name != name_tokens:
                continue
            candidate = definition_scope(values, i, path, namespace_paths, class_paths)
            if candidate is None or candidate[1] != scope_names:
                continue
            constructor = bool(scope_names) and name_tokens == [scope_names[-1]]
            begin = declaration_start(values, reverse, expression_tokens, i)
            typeless = constructor or name_tokens[0] in ("~", "operator")
            if not declaration_prefix(values, pairs, begin, i, typeless):
                continue
            body = function_body(values, pairs, pairs[end] + 1, constructor)
        except Unsupported as exc:
            line = text.replace("\r\n", "\n")[:stream[exc.index][1]].count("\n") + 1
            raise ValueError("unsupported %s at line %d" % (exc.construct, line)) from None
        if body is not None:
            bodies[body] = (candidate[0], i)
    if len({namespace for namespace, _ in bodies.values()}) > 1:
        candidates = []
        for body, (namespace, index) in sorted(bodies.items()):
            line = text.replace("\r\n", "\n")[:stream[index][1]].count("\n") + 1
            label = "::".join(namespace + scope_names + (" ".join(name_tokens),))
            candidates.append("%s at line %d" % (label, line))
        raise ValueError("%s: ambiguous namespace candidates: %s" % (function, "; ".join(candidates)))
    return bodies


def locate(text, function, fragment):
    """Match one fragment in exactly the named definition, excluding nested bodies.

    A fragment can select an overload, but cannot resolve namespace ambiguity.
    Friend definitions cannot be referenced in this subset.
    """
    stream, values, pairs, parents, classes, namespaces, expression_tokens, lambdas, paths = analyze(text)
    bodies = definition_candidates(text, function)
    needle = [v for v, _ in tokens(fragment)]
    if not needle or "\x00literal" in needle:
        raise ValueError("source fragments must contain code tokens")
    hits = []
    for body in bodies:
        end = pairs[body]
        excluded = set()
        for start, (_, finish) in classes.items():
            if body < start < end:
                excluded.update(range(start, finish + 1))
        # Exclude only bodies of syntactically recognized nested lambdas.
        for start, finish in lambdas.items():
            if body < start < end:
                excluded.update(range(start, finish + 1))
        for i in range(body + 1, end - len(needle) + 1):
            if not any(j in excluded for j in range(i, i + len(needle))) and values[i : i + len(needle)] == needle:
                hits.append(stream[i][1])
    if len(hits) != 1:
        line = text.replace("\r\n", "\n")[:stream[0][1]].count("\n") + 1 if stream else 1
        raise ValueError("%s | %s: expected one fragment in one definition, found %d at line %d"
                         % (function, fragment, len(hits), line))
    return text.replace("\r\n", "\n")[: hits[0]].count("\n") + 1


@lru_cache(maxsize=256)
def resolve(text, reference):
    if " || " in reference:
        return " || ".join(resolve(text, part) for part in reference.split(" || "))
    if " | " not in reference:
        return reference
    function, fragment = reference.split(" | ", 1)
    function = function.removeprefix("model-only; call site ")
    line = locate(text, function, fragment)
    return "%s (line %d)" % (reference, line)


def self_test(root):
    from .self_tests import run

    run(root)


def fingerprints(root, source, package):
    def tree_hash(paths):
        data = bytearray()
        for p in sorted(paths):
            data.extend(str(p.relative_to(root)).encode() + b"\0")
            data.extend(p.read_bytes().replace(b"\r\n", b"\n") + b"\0")
        return digest(data)

    model_dir = root / "formal" / package
    data = (root / source).read_bytes()
    analyze(normalize(data).decode("utf-8"))
    return {
        "source_digest": digest(normalize(data)),
        "checker_digest": tree_hash(
            [
                p
                for d in ("engine", package)
                for p in (root / "formal" / d).rglob("*.py")
                if "__pycache__" not in p.parts
            ]
        ),
        "expected_digest": tree_hash(list((model_dir / "expected").glob("*.tsv")) + [model_dir / "sensitivity.txt"]),
    }


def quick_qualification(root, source, package):
    lock = json.loads((root / "formal" / package / "source.lock").read_text())
    full = lock.get("last_full_qualification")
    current = fingerprints(root, source, package)
    if (not full or full.get("source_digest_mode") != SOURCE_DIGEST_MODE
            or any(current[k] != full[k] for k in ("checker_digest", "expected_digest"))):
        raise ValueError(
            "quick refresh requires checker and expected digests from the last full qualification; run full"
        )
    return lock


def qualification(root, source, package, tier, assessment, count, outcomes):
    current = fingerprints(root, source, package)
    lock = quick_qualification(root, source, package) if tier == "quick" else {}
    latest = dict(
        date=time.strftime("%Y-%m-%d"),
        python=platform.python_version(),
        tier=tier,
        assessment=assessment,
        configuration_count=count,
        outcome_summary=outcomes,
    )
    if tier == "full":
        lock.update(current)
        lock["last_full_qualification"] = dict(latest, source_digest_mode=SOURCE_DIGEST_MODE, **current)
    else:
        lock["source_digest"] = current["source_digest"]
    lock.update(latest, source=source, source_digest_mode=SOURCE_DIGEST_MODE)
    return lock


def check(root, model, package):
    self_test(root)
    lock = json.loads((root / "formal" / package / "source.lock").read_text())
    errors = []
    if lock.get("source_digest_mode") != SOURCE_DIGEST_MODE:
        errors.append("source digest mode changed")
    for field, value in fingerprints(root, lock["source"], package).items():
        if lock.get(field) != value:
            errors.append(field.replace("_", "-") + " changed")
    text = (root / lock["source"]).read_text()
    for rid, pc, reference, kind, desc in model.step_table():
        try:
            resolve(text, reference)
        except ValueError as exc:
            errors.append("fragment failure %s.%s: %s" % (rid, pc, exc))
    if errors:
        raise ValueError(
            "\n".join(errors)
            + (
                "\nReview source/model fidelity, then run:\n"
                "python3 -B formal/{0}/check.py --tier full --update-expected\n"
                "python3 -B formal/{0}/check.py --tier full --refresh-lock "
                '--assessment "Describe the fidelity review"'
            ).format(package)
        )
    return lock
