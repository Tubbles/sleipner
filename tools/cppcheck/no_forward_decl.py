"""Cppcheck addon: detect cross-module forward declarations.

Flags struct/union/enum forward declarations where the type is not defined
in the same file.  Run via: cppcheck --addon=tools/cppcheck/no_forward_decl.py
"""

import dataclasses
import sys
from collections import defaultdict

TYPE_KEYWORDS = frozenset({"struct", "union", "enum"})


@dataclasses.dataclass
class ForwardDecl:
    type_keyword: str
    type_name: str
    file: str
    linenr: int
    column: int
    is_typedef: bool


def _safe(tok):
    """Return tok if not None, else a sentinel with .str = None."""
    return tok if tok is not None else _SENTINEL


class _Sentinel:
    str = None
    next = None
    previous = None
    file = None
    linenr = 0
    column = 0


_SENTINEL = _Sentinel()


def find_forward_declarations(tokens):
    """Return list of ForwardDecl for cross-module forward declarations.

    Processes all tokens, grouping by file.  A forward declaration is only
    suppressed if the type is defined in the *same* file — ordering within
    the file does not matter.
    """
    definitions = defaultdict(set)

    # Pass 1: collect definitions per file.
    for tok in tokens:
        if tok.str in TYPE_KEYWORDS:
            name_tok = _safe(tok.next)
            brace_tok = _safe(name_tok.next)
            if name_tok.str is not None and brace_tok.str == "{":
                definitions[tok.file].add(name_tok.str)

    # Pass 2: find forward declarations.
    violations = []
    for tok in tokens:
        # Pattern A: struct|union|enum NAME ;
        if tok.str in TYPE_KEYWORDS:
            prev = _safe(tok.previous)
            name_tok = _safe(tok.next)
            semi_tok = _safe(name_tok.next)
            if (
                name_tok.str is not None
                and name_tok.str != "{"
                and semi_tok.str == ";"
                and prev.str != "typedef"
            ):
                if name_tok.str not in definitions.get(tok.file, set()):
                    violations.append(
                        ForwardDecl(
                            type_keyword=tok.str,
                            type_name=name_tok.str,
                            file=tok.file,
                            linenr=tok.linenr,
                            column=tok.column,
                            is_typedef=False,
                        )
                    )

        # Pattern B: typedef struct|union|enum NAME [NAME] ;
        if tok.str == "typedef":
            kw_tok = _safe(tok.next)
            if kw_tok.str not in TYPE_KEYWORDS:
                continue
            name_tok = _safe(kw_tok.next)
            if name_tok.str is None or name_tok.str == "{":
                continue
            after_name = _safe(name_tok.next)
            # 4-token: typedef struct Foo ;
            if after_name.str == ";":
                if name_tok.str not in definitions.get(tok.file, set()):
                    violations.append(
                        ForwardDecl(
                            type_keyword=kw_tok.str,
                            type_name=name_tok.str,
                            file=tok.file,
                            linenr=tok.linenr,
                            column=tok.column,
                            is_typedef=True,
                        )
                    )
            # 5-token: typedef struct Foo Bar ;
            elif after_name.str is not None and after_name.str != "{":
                final = _safe(after_name.next)
                if final.str == ";":
                    if name_tok.str not in definitions.get(tok.file, set()):
                        violations.append(
                            ForwardDecl(
                                type_keyword=kw_tok.str,
                                type_name=name_tok.str,
                                file=tok.file,
                                linenr=tok.linenr,
                                column=tok.column,
                                is_typedef=True,
                            )
                        )

    return violations


import cppcheckdata  # noqa: E402  — provided by cppcheck or PYTHONPATH

if __name__ == "__main__":
    for arg in sys.argv[1:]:
        if not arg.endswith(".dump"):
            continue
        data = cppcheckdata.CppcheckData(arg)
        for violation in find_forward_declarations(data.rawTokens):
            cppcheckdata.reportError(
                violation,
                "warning",
                f"Cross-module forward declaration of"
                f" '{violation.type_keyword} {violation.type_name}'",
                "noForwardDecl",
                "noForwardDecl",
            )
