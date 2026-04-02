"""Unit tests for no_forward_decl cppcheck addon."""

import sys
import os

# Make the addon importable without cppcheckdata by mocking it before import.
import types

sys.modules["cppcheckdata"] = types.ModuleType("cppcheckdata")

sys.path.insert(0, os.path.dirname(__file__))
from no_forward_decl import find_forward_declarations  # noqa: E402


# ---------------------------------------------------------------------------
# Mock token infrastructure
# ---------------------------------------------------------------------------


class MockToken:
    def __init__(self, s, file="test.h", linenr=1, column=1):
        self.str = s
        self.file = file
        self.linenr = linenr
        self.column = column
        self.next = None
        self.previous = None


def make_tokens(strings, file="test.h"):
    """Build a linked list of MockTokens from a list of strings.

    Returns the list of all tokens (for passing to find_forward_declarations).
    Each token gets an incrementing line number.
    """
    tokens = []
    for index, s in enumerate(strings):
        tokens.append(MockToken(s, file=file, linenr=index + 1, column=1))
    for index in range(len(tokens)):
        if index > 0:
            tokens[index].previous = tokens[index - 1]
        if index < len(tokens) - 1:
            tokens[index].next = tokens[index + 1]
    return tokens


def make_tokens_multi(file_token_pairs):
    """Build a linked list of MockTokens from multiple files.

    file_token_pairs: list of (file, [str, ...]) tuples.
    Returns a flat list of all tokens with correct file attributes and linking.
    """
    all_tokens = []
    for file, strings in file_token_pairs:
        for s in strings:
            all_tokens.append(
                MockToken(s, file=file, linenr=len(all_tokens) + 1, column=1)
            )
    for index in range(len(all_tokens)):
        if index > 0:
            all_tokens[index].previous = all_tokens[index - 1]
        if index < len(all_tokens) - 1:
            all_tokens[index].next = all_tokens[index + 1]
    return all_tokens


# ===========================================================================
# Positive tests — should detect
# ===========================================================================


# --- struct ---


def test_bare_struct_forward_decl():
    tokens = make_tokens(["struct", "Foo", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 1
    assert result[0].type_name == "Foo"
    assert result[0].type_keyword == "struct"
    assert result[0].is_typedef is False


def test_typedef_struct_forward_decl():
    tokens = make_tokens(["typedef", "struct", "Foo", "Foo", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 1
    assert result[0].type_name == "Foo"
    assert result[0].is_typedef is True


def test_typedef_struct_different_names():
    tokens = make_tokens(["typedef", "struct", "Foo", "Bar", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 1
    assert result[0].type_name == "Foo"
    assert result[0].is_typedef is True


def test_multiple_forward_decls():
    tokens = make_tokens(["struct", "Foo", ";", "struct", "Bar", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 2
    names = {r.type_name for r in result}
    assert names == {"Foo", "Bar"}


# --- union / enum ---


def test_bare_union_forward_decl():
    tokens = make_tokens(["union", "Foo", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 1
    assert result[0].type_keyword == "union"


def test_bare_enum_forward_decl():
    tokens = make_tokens(["enum", "Foo", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 1
    assert result[0].type_keyword == "enum"


def test_typedef_union_forward_decl():
    tokens = make_tokens(["typedef", "union", "Foo", "Foo", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 1
    assert result[0].type_keyword == "union"
    assert result[0].is_typedef is True


def test_typedef_enum_forward_decl():
    tokens = make_tokens(["typedef", "enum", "Foo", "Foo", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 1
    assert result[0].type_keyword == "enum"
    assert result[0].is_typedef is True


# --- .c files ---


def test_forward_decl_in_c_file():
    tokens = make_tokens(["struct", "Foo", ";"], file="test.c")
    result = find_forward_declarations(tokens)
    assert len(result) == 1
    assert result[0].file == "test.c"


# ===========================================================================
# Negative tests — should NOT detect
# ===========================================================================


def test_struct_definition_not_flagged():
    tokens = make_tokens(["struct", "Foo", "{", "int", "x", ";", "}", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 0


def test_union_definition_not_flagged():
    tokens = make_tokens(["union", "Foo", "{", "int", "x", ";", "}", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 0


def test_enum_definition_not_flagged():
    tokens = make_tokens(["enum", "Foo", "{", "A", ",", "B", "}", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 0


def test_anonymous_struct_not_flagged():
    tokens = make_tokens(
        ["typedef", "struct", "{", "int", "x", ";", "}", "Foo", ";"]
    )
    result = find_forward_declarations(tokens)
    assert len(result) == 0


def test_self_referential_typedef_not_flagged():
    tokens = make_tokens(
        ["typedef", "struct", "AN", "AN", ";", "struct", "AN", "{", "int", "x", ";", "}", ";"]
    )
    result = find_forward_declarations(tokens)
    assert len(result) == 0


def test_forward_then_define_same_file():
    tokens = make_tokens(
        ["struct", "Foo", ";", "struct", "Foo", "{", "int", "x", ";", "}", ";"]
    )
    result = find_forward_declarations(tokens)
    assert len(result) == 0


def test_define_then_forward_same_file():
    tokens = make_tokens(
        ["struct", "Foo", "{", "int", "x", ";", "}", ";", "struct", "Foo", ";"]
    )
    result = find_forward_declarations(tokens)
    assert len(result) == 0


def test_struct_pointer_in_param_not_flagged():
    tokens = make_tokens(
        ["void", "func", "(", "struct", "Foo", "*", "ptr", ")", ";"]
    )
    result = find_forward_declarations(tokens)
    assert len(result) == 0


def test_struct_pointer_in_body_not_flagged():
    tokens = make_tokens(
        ["struct", "Bar", "{", "struct", "Foo", "*", "ptr", ";", "}", ";"]
    )
    # Bar is defined (has body), Foo is used as pointer (not fwd decl)
    result = find_forward_declarations(tokens)
    assert len(result) == 0


def test_const_struct_pointer_not_flagged():
    tokens = make_tokens(["const", "struct", "Foo", "*", "ptr", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 0


def test_struct_variable_decl_not_flagged():
    # struct Foo foo ; — Foo is defined in same file
    tokens = make_tokens(
        ["struct", "Foo", "{", "int", "x", ";", "}", ";", "struct", "Foo", "foo", ";"]
    )
    result = find_forward_declarations(tokens)
    assert len(result) == 0


def test_nested_struct_def_not_confused():
    tokens = make_tokens(
        ["struct", "Outer", "{", "struct", "Inner", "{", "int", "x", ";", "}", ";", "}", ";"]
    )
    result = find_forward_declarations(tokens)
    assert len(result) == 0


def test_typedef_definition_not_flagged():
    tokens = make_tokens(
        ["typedef", "struct", "Foo", "{", "int", "x", ";", "}", "Foo", ";"]
    )
    result = find_forward_declarations(tokens)
    assert len(result) == 0


# ===========================================================================
# Multi-file — per-file grouping
# ===========================================================================


def test_decl_in_file_a_def_in_file_b_still_flagged():
    tokens = make_tokens_multi(
        [
            ("a.h", ["struct", "Foo", ";"]),
            ("b.h", ["struct", "Foo", "{", "int", "x", ";", "}", ";"]),
        ]
    )
    result = find_forward_declarations(tokens)
    assert len(result) == 1
    assert result[0].file == "a.h"
    assert result[0].type_name == "Foo"


def test_decl_and_def_same_file_not_flagged():
    tokens = make_tokens_multi(
        [
            ("a.h", ["struct", "Foo", ";", "struct", "Foo", "{", "int", "x", ";", "}", ";"]),
        ]
    )
    result = find_forward_declarations(tokens)
    assert len(result) == 0


# ===========================================================================
# Edge cases
# ===========================================================================


def test_empty_token_list():
    result = find_forward_declarations([])
    assert len(result) == 0


def test_no_forward_decls():
    tokens = make_tokens(
        ["struct", "Foo", "{", "int", "x", ";", "}", ";"]
    )
    result = find_forward_declarations(tokens)
    assert len(result) == 0


# ===========================================================================
# Metadata
# ===========================================================================


def test_violation_has_correct_type_keyword():
    tokens = make_tokens_multi(
        [
            ("a.h", ["struct", "S", ";"]),
            ("b.h", ["union", "U", ";"]),
            ("c.h", ["enum", "E", ";"]),
        ]
    )
    result = find_forward_declarations(tokens)
    by_name = {r.type_name: r for r in result}
    assert by_name["S"].type_keyword == "struct"
    assert by_name["U"].type_keyword == "union"
    assert by_name["E"].type_keyword == "enum"


def test_violation_has_correct_line_number():
    tokens = make_tokens(["struct", "Foo", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 1
    assert result[0].linenr == 1  # the struct keyword is on line 1


def test_typedef_violation_marked_as_typedef():
    tokens = make_tokens(["typedef", "struct", "Foo", "Bar", ";"])
    result = find_forward_declarations(tokens)
    assert len(result) == 1
    assert result[0].is_typedef is True

    tokens2 = make_tokens(["struct", "Foo", ";"])
    result2 = find_forward_declarations(tokens2)
    assert len(result2) == 1
    assert result2[0].is_typedef is False
