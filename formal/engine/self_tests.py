"""Fast lexical, qualification and transaction checks without state exploration."""

import ast
import contextlib
import copy
import io
import json
import sys
import tempfile
from pathlib import Path
from types import ModuleType, SimpleNamespace
from unittest.mock import patch

from . import source_lock


def lexical_tests(root):
    # Byte hashing retains comments, whitespace, literals and directive lines.
    for text in (b"a/**/b", b'"//x" /* c */ R"tag(/*x*/)tag"', b"a // comment\\\n b"):
        assert source_lock.normalize(text) == text
    # Former normalization-equivalence inputs now retain every byte except CRLF.
    for text in ('"a  b"', 'R"(a  b)"', "'/' // x\n#define X a\\\n b\r\n"):
        assert source_lock.normalize(text.encode()) == text.replace("\r\n", "\n").encode()
    for left, right in (("a/**/b", "a b"), ('R"(a  b)"', 'R"(ab)"'),
                        ("#define A 1\n#define B 2", "#define A 1 #define B 2")):
        assert source_lock.normalize(left.encode()) != source_lock.normalize(right.encode())
    assert source_lock.normalize(b"a\r\nb\rc") == b"a\nb\rc"
    refused(lambda: source_lock.tokens("a\\\r\nb"))
    refused(lambda: source_lock.tokens("a\\\nb"))
    refused(lambda: source_lock.tokens('R"(a\\\nb)"'))
    assert source_lock.locate("namespace N { bool A::f() { return true; } }", "A::f", "return true;") == 1
    assert [v for v, _ in source_lock.tokens("a <<= 1; 0x1 1e+2")] != [
        v for v, _ in source_lock.tokens("a << = 1; 0 x1 1e + 2")
    ]
    assert [v for v, _ in source_lock.tokens("1e+2")] == ["1e+2"]
    assert [v for v, _ in source_lock.tokens("1e+2")] != [v for v, _ in source_lock.tokens("1e + 2")]
    fixture = "class A { bool f() { return true; } }; class B { bool f() { return true; } };"
    assert source_lock.locate(fixture, "A::f", "return true;") == 1
    assert source_lock.locate("bool A::f() const noexcept { return true; }", "A::f", "return true;") == 1
    assert source_lock.locate("class A { A(int x) : x_{x} { use(x); } };", "A::A", "use(x);") == 1
    assert source_lock.locate("class A { void f(int) { one(); } void f() { two(); } };", "A::f", "one();") == 1
    for fixture in (
        'class A { void f() { auto s = "return true;"; } };',
        "class A { void f() { auto c = 'x'; } };",
        "class A { void f() { /* return true; */ } };",
        "class A { void f() { class B { void g() { return true; } }; } };",
        "class A { void f() { return true; return true; } };",
        "class A { void f(int) { return true; } void f() { return true; } };",
        "class A { void f() {} void g() { return true; } };",
        "class A { void f() { auto g = []() { return true; }; } };",
    ):
        try:
            source_lock.locate(fixture, "A::f", "return true;")
        except ValueError:
            pass
        else:
            raise AssertionError("invalid fragment accepted: " + fixture)
    for path in (root / "formal").rglob("*.py"):
        ast.parse(path.read_text(), filename=str(path), feature_version=(3, 9))
    # The real header is a positive subset fixture, not an engine identifier.
    for lock_path in (root / "formal").glob("*/source.lock"):
        source = json.loads(lock_path.read_text())["source"]
        source_lock.analyze((root / source).read_text())


def regression_cases():
    """Source-matching regression fixtures, independent of the selected implementation."""
    raw = "1'000; auto s = R\"tag(a\\\nb)tag\"; 2'000;"
    joined = "1'000; auto s = R\"tag(ab)tag\"; 2'000;"
    cases = [("pp-number before raw literal", "different", raw, joined)]
    for number in ("1'000e+2", "0x1'0.fp+2", "0x1.fp+1'0"):
        cases.append(("pp-number " + number, "different", raw.replace("1'000", number),
                      joined.replace("1'000", number)))
    for prefix in ("u8", "u", "U", "L"):
        declaration = "auto c = " + prefix + "'1'; auto s ="
        cases.append(("literal prefix " + prefix, "different", raw.replace("auto s =", declaration),
                      joined.replace("auto s =", declaration)))
    cases.append(("continued pp-number before raw literal", "different",
                  raw.replace("1'000", "1\\\n'000"), joined.replace("1'000", "1\\\n'000")))
    for description, body in (
        ("anonymous class", "class { void g() { target(); } } x;"),
        ("anonymous struct", "struct { void g() { target(); } } x;"),
        ("anonymous union", "union { void g() { target(); } } x;"),
        ("named union", "union B { void g() { target(); } };"),
        ("class with bases", "class B : X, Y { void g() { target(); } };"),
        ("class specialization", "class B<int> { void g() { target(); } };"),
    ):
        cases.append((description, "absent", "class A { void f() { " + body + " } };", "target();"))
    for description, declaration in (
        ("decltype member", "decltype(f()) g()"),
        ("default argument", "void g(int x = f())"),
        ("noexcept operand", "void g() noexcept(f())"),
        ("trailing return", "auto g() -> decltype(f())"),
    ):
        text = "class A { void f(); " + declaration + " { target(); } };"
        cases.append((description, "absent", text, "target();"))
    for expression in ("a[0]", "a()[0]", "a[0][0]", '"x"[0]', "this[0]", "1'000[a]"):
        text = "class A { void f() { if (" + expression + ") { target(); } } };"
        cases.append(("subscript " + expression, "present", text, "target();"))
    return cases


def regression_case(module, case):
    description, mode, text, fragment = case
    if mode == "different":
        assert module.normalize(text.encode()) != module.normalize(fragment.encode()), description
        # The byte digest does not parse C++; the locating tokenizer separately
        # retains the fail-closed continuation rule for these older fixtures.
        refused(lambda: module.tokens(text))
    elif mode == "present":
        assert module.locate(text, "A::f", fragment) == 1, description
    else:
        try:
            module.locate(text, "A::f", fragment)
        except ValueError:
            return
        raise AssertionError(description + ": nested fragment accepted")


def subset_cases():
    """Round-3 fixtures: each exposes a false match or missing rejection in round 2."""
    cases = []
    for description, text in (
        ("split raw prefix", 'auto s = R\\\n"tag(a\\\nb)tag";'),
        ("split encoded raw prefix", 'auto s = u8R\\\n"(ab)";'),
        ("raw contents", 'auto s = R"(a\\\nb)";'),
        ("raw directive-looking contents", 'auto s = R"(\n#define X a\\\nb)";'),
        ("string contents", 'auto s = "a\\\nb";'),
        ("character contents", "auto c = 'a\\\n';"),
        ("split token", "tar\\\nget();"),
        ("between tokens", "target \\\n ();"),
        ("CRLF continuation", "target\\\r\n();"),
        ("line comment", "// comment\\\n continued\ntarget();"),
        ("block comment", "/*\n#define X a\\\nb\n*/"),
    ):
        cases.append((description, "tokens-reject", text, "line continuation"))
    for description, text in (
        ("template trailing-return call", "auto g() -> Box<A::f()> { target(); return {}; }"),
        ("nested template trailing return", "auto g() -> Box<Box<A::f()>> { target(); return {}; }"),
        ("class base braced template argument",
         "void A::f() { struct B : Base<Tag{}> { void g() { target(); } }; }"),
    ):
        cases.append((description, "absent", text, "target();"))
    for description, body, diagnostic in (
        ("front lambda attribute", "[] [[nodiscard]] () { target(); };", "lambda"),
        ("back lambda attribute", "[] () [[nodiscard]] { nested(); }; target();", "lambda"),
        ("template lambda", "[]<class T>() { nested(); }; target();", "lambda"),
        ("static lambda", "[]() static { nested(); }; target();", "lambda"),
        ("misplaced lambda specifier", "[] constexpr () { nested(); }; target();", "lambda"),
        ("lambda constraint", "[]() requires C<int> { nested(); }; target();", "requires-clause"),
        ("lambda return attribute", "[]() -> int [[nodiscard]] { nested(); }; target();", "lambda"),
        ("co_await lambda", "co_await [] { target(); }();", "co_await"),
        ("co_yield lambda", "co_yield [] { target(); };", "co_yield"),
        ("co_return", "co_return value; target();", "co_return"),
        ("body requires expression", "bool b = requires { nested(); }; target();", "requires-expression"),
        ("nested body requires expression",
         "bool b = requires { requires requires { nested(); }; }; target();", "requires-expression"),
        ("ambiguous class angles", "struct B : Base<N << 1> { void g() {} }; target();", "template"),
    ):
        cases.append((description, "locate-reject", "void A::f() {\n" + body + "\n}", diagnostic))
    for description, declaration in (
        ("ambiguous declaration shift", "auto g() -> Box<N << 1>"),
        ("ambiguous declaration comparison", "auto g() -> Box<N <= 1>"),
        ("ambiguous declaration greater-equal", "auto g() -> Box<N >= 1>"),
        ("unbalanced declaration angles", "auto g() -> Box<Box<int>"),
        ("ambiguous declaration close", "auto g() -> Box<int>>"),
    ):
        text = "void A::f() { target(); }\n" + declaration + " { return {}; }"
        cases.append((description, "locate-reject", text, "template"))
    cases.append(("ambiguous trailing requires", "locate-reject",
                  "void A::f() requires (C<int>) { target(); }", "trailing requires"))
    for expression in ("sizeof [] { target(); };", "if (true) [] { target(); }();",
                       "if (false) {} else [] { target(); }();", "do [] { target(); }(); while (false);"):
        cases.append(("lambda expression position " + expression, "absent",
                      "void A::f() { " + expression + " }", "target();"))
    return cases


def subset_case(module, case):
    description, mode, text, diagnostic = case
    if mode == "absent":
        regression_case(module, case)
        return
    try:
        if mode == "tokens-reject":
            module.tokens(text)
        else:
            module.locate(text, "A::f", "target();")
    except ValueError as exc:
        assert diagnostic in str(exc) and "line " in str(exc), (description, str(exc))
        return
    raise AssertionError(description + ": unsupported construct accepted")


def subset_tests():
    for case in subset_cases():
        subset_case(source_lock, case)
    assert source_lock.spliced("#define X a\\\n b\n")[0] == "#define X a b\n"
    for suffix in ("", "()", "() mutable", "() constexpr", "() consteval", "() noexcept",
                   "() noexcept(true)", "() mutable constexpr noexcept(true) -> Box<Box<int>>"):
        text = "void A::f() { []" + suffix + " { nested(); }; target(); }"
        assert source_lock.locate(text, "A::f", "target();") == 1
        refused(lambda: source_lock.locate(text, "A::f", "nested();"))
    text = "void A::f() { a[0](); if (a[0]()) { target(); } }"
    assert source_lock.locate(text, "A::f", "target();") == 1
    for head in ("Box<Box<int>>", "Box<(N << 1)>", "Box<(N <= 1)>", "Box<(N >= 1)>",
                 "Box<decltype(X::operator<(1))>", "Box<decltype(X::operator>>(1))>"):
        text = "struct B : " + head + " { void g() {} }; void A::f() { target(); }"
        assert source_lock.locate(text, "A::f", "target();") == 1
    text = "template<class T> requires C<T> void A::f() { target(); }"
    assert source_lock.locate(text, "A::f", "target();") == 1
    text = "namespace N { template<class T> concept C = requires(T t) { t(); }; } " + text
    assert source_lock.locate(text, "A::f", "target();") == 1
    for text in ("auto A::f() -> Box<Tag{}> { target(); }",
                 "void A::f() requires requires { nested(); } { target(); }"):
        assert source_lock.locate(text, "A::f", "target();") == 1
        refused(lambda: source_lock.locate(text, "A::f", "nested();"))


def allowlist_cases():
    """Round-4 fixtures: no unknown tail, digraph, attribute or constraint passes."""
    cases = [("initializer call before lambda", "absent",
              "struct A { int f(); int x = f() + [] { target(); return 0; }(); };", "target();")]
    for tail in ("+ []", "* []", "|| []", ", []", "[0]", "unexpected"):
        cases.append(("non-declarator tail " + tail, "absent",
                      "struct A { int f(); int x = f() " + tail + " { target(); }; };", "target();"))
    for description, text in (
        ("digraph lambda", "void A::f() { <: :> { target(); }; }"),
        ("digraph class", "void A::f() { struct B <% void g() { target(); } %>; }"),
    ):
        cases.append((description, "locate-reject", text, "digraph"))
    for digraph in ("<%", "%>", "<:", ":>", "%:", "%:%:", "<:::", "<::>"):
        cases.append(("digraph code " + digraph, "locate-reject",
                      "void A::f() { " + digraph + "; target(); }", "digraph"))
    cases.append(("digraph directive", "locate-reject",
                  "#define X %:%:\nvoid A::f() { target(); }", "digraph"))
    for description, attribute in (
        ("attribute lambda", "[[assume([] [[nodiscard]] () { return true; }())]]"),
        ("attribute subscript", "[[assume(a[0])]]"),
        ("attribute brace", "[[assume(X{})]]"),
        ("attribute semicolon", "[[assume(x; y)]]"),
        ("attribute operator", "[[assume(x + y)]]"),
        ("attribute scope argument", "[[assume(N::x)]]"),
        ("attribute trailing comma", "[[a, b,]]"),
        ("attribute using", "[[using N: a]]"),
        ("attribute third name", "[[N::a::b]]"),
        ("attribute empty name", "[[]]"),
        ("attribute bare arguments", "[[a x]]"),
    ):
        cases.append((description, "locate-reject",
                      "void A::f() { target(); " + attribute + "; }", "attribute"))
    for qualifiers in ("noexcept", "const", "volatile", "&", "&&", "override", "final",
                       "[[a]]", "-> int"):
        text = ("void A::f() { target(); }\n"
                "template<class T> void g() " + qualifiers + " requires (C<T>) {}")
        cases.append(("unreferenced constraint after " + qualifiers, "locate-reject", text, "trailing requires"))
    cases.append(("unreferenced requires operator", "locate-reject",
                  "void A::f() { target(); }\nvoid g() requires C<T> + D<T> {}", "requires-clause"))
    return cases


def allowlist_tests():
    for case in allowlist_cases():
        subset_case(source_lock, case)
    for suffix in ("const volatile & noexcept(true)", "&& noexcept", "override final", "[[N::a(x, (y), 1)]]",
                   "-> Box<Box<int>>", "const noexcept requires C<int> && N::D<int>",
                   "noexcept requires requires { nested(); }"):
        text = "void A::f() " + suffix + " { target(); }"
        assert source_lock.locate(text, "A::f", "target();") == 1, suffix
    for tail in ("try { target(); } catch (...) {}", ": x(1), y{2} { target(); }",
                 "noexcept try : x(1) { target(); } catch (...) {}"):
        assert source_lock.locate("struct A { A() " + tail + " };", "A::A", "target();") == 1
    for attribute in ("[[a]]", "[[N::a]]", "[[a()]]", '[[a(x, (y), 1, 0x1p+2, "[]{};")]]'):
        text = attribute + " void A::f() { " + attribute + "; target(); }"
        assert source_lock.locate(text, "A::f", "target();") == 1, attribute
    for text in ("void A::f() { target(); } // <% :> %:%:",
                 'void A::f() { auto s = "<% %> <: :> %: %:%:"; target(); }',
                 "Box<::N::T> x; void A::f() { target(); }"):
        assert source_lock.locate(text, "A::f", "target();") == 1
    for keyword in ("mutable", "constexpr", "consteval", "noexcept", "static"):
        refused(lambda: source_lock.analyze("void A::f() { []() -> int " + keyword + " { nested(); }; }"))
    for keyword in ("co_await", "co_yield", "co_return"):
        refused(lambda: source_lock.analyze("[[assume(" + keyword + " x)]] void g();"))
    for qualifiers in ("noexcept(true)", "const & noexcept(true)"):
        refused(lambda: source_lock.analyze("void g() " + qualifiers + " requires (C<T>) {}"))
    refused(lambda: source_lock.locate("struct A { int f(); int x = f() = [] { target(); }; };",
                                       "A::f", "target();"))
    assert [v for v, _ in source_lock.tokens("<::")] == ["<", "::"]
    assert [v for v, _ in source_lock.tokens("<::T")] == ["<", "::", "T"]


def refused(action):
    try:
        action()
    except ValueError:
        return
    raise AssertionError("invalid input accepted")


def byte_digest_cases():
    """All pairs collided under lexical normalization; every byte edit now counts."""
    cases = [("comment-only", "int x;", "int x; // comment"),
             ("whitespace-only", "int x;", "int  x;"),
             ("include header spaces", "#include <a b>\n", "#include <a  b>\n")]
    for whitespace in (" ", "\t", " \t"):
        cases.append(("comment splice " + repr(whitespace), "void A::f() {\n\ntarget();\n}",
                      "void A::f() {\n// swallowed\\" + whitespace + "\ntarget();\n}"))
    return cases


def class_head_cases():
    cases = []
    for name, head in (
        ("local qualified class", "struct B { struct C; }; struct B::C"),
        ("UCN class", r"struct \u03B1"),
        ("UCN suffix class", r"struct B\u03B1"),
        ("non-ASCII class", "struct Café"),
        ("non-ASCII initial class", "struct 日本"),
        ("specialized class", "struct B<int>"),
        ("anonymous class", "struct"),
        ("class alignment", "struct alignas(16) B"),
    ):
        cases.append((name, "locate-reject", "void A::f() {\n" + head
                      + " { void g() { target(); } };\n}", "class head"))
    return cases


def byte_scope_tests():
    for name, before, after in byte_digest_cases():
        assert source_lock.digest(source_lock.normalize(before.encode())) != source_lock.digest(
            source_lock.normalize(after.encode())), name
    for whitespace in (" ", "\t", " \t"):
        splice = "\\" + whitespace + "\n"
        for text in ("tar" + splice + "get();", "/* comment" + splice + "*/ target();",
                     'auto s = "a' + splice + 'b";', '// comment' + splice + 'target();'):
            try:
                source_lock.tokens(text)
            except ValueError as exc:
                assert "line continuation" in str(exc) and "line 1" in str(exc), str(exc)
            else:
                raise AssertionError("whitespace continuation accepted")
        directive = "#define X tar" + splice + "get()\nvoid A::f() { target(); }"
        logical, offsets = source_lock.spliced(directive)
        assert logical == "#define X target()\nvoid A::f() { target(); }"
        assert offsets[logical.index("get")] == directive.index("get")
        assert source_lock.locate(directive, "A::f", "target();") == 3
        # A continued // comment must still hide directive-looking text.
        refused(lambda: source_lock.tokens("// c" + splice + "#define X a" + splice + "b"))
    for header in ("<a b>", "<a  b>", "<a//b>", "<a/*b>", "<a<%b>", '"a b"'):
        directive = "#include " + header + "\n"
        match = next(source_lock.TOKEN.finditer(directive))
        assert match["header"] == header  # One opaque header-name, including its whitespace.
        assert source_lock.tokens(directive) == []
        source_lock.analyze(directive + "void A::f() { target(); }")
    for case in class_head_cases():
        subset_case(source_lock, case)
    for head in ("class B", "struct [[nodiscard]] B final", "union B", "struct B : public X, virtual Y"):
        text = "void A::f() { " + head + " { void g() { nested(); } }; target(); }"
        assert source_lock.locate(text, "A::f", "target();") == 1
        refused(lambda: source_lock.locate(text, "A::f", "nested();"))


def declarator_cases():
    """Round-7 review examples, using synthetic source rather than header details."""
    return [
        ("constructor versus destructor", "struct inplace_stop_callback {\n"
         "inplace_stop_callback() { construct(); }\n"
         "~inplace_stop_callback() { source_->remove_callback(this); }\n};",
         "inplace_stop_callback::inplace_stop_callback", "source_->remove_callback(this);"),
        ("member versus inline friend", "struct inplace_stop_token {\n"
         "void swap(inplace_stop_token& other) { member_swap(other); }\n"
         "friend void swap(inplace_stop_token& a, inplace_stop_token& b) { a.swap(b); }\n};",
         "inplace_stop_token::swap", "a.swap(b);"),
    ]


def declaration_prefix_cases():
    """Round-8 regressions: initializer calls and friends with braced operands."""
    return [
        ("constructor mem-initializer", "struct X {\n"
         "X(Token token) : callback_(token), source_(token.source_) {\nsource_ = nullptr;\n}\n};",
         "X::source_", "source_ = nullptr;"),
        ("single mem-initializer", "struct X { X() : source_(nullptr) { target(); } };",
         "X::source_", "target();"),
        ("friend with braced decltype operand", "struct X { friend decltype(int{}) f() { return 1; } };",
         "X::f", "return 1;"),
    ]


def declarator_tests():
    for description, text, reference, fragment in declarator_cases():
        refused(lambda: source_lock.locate(text, reference, fragment))
    callback = declarator_cases()[0][1]
    assert source_lock.locate(callback, "inplace_stop_callback::inplace_stop_callback", "construct();") == 2
    assert source_lock.locate(callback, "inplace_stop_callback::~inplace_stop_callback",
                              "source_->remove_callback(this);") == 3
    refused(lambda: source_lock.locate(callback, "inplace_stop_callback::~inplace_stop_callback", "construct();"))
    token = declarator_cases()[1][1]
    assert source_lock.locate(token, "inplace_stop_token::swap", "member_swap(other);") == 2
    refused(lambda: source_lock.locate(token, "::swap", "a.swap(b);"))
    for text in ("struct X { template<class T> friend void f(T) { target(); } };",
                 "struct X { [[nodiscard]] friend bool f() { target(); } };",
                 "struct X { friend void f() { target(); } }; void X::g() {}"):
        refused(lambda: source_lock.locate(text, "X::f", "target();"))
        refused(lambda: source_lock.locate(text, "::f", "target();"))
    # Complete operator ids, including conversion types, cannot supply a suffix.
    for name, declaration in (
        ("operator()", "int operator()()"), ("operator[]", "int operator[](int)"),
        ("operator+", "X operator+()"), ("operator++", "X& operator++()"),
        ("operator=", "X& operator=(const X&)"), ("operator==", "bool operator==(const X&) const"),
        ("operator new[]", "static void* operator new[](unsigned long)"),
        ("operator bool", "operator bool()"), ("operator N::T", "operator N::T()"),
        ("operator unsigned long", "operator unsigned long()"),
    ):
        text = "struct X { " + declaration + " { target(); } };"
        assert source_lock.locate(text, "X::" + name, "target();") == 1, name
    for reference, name in (("X::operator+", "operator++"), ("X::operator=", "operator=="),
                            ("X::operator new", "operator new[]"), ("X::operator unsigned", "operator unsigned long"),
                            ("X::T", "operator N::T"), ("X::f", "operator f")):
        refused(lambda: source_lock.locate("struct X { " + name + "() { target(); } };",
                                           reference, "target();"))
    refused(lambda: source_lock.locate("namespace N { void f() { target(); } } "
                                       "namespace M { void f() {} }", "::f", "target();"))
    for text in ("void f() { target(); }", "namespace N { void f() { target(); } }"):
        assert source_lock.locate(text, "::f", "target();") == 1
    for description, text, reference, fragment in declaration_prefix_cases():
        refused(lambda: source_lock.locate(text, reference, fragment))
        assert not source_lock.definition_candidates(text, reference), description
    text = declaration_prefix_cases()[0][1]
    assert source_lock.locate(text, "X::X", "source_ = nullptr;") == 3
    for operand in ("int{}", "T{g()}", "g(T{})"):
        declaration = "decltype(" + operand + ") f() { target(); }"
        assert source_lock.locate("struct X { " + declaration + " };", "X::f", "target();") == 1
        for prefix in ("friend ", "[[nodiscard]] friend ", "template<class T> friend "):
            text = "struct X { " + prefix + declaration + " };"
            refused(lambda: source_lock.locate(text, "X::f", "target();"))
            refused(lambda: source_lock.locate(text, "::f", "target();"))
    for initializer in (": source_(nullptr)", ": callback_(0), source_(nullptr)",
                        "try : source_(nullptr)", ": X(0)"):
        text = "struct X { X() " + initializer + " { target(); } };"
        assert source_lock.locate(text, "X::X", "target();") == 1
        assert not source_lock.definition_candidates(text, "X::source_")
    # Calls inside a data initializer or a balanced type/specifier operand
    # cannot borrow another declaration's body.
    for declaration in ("decltype(f()) g()", "auto g() noexcept(f())", "auto g() -> decltype(f())",
                        "int x = f() + []", "int x = f(), y = []"):
        text = "struct X { int f(); " + declaration + " { target(); }; };"
        assert not source_lock.definition_candidates(text, "X::f"), declaration


def exact_scope_cases():
    """Round-6 review examples: neither unrelated class path supplies A::f."""
    return [
        ("extra out-of-class qualifier",
         "void target();\nstruct A { void f() {} };\n"
         "struct B { struct A { void f(); }; };\nvoid B::A::f() { target(); }"),
        ("local class inside function",
         "void target();\nstruct A { void f() {} };\n"
         "void g() { struct A { void f() { target(); } }; }"),
    ]


def exact_scope_tests():
    for description, text in exact_scope_cases():
        try:
            source_lock.locate(text, "A::f", "target();")
        except ValueError as exc:
            assert "found 0 at line" in str(exc), (description, str(exc))
        else:
            raise AssertionError(description + ": unrelated scope accepted")
    for text in (
        "struct A { void f() { target(); } };",
        "struct A { void f(); }; void A::f() { target(); }",
        "struct A { void f(); }; void ::A::f() { target(); }",
        "namespace N { struct A { void f() { target(); } }; }",
        "namespace N { struct A { void f(); }; void A::f() { target(); } }",
        "namespace N { struct A { void f(); }; } void N::A::f() { target(); }",
        "namespace N::M { struct A { void f(); }; } void N::M::A::f() { target(); }",
        "namespace N { namespace M { struct A { void f(); }; } void M::A::f() { target(); } }",
        "namespace { struct A { void f() { target(); } }; }",
        "namespace N::M {} namespace N { struct A { void f(); }; } void N::A::f() { target(); }",
        "namespace N { struct A { void f(); }; } namespace N { void A::f() { target(); } }",
        "namespace B {} namespace N { struct B { struct A { void f(); }; }; "
        "void B::A::f() { target(); } }",
        "struct A { void f() { target(); } }; "
        "struct B { struct A { void f() { target(); } }; };",
        "struct A { void f() { target(); } }; "
        "void g() { struct A { void f() { target(); } }; }",
    ):
        expected = "B::A::f" if text.startswith("namespace B {}") else "A::f"
        assert source_lock.locate(text, expected, "target();") == 1, text
    for text in (
        "struct B { struct A { void f() { target(); } }; };",
        "void g() { { struct A { void f() { target(); } }; } }",
        "struct B { void g() { struct A { void f() { target(); } }; } };",
        "void g() { struct B { struct A { void f() { target(); } }; }; }",
    ):
        refused(lambda: source_lock.locate(text, "A::f", "target();"))
    for definition in ("struct B { struct A { void f() { target(); } }; };",
                       "struct B { struct A { void f(); }; }; void B::A::f() { target(); }"):
        assert source_lock.locate(definition, "B::A::f", "target();") == 1
    # A fragment cannot choose a namespace, even when one candidate is empty.
    for body in ("", "target();"):
        text = ("namespace N { struct A { void f() { target(); } }; }\n"
                "namespace M { struct A { void f() { " + body + " } }; }")
        try:
            source_lock.locate(text, "A::f", "target();")
        except ValueError as exc:
            diagnostic = str(exc)
            assert "ambiguous namespace candidates" in diagnostic, diagnostic
            assert "N::A::f at line 1" in diagnostic and "M::A::f at line 2" in diagnostic, diagnostic
        else:
            raise AssertionError("namespace ambiguity accepted")
    # Same-namespace overloads may still be selected by a unique fragment.
    text = "namespace N { struct A { void f(int) {} void f() { target(); } }; }"
    assert source_lock.locate(text, "A::f", "target();") == 1


def qualification_tests(root):
    source = "sample.hpp"
    package = "sample"
    (root / source).write_text("struct A { int f() { return 1; } };\n")
    directory = root / "formal" / package
    (root / "formal/engine").mkdir(parents=True)
    (directory / "expected").mkdir(parents=True)
    checker = directory / "check.py"
    checker.write_text("# synthetic checker\n")
    expected = directory / "expected/results.tsv"
    expected.write_text("synthetic expected\n")
    (directory / "sensitivity.txt").write_text("# Hand-reviewed synthetic manifest\nfail\n")
    path = directory / "source.lock"
    full = source_lock.qualification(root, source, package, "full", "Synthetic full", 2, {"pass": 1, "fail": 1})
    path.write_text(json.dumps(full))
    assert full["last_full_qualification"]["checker_digest"] == full["checker_digest"]
    source_path = root / source
    source_path.write_text(source_path.read_text().replace("return 1", "return 2"))
    quick = source_lock.qualification(root, source, package, "quick", "Synthetic source edit", 1, {"pass": 1})
    assert quick["source_digest"] != full["source_digest"]
    for key in ("checker_digest", "expected_digest", "last_full_qualification"):
        assert quick[key] == full[key]
    assert quick["tier"] == "quick" and quick["configuration_count"] == 1
    source_path.write_text(source_path.read_text() + "// comment-only edit\n")
    assert source_lock.fingerprints(root, source, package)["source_digest"] != quick["source_digest"]
    comments = source_lock.qualification(root, source, package, "quick",
                                        "Comment-only edit; executable tokens and model unchanged", 1, {"pass": 1})
    assert comments["source_digest"] != quick["source_digest"]
    assert comments["last_full_qualification"] == full["last_full_qualification"]
    assert comments["source_digest_mode"] == source_lock.SOURCE_DIGEST_MODE
    wrong_mode = copy.deepcopy(full)
    wrong_mode["last_full_qualification"]["source_digest_mode"] = "legacy"
    path.write_text(json.dumps(wrong_mode))
    refused(lambda: source_lock.quick_qualification(root, source, package))
    path.write_text(json.dumps(full))
    for target in (checker, expected, directory / "sensitivity.txt"):
        before = target.read_bytes()
        target.write_bytes(before + b"\n# changed\n")
        refused(lambda: source_lock.quick_qualification(root, source, package))
        target.write_bytes(before)
    path.write_text(json.dumps({k: v for k, v in full.items() if k != "last_full_qualification"}))
    refused(lambda: source_lock.quick_qualification(root, source, package))
    path.write_text(json.dumps(full))
    return source, package


def output_tests(root):
    from . import runner

    out = root / "out"
    tasks = [{"name": "sample"}]
    runner.guard_outputs(root, out, tasks)
    assert not out.exists()
    out.mkdir()
    (out / "traces").mkdir()
    # Each symlink is refused even when its target is outside the asset.
    for destination in (out / "results.jsonl", out / "summary.md", out / "traces/sample.txt"):
        destination.symlink_to(root / "untouched")
        refused(lambda: runner.guard_outputs(root, out, tasks))
        destination.unlink()
    (out / "traces").rmdir()
    (out / "traces").symlink_to(root / "untouched-directory", target_is_directory=True)
    refused(lambda: runner.guard_outputs(root, out, tasks))
    (out / "traces").unlink()
    out.rmdir()
    out.symlink_to(root / "untouched-directory", target_is_directory=True)
    refused(lambda: runner.guard_outputs(root, out, tasks))
    out.unlink()
    parent = root / "parent"
    parent.symlink_to(root / "formal", target_is_directory=True)
    refused(lambda: runner.guard_outputs(root, parent / "nested", tasks))
    refused(lambda: runner.guard_outputs(root, root / "formal/out", tasks))
    assert not (root / "untouched").exists() and not (root / "untouched-directory").exists()


class SyntheticPool:
    """Exercise runner decisions with records, never with model exploration."""

    def __init__(self, records):
        self.records = records

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False

    def imap_unordered(self, function, items):
        return iter(copy.deepcopy(self.records))


def transaction_tests(root, source, package):
    from . import runner

    directory = root / "formal" / package
    expected = directory / "expected/results.tsv"
    lock = directory / "source.lock"
    manifest = directory / "sensitivity.txt"
    tasks = [{"name": "pass", "model": "synthetic"}, {"name": "fail", "model": "synthetic", "mut": "kill"}]
    records = [
        dict(name=t["name"], task=t, complete=True, states=1, transitions=0, terminal_states=1,
             violations=[] if t["name"] == "pass" else [{"category": "synthetic violation", "length": 1}],
             semantic={"synthetic": t["name"]}, peak_rss_kib=None)
        for t in tasks
    ]
    expected_text = "name\tstates\ttransitions\tterminal_states\toutcome\tsemantic_sha256\n"
    expected_text += "\n".join(sorted(runner.expected_record(r) for r in records)) + "\n"
    expected.write_text(expected_text)
    model = ModuleType(package + ".model")
    model.STEPS = {}
    model.step_table = lambda: []
    tiers = ModuleType(package + ".tiers")
    tiers.select = lambda tier: list(tasks)
    catalog = ModuleType(package + ".mutants")
    catalog.CATALOG = {"kill": {"outcome": "killed"}}
    catalog.declared = lambda task: task["name"] in {"fail"}
    modules = {m.__name__: m for m in (model, tiers, catalog)}
    full = source_lock.qualification(root, source, package, "full", "Synthetic baseline", 2, {"pass": 1, "fail": 1})
    lock.write_text(json.dumps(full))
    context = SimpleNamespace(Pool=lambda *args, **kwargs: SyntheticPool(records))

    def call(args, status):
        argv = ["check.py", "--out", str(root / "results")] + args
        with patch.object(sys, "argv", argv), contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            try:
                actual = runner.main(root, package, source)
            except SystemExit as exc:
                actual = exc.code
        assert actual == status, (args, actual, status)

    with patch.dict(sys.modules, modules), patch.object(runner.mp, "get_context", return_value=context), \
            patch.object(source_lock, "self_test"):
        before = (expected.read_bytes(), lock.read_bytes(), manifest.read_bytes())
        for args in (
            ["--only", "x", "--update-expected"],
            ["--only", "x", "--refresh-lock", "--assessment", "test"],
            ["--only", "", "--update-expected"],
            ["--update-expected", "--refresh-lock"], ["--refresh-lock"],
            ["--steps", "--update-expected"], ["--check-source", "--update-expected"],
        ):
            call(args, 2)
        for invalid in ("empty", "incomplete", "manifest", "missing killer"):
            saved = copy.deepcopy(records)
            if invalid == "empty":
                tasks.clear()
            elif invalid == "incomplete":
                records[0]["complete"] = False
            elif invalid == "manifest":
                records[1]["violations"] = []
            else:
                tasks[1].pop("mut", None)
                records[1]["task"].pop("mut", None)
            for args in (["--update-expected"], ["--refresh-lock", "--assessment", "test"]):
                call(args, 2 if invalid == "empty" else 1)
                assert (expected.read_bytes(), lock.read_bytes(), manifest.read_bytes()) == before
            tasks[:] = [dict(r["task"]) for r in saved]
            records[:] = saved
        # Expected mismatches block refresh, but a complete update may repair them.
        expected.write_text("stale\n")
        call(["--refresh-lock", "--assessment", "test"], 1)
        assert lock.read_bytes() == before[1]
        call(["--update-expected"], 0)
        assert expected.read_text() == expected_text
        assert lock.read_bytes() == before[1] and manifest.read_bytes() == before[2]
        call(["--tier", "full", "--refresh-lock", "--assessment", "Synthetic full"], 0)
        assert json.loads(lock.read_text())["configuration_count"] == 2
        assert manifest.read_bytes() == before[2]
        # Exercise the source-check path under both platform absences in-process.
        real_read = Path.read_text

        def no_proc(path, *args, **kwargs):
            if str(path).startswith("/proc"):
                raise FileNotFoundError("unavailable")
            return real_read(path, *args, **kwargs)

        with patch.object(runner, "resource", None), patch.object(Path, "read_text", no_proc):
            assert runner.default_jobs() == 1 and runner.peak_rss_kib() is None
            call(["--check-source"], 0)
    if runner.resource is not None:
        for system, expected_rss in (("Darwin", 2), ("Linux", 2048)):
            with patch.object(runner.platform, "system", return_value=system), \
                    patch.object(runner.resource, "getrusage", return_value=SimpleNamespace(ru_maxrss=2048)):
                assert runner.peak_rss_kib() == expected_rss


def run(root):
    lexical_tests(root)
    spawn_subset_tests()
    qualified_template_tests()
    from .memory_self_tests import run as memory_tests
    memory_tests()
    counting_subset_tests()
    subset_tests()
    allowlist_tests()
    byte_scope_tests()
    exact_scope_tests()
    declarator_tests()
    for case in regression_cases():
        regression_case(source_lock, case)
    with tempfile.TemporaryDirectory(prefix="formal-self-tests-") as temp:
        scratch = Path(temp)
        source, package = qualification_tests(scratch)
        output_tests(scratch)
        transaction_tests(scratch, source, package)


def counting_subset_tests():
    """Extensions required by counting_scope, with rejecting near-neighbors."""
    for attribute in ('[[nodiscard, deprecated("old")]]', '[[N::a(x), b, c((1))]]'):
        assert source_lock.locate(attribute + ' void A::f() { target(); }', 'A::f', 'target();') == 1
    for attribute in ('[[,a]]', '[[a,,b]]', '[[a,b,]]', '[[a, using N: b]]',
                      '[[a, b(x + y)]]', '[[a, b([]{})]]', '[[a, N::b::c]]'):
        refused(lambda: source_lock.analyze(attribute + ' void f();'))
    for constraint in ('(!N::C<T>)', '(!C<T> && N::D<T> && E<T, const B&>)'):
        text = 'template<class T> requires ' + constraint + ' void A::f() { target(); }'
        assert source_lock.locate(text, 'A::f', 'target();') == 1
    for constraint in ('(!C<T> + D<T>)', '(!C<T> && f())', '(!C<T> || D<T>)',
                       '(!C<T> && []{})', '(!C<T>; x)', '(!C<T> && (D<T>))',
                       '(!true)', '(!false)', '(!int)', '(!C<T> && true)',
                       '(!N::false)', '(!class<T>)', '(!C<T> && N::return)'):
        refused(lambda: source_lock.analyze('void A::f() requires ' + constraint + ' {}'))
    text = 'namespace N { class A { class B; }; class A::B { void f() { target(); } }; }'
    assert source_lock.locate(text, 'A::B::f', 'target();') == 1
    refused(lambda: source_lock.locate(text, 'B::f', 'target();'))
    for head in ('A::', 'A::B<int>', 'A::B::', 'A::日本'):
        refused(lambda: source_lock.analyze('class ' + head + ' { void f() {} };'))
    refused(lambda: source_lock.analyze('void g() { class A::B { void f() {} }; }'))
    # Scoped enum heads already work; they must not provide a class scope.
    text = 'enum class E : std::uint8_t { x }; void A::f() { target(); }'
    assert source_lock.locate(text, 'A::f', 'target();') == 1
    refused(lambda: source_lock.locate(text, 'E::f', 'target();'))

    for text in ('namespace N {} class N::A { void f() {} };',
                 'class Unknown::B { void f() {} };'):
        refused(lambda: source_lock.analyze(text))


def spawn_subset_tests():
    """Target-required extensions retain exact scope and nested exclusion."""
    primary = 'template<class T> struct X; '
    for arg in ('int', 'F(T...)', 'Box<T...>', 'F(std::decay_t<T>)'):
        text = primary + 'template<class T> struct X<' + arg + '> { void f() { target(); } };'
        assert source_lock.locate(text, 'X::f', 'target();') == 1
    for text in ('struct X<int> { void f() {} };',
                 'struct X; struct X<int> { void f() {} };',
                 primary + 'void g() { struct X<int> { void f() {} }; }',
                 primary + 'struct X<N << 1> { void f() {} };',
                 primary + 'struct X<int>> { void f() {} };'):
        refused(lambda: source_lock.analyze(text))
    for expr in ('requires { result.__values; }',
                 '!requires(Env& e) { N::get(e); } && requires(const decltype(wrapped)& w) { N::get(N::env(w)); }'):
        text = 'void A::f() { if constexpr (' + expr + ') { target(); } }'
        assert source_lock.locate(text, 'A::f', 'target();') == 1
    for expr in ('requires { typename T::type; }', 'requires { { x() } -> C; }',
                 'requires { requires C<T>; }', 'requires { x }', 'requires {}',
                 'requires { x(); } + 1', 'requires(int n = 1) { f(n); }'):
        refused(lambda: source_lock.analyze('void f() { if constexpr (' + expr + ') {} }'))
    text = 'void A::f() { return Box<int, decltype([](int* p) noexcept { hidden(); })>{}; target(); }'
    assert source_lock.locate(text, 'A::f', 'target();') == 1
    refused(lambda: source_lock.locate(text, 'A::f', 'hidden();'))
    for suffix in ('static', 'requires C<int>', '[[nodiscard]]'):
        refused(lambda: source_lock.analyze('void f() { Box<decltype([]() ' + suffix + ' {})> x; }'))


def qualified_template_tests():
    text = 'template<class S, class R> struct C { void f(); }; template<class S, class R> void C<S, R>::f() { target(); }'
    assert source_lock.locate(text, 'C::f', 'target();') == 1
    refused(lambda: source_lock.locate(text, 'D::C::f', 'target();'))
    for qualifier in ('C<int>>', 'C<N << 1>', 'C<int>::'):
        refused(lambda: source_lock.locate('void ' + qualifier + '::f() { target(); }', 'C::f', 'target();'))
