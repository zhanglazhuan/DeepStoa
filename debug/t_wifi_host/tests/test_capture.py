import importlib.util
import io
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("capture", Path(__file__).parents[1] / "tools/capture.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CaptureTests(unittest.TestCase):
    def test_pcap_header_timestamp_and_truncation(self):
        output = io.BytesIO()
        count, bad = capture.convert(["I (1) wifi: ready\n", "CAP 4400123 30 2 b008\n"], output)
        self.assertEqual((count, bad), (1, 0))
        data = output.getvalue()
        self.assertEqual(struct.unpack("<IHHIIII", data[:24]),
                         (0xA1B2C3D4, 2, 4, 0, 0, 65535, 105))
        self.assertEqual(struct.unpack("<IIII", data[24:40]), (4, 400123, 2, 30))
        self.assertEqual(data[40:], bytes.fromhex("b008"))

    def test_bad_lengths_and_hex_are_rejected(self):
        for line in ("CAP 1 2 3 aabbcc", "CAP 1 30 3 aabb", "CAP 1 30 1 a",
                     "CAP 1 30 1 zz", "CAP 1 0 0 aa"):
            with self.subTest(line=line), self.assertRaises(ValueError):
                capture.parse_record(line)

    def test_no_log_text_in_pcap(self):
        self.assertIsNone(capture.parse_record("I (0) wifi_host: STATS rx=12"))


if __name__ == "__main__":
    unittest.main()
