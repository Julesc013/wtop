"""Secret sentinel fields never enter the diagnostic allowlist."""
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from support_report import safe_capabilities
class DiagnosticPrivacy(unittest.TestCase):
    def test_unknown_and_nested_private_fields_are_dropped(self):
        secret='SECRET_SENTINEL_DO_NOT_SHARE'
        value={'kind':'build-capabilities','runtime_probe':False,'version':'0.4.0-beta.1','profile':'win-nt-x64',
               'schema':1,'snapshot_schema':2,'tui':True,'metrics':['cpu_percent',secret],
               'optional_providers':['services',secret,{'password':secret}],
               'paths':secret,'command_line':secret,'username':secret,'environment':{'TOKEN':secret},
               'gui':secret,'settings_schema':True}
        result=safe_capabilities(value)
        self.assertNotIn(secret,json.dumps(result));self.assertEqual(result['metrics'],['cpu_percent'])
        self.assertEqual(result['optional_providers'],['services']);self.assertNotIn('settings_schema',result)
        self.assertEqual(result['tui'],True)
    def test_process_snapshot_and_runtime_report_are_rejected(self):
        for value in ({'kind':'snapshot'}, {'kind':'build-capabilities','runtime_probe':True},
                      {'kind':'build-capabilities','runtime_probe':False,'version':'private-path','profile':'win-nt-x64'}):
            with self.assertRaises(ValueError):safe_capabilities(value)
if __name__=='__main__':unittest.main()
