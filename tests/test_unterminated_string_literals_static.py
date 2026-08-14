from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def count_unescaped_quotes(line: str) -> int:
    count = 0
    escaped = False
    for ch in line:
        if escaped:
            escaped = False
            continue
        if ch == "\\":
            escaped = True
            continue
        if ch == '"':
            count += 1
    return count


class UnterminatedStringLiteralsStaticTest(unittest.TestCase):
    def test_source_text_does_not_contain_replacement_characters(self):
        offenders = []
        replacement = "\ufffd"
        for path in sorted((ROOT / "src").glob("*")):
            if path.suffix not in {".cpp", ".h", ".hpp"}:
                continue
            text = path.read_text(encoding="utf-8-sig", errors="replace")
            for line_no, line in enumerate(text.splitlines(), 1):
                if replacement in line:
                    offenders.append(f"{path.relative_to(ROOT)}:{line_no}: {line}")

        self.assertEqual([], offenders)

    def test_source_lines_do_not_have_unterminated_string_literals(self):
        offenders = []
        for path in sorted((ROOT / "src").glob("*")):
            if path.suffix not in {".cpp", ".h", ".hpp"}:
                continue
            text = path.read_text(encoding="utf-8-sig", errors="replace")
            in_raw_string = False
            for line_no, line in enumerate(text.splitlines(), 1):
                if in_raw_string:
                    if ')"' in line:
                        in_raw_string = False
                    continue
                if 'R"(' in line:
                    if ')"' not in line:
                        in_raw_string = True
                    continue
                if count_unescaped_quotes(line) % 2:
                    offenders.append(f"{path.relative_to(ROOT)}:{line_no}: {line}")

        self.assertEqual([], offenders)


if __name__ == "__main__":
    unittest.main()
