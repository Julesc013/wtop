"""Host classification is a policy fixture, not native execution evidence."""
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from desktop_acceptance import supported_host
class HostPolicy(unittest.TestCase):
    def test_exact_desktop_boundary(self):
        good = dict(system="Windows", major=10, build=26100, product_type=1,
                    native_architecture="x64", elevated=False)
        self.assertTrue(supported_host(good))
        for key,value in (("product_type",3),("elevated",True),("elevated",None),
                          ("build",19045),("build",True),("native_architecture","arm64"),
                          ("system","Linux"),("major",11)):
            bad=copy.deepcopy(good);bad[key]=value
            with self.subTest(key=key,value=value): self.assertFalse(supported_host(bad))
        self.assertFalse(supported_host({}))
if __name__ == "__main__": unittest.main()
