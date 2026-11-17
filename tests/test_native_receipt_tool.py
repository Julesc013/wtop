"""Metadata-probe boundary tests; Windows execution is still required in CI."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
spec=importlib.util.spec_from_file_location('native_receipt_probe',ROOT/'packaging/native_receipt.py')
probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)

class MetadataProbe(unittest.TestCase):
    def test_edition_local_modules_and_filename_as_data(self):
        filename="C:\\Review Folder\\$(not-a-command)'wtop.exe"
        value={'file':'0.4.0-beta.1','product':'0.4.0-beta.1','authenticode':'NotSigned'}
        completed=subprocess.CompletedProcess([],0,json.dumps(value),'')
        with patch.object(probe.subprocess,'run',return_value=completed) as run:
            self.assertEqual(probe.read_windows_metadata(filename),value)
        args,kwargs=run.call_args
        self.assertEqual(args[0][:4],['powershell.exe','-NoProfile','-NonInteractive','-Command'])
        script=args[0][4]
        self.assertNotIn(filename,script)
        self.assertIn("$env:PSModulePath=$PSHOME+'\\Modules'",script)
        self.assertIn("$ErrorActionPreference='Stop'",script)
        self.assertNotIn('ExecutionPolicy',script)
        self.assertEqual(kwargs['env']['WTOP_RECEIPT_EXE'],filename)
        self.assertTrue(kwargs['check']);self.assertEqual(kwargs['timeout'],30)
    def test_failed_signature_query_is_not_unsigned_success(self):
        failure=subprocess.CalledProcessError(1,['powershell.exe'],stderr='module load failed')
        with patch.object(probe.subprocess,'run',side_effect=failure):
            with self.assertRaises(subprocess.CalledProcessError):probe.read_windows_metadata('wtop.exe')

if __name__=='__main__':unittest.main()
