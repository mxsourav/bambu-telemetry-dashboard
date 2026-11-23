#!/usr/bin/env python3
"""
run_tests.py
Automated test runner for STΔN studio & Bambu Telemetry Dashboard.
Runs all unit and integration test suites in tests/ with formatted summary.
"""

import sys
import os
import unittest
import time

REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)


def main():
    print("=" * 70)
    print(" STΔN studio - Automated Test Suite Runner")
    print("=" * 70)

    start_time = time.time()
    loader = unittest.TestLoader()
    suite = loader.discover(start_dir=os.path.join(REPO_ROOT, "tests"), pattern="test_*.py")

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    duration = time.time() - start_time

    print("=" * 70)
    print(f" Tests Run:   {result.testsRun}")
    print(f" Errors:      {len(result.errors)}")
    print(f" Failures:    {len(result.failures)}")
    print(f" Time Taken:  {duration:.2f} seconds")
    print("=" * 70)

    if result.wasSuccessful():
        print(" [PASS] All test suites completed successfully!")
        return 0
    else:
        print(" [FAIL] Test suite encountered errors or failures.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
