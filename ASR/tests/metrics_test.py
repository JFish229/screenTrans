import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from benchmark import cer, distribution


class Metrics(unittest.TestCase):
    def test_exact(self):
        self.assertEqual(cer("hello", "hello")["cer"], 0)

    def test_operations(self):
        self.assertEqual(cer("abc", "adc")["substitutions"], 1)
        self.assertEqual(cer("abc", "ac")["deletions"], 1)
        self.assertEqual(cer("abc", "abxc")["insertions"], 1)

    def test_punctuation_only_normalization(self):
        self.assertEqual(cer("Hello, WORLD!", "hello world")["cer"], 0)
        # Do not silently convert traditional/simplified characters.
        self.assertEqual(cer("\u4f1a", "\u6703")["cer"], 1)

    def test_empty(self):
        with self.assertRaises(ValueError):
            cer("", "text")
        self.assertIsNone(distribution([]))

    def test_percentiles(self):
        self.assertEqual(distribution(list(range(1, 21))), {"p50": 10.5, "p95": 19, "count": 20})


if __name__ == "__main__":
    unittest.main()
