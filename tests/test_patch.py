from __future__ import unicode_literals

import unittest

import fast_diff_match_patch

class PatchTests(unittest.TestCase):
    def test_patch_apply_unicode(self):
        text1 = "The quick brown fox jumps over the lazy dog."
        text2 = "The quick brown cat jumps over the lazy dog."

        patch_text = fast_diff_match_patch.diff(text1, text2, as_patch=True)
        result, applied = fast_diff_match_patch.patch_apply(patch_text, text1)

        self.assertEqual(result, text2)
        self.assertEqual(applied, [True])

    def test_patch_apply_bytes(self):
        text1 = b"The quick brown fox jumps over the lazy dog."
        text2 = b"The quick brown cat jumps over the lazy dog."

        patch_text = fast_diff_match_patch.diff(text1, text2, as_patch=True)
        result, applied = fast_diff_match_patch.patch_apply(patch_text, text1)

        self.assertEqual(result, text2)
        self.assertEqual(applied, [True])

    def test_patch_no_match(self):
        patch_text = fast_diff_match_patch.diff("hello world", "hello there", as_patch=True)
        result, applied = fast_diff_match_patch.patch_apply(patch_text, "something completely different")

        self.assertEqual(result, "something completely different")
        self.assertEqual(applied, [False])

    def test_patch_unicode_chinese(self):
        text1 = 'hello world'
        text2 = 'hello python world'

        patch_text = fast_diff_match_patch.diff(text1, text2, as_patch=True)
        result, applied = fast_diff_match_patch.patch_apply(patch_text, text1)

        self.assertEqual(result, text2)
        self.assertEqual(applied, [True])

    def test_patch_empty(self):
        text1 = ""
        text2 = "hello"

        patch_text = fast_diff_match_patch.diff(text1, text2, as_patch=True)
        result, applied = fast_diff_match_patch.patch_apply(patch_text, text1)

        self.assertEqual(result, text2)
        self.assertEqual(applied, [True])

    def test_patch_roundtrip(self):
        # Generate patch from text1 -> text2, apply to text1, should get text2 back
        cases = [
            ("", ""),
            ("hello", "hello"),
            ("hello", "world"),
            ("The quick brown fox", "The quick brown cat"),
            ("Line 1\nLine 2\nLine 3", "Line 1\nLine 3\nLine 2"),
        ]

        for text1, text2 in cases:
            with self.subTest(text1=text1, text2=text2):
                patch_text = fast_diff_match_patch.diff(text1, text2, as_patch=True)
                result, applied = fast_diff_match_patch.patch_apply(patch_text, text1)
                self.assertEqual(result, text2)
                self.assertTrue(all(applied))
