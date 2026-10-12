#!/usr/bin/env python3
"""Fail closed unless every failed JUnit test has only a startup diagnostic."""

import re
import sys
import xml.etree.ElementTree as ET


def isolated_mapping_output(kind, output):
    lines = output.strip().splitlines()
    if kind == "tsan":
        return len(lines) == 1 and re.fullmatch(
            r"FATAL: ThreadSanitizer: unexpected memory mapping(?: 0x[0-9a-fA-F]+-0x[0-9a-fA-F]+)?",
            lines[0],
        ) is not None
    lines = [re.sub(r"^==\d+==", "", line) for line in lines]
    marker = "Shadow memory range interleaves with an existing memory mapping"
    if not lines or lines[0] not in (
        marker,
        marker + ". ASan cannot proceed correctly. ABORTING.",
    ):
        return False
    if len(lines) == 1:
        return True
    preamble = [
        r"ASan shadow was supposed to be located in the \[0x[0-9a-fA-F]+-0x[0-9a-fA-F]+\] range\.",
        r"This might be related to ELF_ET_DYN_BASE change in Linux 4\.12\.",
        r"See https://github\.com/google/sanitizers/issues/856 for possible workarounds\.",
        r"Process memory map follows:",
    ]
    if len(lines) < 7 or lines[-1] != "End of process memory map.":
        return False
    return all(re.fullmatch(pattern, line) for pattern, line in zip(preamble, lines[1:5])) and all(
        re.fullmatch(r"\s*0x[0-9a-fA-F]+-0x[0-9a-fA-F]+\s*[^\n]*", line)
        for line in lines[5:-1]
    )


def eligible(kind, path):
    root = ET.parse(path).getroot()
    if root.tag != "testsuite" or int(root.get("errors", "0")) != 0:
        return False
    tests = root.findall("testcase")
    if len(tests) != int(root.get("tests", "-1")):
        return False
    failures = [test for test in tests if test.find("failure") is not None]
    if not failures or len(failures) != int(root.get("failures", "-1")):
        return False
    if any(test.find("error") is not None for test in tests):
        return False
    return all(isolated_mapping_output(kind, test.findtext("system-out", "")) for test in failures)


try:
    sys.exit(0 if eligible(sys.argv[1], sys.argv[2]) else 1)
except (OSError, ET.ParseError, ValueError, IndexError):
    sys.exit(1)
