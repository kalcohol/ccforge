"""Syntax regression for direct variant-dependent contract-oracle assignments."""
import ast


def check(tree, fields):
    def variant(node):
        return any(isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name)
                   and n.value.id in ('C', 'c') and n.attr in ('mut', 'control') for n in ast.walk(node))

    def protected(node):
        return any(isinstance(n, ast.Attribute) and isinstance(n.ctx, ast.Store)
                   and isinstance(n.value, ast.Name) and n.value.id == 'w'
                   and n.attr in fields for n in ast.walk(node))

    for node in ast.walk(tree):
        if isinstance(node, ast.If) and variant(node.test):
            assert not any(protected(child) for child in node.body+node.orelse), node.lineno
        if isinstance(node, (ast.Assign, ast.AnnAssign, ast.AugAssign, ast.NamedExpr)) and protected(node):
            assert node.value is None or not variant(node.value), node.lineno


def fixtures(fields):
    for field in fields:
        for variant in ('mut', 'control'):
            for source in (
                "if C.%s:\n w.%s = True" % (variant, field),
                "if C.%s:\n pass\nelse:\n w.%s = True" % (variant, field),
                "w.%s = True if C.%s else False" % (field, variant),
                "w.%s: bool = bool(C.%s)" % (field, variant),
                "w.%s += 1 if C.%s else 0" % (field, variant)):
                try:
                    check(ast.parse(source), fields)
                except AssertionError:
                    pass
                else:
                    raise AssertionError(source)
    check(ast.parse('w.expected = C.payload\nif C.control:\n w.discharged = True'), fields)
