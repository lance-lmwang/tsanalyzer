import os
import subprocess
import unittest

class TestVerifyScenarios(unittest.TestCase):
    def test_script_exists(self):
        self.assertTrue(os.path.exists("./scripts/tsa_verify_scenarios.py"))

    def test_help_output(self):
        result = subprocess.run(["python3", "./scripts/tsa_verify_scenarios.py", "--help"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0)
        self.assertIn("usage", result.stdout.lower())

if __name__ == "__main__":
    unittest.main()
