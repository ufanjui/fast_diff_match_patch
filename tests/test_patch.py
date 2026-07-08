from __future__ import unicode_literals

import unittest

import fast_diff_match_patch


class PatchApplyTests(unittest.TestCase):
    def test_apply_unicode(self):
        text1 = "The quick brown fox jumps over the lazy dog."
        text2 = "The quick brown cat jumps over the lazy dog."

        patch_text = fast_diff_match_patch.diff(text1, text2, as_patch=True)
        result, applied = fast_diff_match_patch.patch_apply(patch_text, text1)

        self.assertEqual(result, text2)
        self.assertEqual(applied, [True])

    def test_apply_bytes(self):
        text1 = b"The quick brown fox jumps over the lazy dog."
        text2 = b"The quick brown cat jumps over the lazy dog."

        patch_text = fast_diff_match_patch.diff(text1, text2, as_patch=True)
        result, applied = fast_diff_match_patch.patch_apply(patch_text, text1)

        self.assertEqual(result, text2)
        self.assertEqual(applied, [True])

    def test_apply_no_match(self):
        patch_text = fast_diff_match_patch.diff(
            "hello world", "hello there", as_patch=True)
        result, applied = fast_diff_match_patch.patch_apply(
            patch_text, "something completely different")

        self.assertEqual(result, "something completely different")
        self.assertEqual(applied, [False])

    def test_apply_empty_to_text(self):
        patch_text = fast_diff_match_patch.diff("", "hello", as_patch=True)
        result, applied = fast_diff_match_patch.patch_apply(patch_text, "")

        self.assertEqual(result, "hello")
        self.assertEqual(applied, [True])

    def test_apply_roundtrip(self):
        cases = [
            ("", ""),
            ("hello", "hello"),
            ("hello", "world"),
            ("The quick brown fox", "The quick brown cat"),
            ("Line 1\nLine 2\nLine 3", "Line 1\nLine 3\nLine 2"),
            ('你好世界', '你好Python世界'),
        ]

        for text1, text2 in cases:
            with self.subTest(text1=text1, text2=text2):
                patch_text = fast_diff_match_patch.diff(
                    text1, text2, as_patch=True)
                result, applied = fast_diff_match_patch.patch_apply(
                    patch_text, text1)
                self.assertEqual(result, text2)
                self.assertTrue(all(applied))

    def test_apply_direct_string(self):
        """patch_apply with a patch string directly (not from diff)."""
        patch = '@@ -10,7 +10,7 @@\n abc\n-def\n+ghi\n jkl\n'
        result, applied = fast_diff_match_patch.patch_apply(
            patch, '123456789abcdefjkl')
        self.assertEqual(result, '123456789abcghijkl')
        self.assertEqual(applied, [True])

    def test_apply_bytes_direct_string(self):
        patch = b'@@ -10,7 +10,7 @@\n abc\n-def\n+ghi\n jkl\n'
        result, applied = fast_diff_match_patch.patch_apply(
            patch, b'123456789abcdefjkl')
        self.assertEqual(result, b'123456789abcghijkl')
        self.assertEqual(applied, [True])


class PatchFromTextTests(unittest.TestCase):
    def test_fromText_bytes(self):
        patch_bytes = b'@@ -10,7 +10,7 @@\n abc\n-def\n+ghi\n jkl\n'
        patches = fast_diff_match_patch.patch_fromText(patch_bytes)

        self.assertEqual(len(patches), 1)
        p = patches[0]
        self.assertEqual(p['start1'], 9)   # 1-based 10 -> 0-based 9
        self.assertEqual(p['length1'], 7)
        self.assertEqual(p['start2'], 9)
        self.assertEqual(p['length2'], 7)
        self.assertEqual(p['diffs'], [
            ('=', b'abc'),
            ('-', b'def'),
            ('+', b'ghi'),
            ('=', b'jkl'),
        ])

    def test_fromText_diffs_content_unicode(self):
        """Unicode patch_fromText diffs are populated (start values have upstream bug)."""
        text1 = "The quick brown fox"
        text2 = "The quick brown cat"
        patch_text = fast_diff_match_patch.diff(text1, text2, as_patch=True)
        patches = fast_diff_match_patch.patch_fromText(patch_text)

        self.assertEqual(len(patches), 1)
        self.assertGreater(len(patches[0]['diffs']), 0)

    def test_fromText_roundtrip_bytes(self):
        """patch_fromText -> patch_apply should work."""
        text1 = b"hello world"
        text2 = b"hello there"
        patch_text = fast_diff_match_patch.diff(text1, text2, as_patch=True)

        # Parse and re-apply
        patches = fast_diff_match_patch.patch_fromText(patch_text)
        self.assertEqual(len(patches), 1)

        result, applied = fast_diff_match_patch.patch_apply(patch_text, text1)
        self.assertEqual(result, text2)
        self.assertTrue(all(applied))


class DiffLinesTests(unittest.TestCase):
    def test_one_line_modified(self):
        text1 = "hello world\nthis is a test\nfoo bar\nend\n"
        text2 = "hello world\nthis is MODIFIED\nfoo bar\nend\n"

        diffs = fast_diff_match_patch.diff_lines(text1, text2)

        self.assertEqual(diffs, [
            ('=', 'hello world\n'),
            ('-', 'this is a test\n'),
            ('+', 'this is MODIFIED\n'),
            ('=', 'foo bar\nend\n'),
        ])

    def test_one_line_inserted(self):
        text1 = "hello world\nthis is a test\nfoo bar\nend\n"
        text2 = "hello world\nthis is a test\nINSERTED\nfoo bar\nend\n"

        diffs = fast_diff_match_patch.diff_lines(text1, text2)

        self.assertEqual(diffs, [
            ('=', 'hello world\nthis is a test\n'),
            ('+', 'INSERTED\n'),
            ('=', 'foo bar\nend\n'),
        ])

    def test_one_line_deleted(self):
        text1 = "hello world\nthis is a test\nfoo bar\nend\n"
        text2 = "hello world\nfoo bar\nend\n"

        diffs = fast_diff_match_patch.diff_lines(text1, text2)

        self.assertEqual(diffs, [
            ('=', 'hello world\n'),
            ('-', 'this is a test\n'),
            ('=', 'foo bar\nend\n'),
        ])

    def test_empty_to_content(self):
        diffs = fast_diff_match_patch.diff_lines("", "hello\nworld\n")
        self.assertEqual(diffs, [
            ('+', 'hello\nworld\n'),
        ])

    def test_content_to_empty(self):
        diffs = fast_diff_match_patch.diff_lines("hello\nworld\n", "")
        self.assertEqual(diffs, [
            ('-', 'hello\nworld\n'),
        ])

    def test_identical(self):
        text = "line1\nline2\nline3\n"
        diffs = fast_diff_match_patch.diff_lines(text, text)
        self.assertEqual(diffs, [('=', 'line1\nline2\nline3\n')])

    def test_bytes(self):
        text1 = b"hello\nworld\n"
        text2 = b"hello\npython\n"

        diffs = fast_diff_match_patch.diff_lines(text1, text2)

        self.assertEqual(diffs, [
            ('=', b'hello\n'),
            ('-', b'world\n'),
            ('+', b'python\n'),
        ])

    def test_no_trailing_newline(self):
        text1 = "line1\nline2"
        text2 = "line1\nline3"

        diffs = fast_diff_match_patch.diff_lines(text1, text2)

        self.assertEqual(diffs, [
            ('=', 'line1\n'),
            ('-', 'line2'),
            ('+', 'line3'),
        ])


if __name__ == '__main__':
    unittest.main()
