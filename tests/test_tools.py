"""
Unit tests for STΔN studio developer tools and utility scripts.
"""

import os
import sys
import tempfile
import unittest

# Ensure tools directory is accessible
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS_DIR = os.path.join(REPO_ROOT, "tools")
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from admin_server import build_parser as build_admin_parser
from build_web_ui_header import build_web_ui_header
from gen_pwa_header import generate_pwa_header
from create_delta_icons import generate_icons
from test_push_image import create_rgb565_buffer, build_multipart_payload


class ToolsTestCase(unittest.TestCase):
    def test_admin_server_parser_defaults(self):
        parser = build_admin_parser()
        args = parser.parse_args([])
        self.assertEqual(args.port, 3000)
        self.assertEqual(args.host, "0.0.0.0")
        self.assertIsNone(args.ip)
        self.assertFalse(args.no_browser)
        self.assertFalse(args.no_pna)

    def test_admin_server_parser_custom_args(self):
        parser = build_admin_parser()
        args = parser.parse_args(["--port", "8080", "--ip", "192.168.1.100", "--no-browser", "--no-pna"])
        self.assertEqual(args.port, 8080)
        self.assertEqual(args.ip, "192.168.1.100")
        self.assertTrue(args.no_browser)
        self.assertTrue(args.no_pna)

    def test_build_web_ui_header_check(self):
        # Checks that the current src/web_ui.h matches web/index.html
        is_synced = build_web_ui_header(check_only=True)
        self.assertTrue(is_synced, "src/web_ui.h should be synced with web/index.html")

    def test_gen_pwa_header_dry_run(self):
        # Validates PWA assets in web/ without overwriting src/pwa_assets.h
        res = generate_pwa_header(dry_run=True)
        self.assertTrue(res)

    def test_create_delta_icons_temporary(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            generated_files = generate_icons(output_dir=tmp_dir)
            self.assertEqual(len(generated_files), 6)
            for fpath in generated_files:
                self.assertTrue(os.path.isfile(fpath), f"File {fpath} was not created")
                self.assertGreater(os.path.getsize(fpath), 0)

    def test_rgb565_buffer_dimensions_and_colors(self):
        # 160 x 128 at 2 bytes per pixel = 40,960 bytes
        expected_len = 160 * 128 * 2

        red_buf = create_rgb565_buffer("red")
        self.assertEqual(len(red_buf), expected_len)
        self.assertEqual(red_buf[0], 0xF8)
        self.assertEqual(red_buf[1], 0x00)

        green_buf = create_rgb565_buffer("green")
        self.assertEqual(len(green_buf), expected_len)
        self.assertEqual(green_buf[0], 0x07)
        self.assertEqual(green_buf[1], 0xE0)

        blue_buf = create_rgb565_buffer("blue")
        self.assertEqual(len(blue_buf), expected_len)
        self.assertEqual(blue_buf[0], 0x00)
        self.assertEqual(blue_buf[1], 0x1F)

        grad_buf = create_rgb565_buffer("gradient")
        self.assertEqual(len(grad_buf), expected_len)

    def test_multipart_payload_structure(self):
        buf = bytearray(32)
        payload, boundary = build_multipart_payload(buf, boundary="TEST_BOUNDARY_XYZ")
        self.assertIn(b"--TEST_BOUNDARY_XYZ", payload)
        self.assertIn(b'name="file"; filename="screen.bin"', payload)
        self.assertIn(b"Content-Type: application/octet-stream", payload)
        self.assertTrue(payload.endswith(b"--TEST_BOUNDARY_XYZ--\r\n"))


if __name__ == "__main__":
    unittest.main()
