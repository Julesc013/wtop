"""Independent publication boundary fixtures. Synthetic receipts never qualify a release."""
import copy
import gzip
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from beta_release import BetaError, CHECKS, REQUIRED_TESTS, archive_tree, desktop_blockers, load_json, native_tests, public_blockers, validate_native
from package_candidate import SEMVER
loader=importlib.util.spec_from_file_location('beta_record_build',ROOT/'packaging/record_build.py')
record=importlib.util.module_from_spec(loader);loader.loader.exec_module(record)

PAYLOAD={'version':'0.4.0-beta.1','profile':'win-nt-x64','source':{'revision':'a'*40},'executable':{'sha256':'b'*64}}
def desktop():
    return {'schema':'wtop.desktop-acceptance/1','result':'passed','version':PAYLOAD['version'],
            'profile':'win-nt-x64','source_revision':'a'*40,'executable_sha256':'b'*64,'automated_smoke':'passed',
            'host':{'system':'Windows','major':10,'build':26100,'product_type':1,'native_architecture':'x64','elevated':False},
            'terminal':'Fixture terminal 1','human_review':{'method':'interactive-attestation','checks':[{'check':c,'passed':True} for c in CHECKS]}}

class PublicationBoundary(unittest.TestCase):
    def test_beta_version_is_explicit(self):
        self.assertTrue(SEMVER.fullmatch('0.4.0-beta.1'))
        for value in ['0.4.0-beta.0','0.4.0-beta.01','0.4.0-beta.1.exe','0.4.0-stable.1']:
            self.assertFalse(SEMVER.fullmatch(value))
    def test_missing_evidence_and_feedback_block(self):
        result=public_blockers({'payload':PAYLOAD},None,{})
        self.assertIn('desktop_human_receipt_missing',result)
        self.assertIn('live_public_issue_feedback_unavailable',result)
    def test_exact_match_is_not_neighbor_target(self):
        self.assertEqual(desktop_blockers(desktop(),PAYLOAD),[])
        for key,value in [('version','0.4.0-dev.1'),('source_revision','c'*40),('executable_sha256','c'*64),('profile','win-nt-arm64'),('result','failed')]:
            receipt=desktop();receipt[key]=value
            with self.subTest(key=key):self.assertTrue(desktop_blockers(receipt,PAYLOAD))
        for key,value in [('elevated',True),('product_type',3),('build',19045),('native_architecture','arm64')]:
            receipt=desktop();receipt['host'][key]=value
            with self.subTest(key=key):self.assertTrue(desktop_blockers(receipt,PAYLOAD))
    def test_all_real_human_questions_required(self):
        receipt=desktop();receipt['human_review']['checks'].pop()
        self.assertIn('complete_human_review_missing',desktop_blockers(receipt,PAYLOAD))
        receipt=desktop();receipt['human_review']['checks'][0]['passed']=1
        self.assertTrue(desktop_blockers(receipt,PAYLOAD))
        receipt=desktop();receipt['human_review']['checks'][-1]=receipt['human_review']['checks'][0]
        self.assertTrue(desktop_blockers(receipt,PAYLOAD))
    def test_feedback_boolean_and_repository_identity(self):
        good={'full_name':'Julesc013/wtop','has_issues':True,'private':False,'archived':False}
        self.assertEqual(public_blockers({'payload':PAYLOAD},desktop(),good),[])
        for key,value in [('has_issues','true'),('has_issues',False),('full_name','other/wtop'),('archived',True),('private',True)]:
            bad=dict(good);bad[key]=value
            with self.subTest(key=key):self.assertTrue(public_blockers({'payload':PAYLOAD},desktop(),bad))
    def test_native_identity_and_version_are_bound(self):
        good={'schema':'wtop.beta-native/1','result':'passed','version':PAYLOAD['version'],'profile':'win-nt-x64',
              'source_revision':'a'*40,'executable_sha256':'b'*64,'tests':{'status':'passed','names':list(REQUIRED_TESTS),'junit_sha256':'c'*64},
              'embedded_version':{'file':PAYLOAD['version'],'product':PAYLOAD['version']},'version_command':PAYLOAD['version'],
              'host':{'system':'Windows'},'authenticode':'NotSigned'}
        validate_native(good,PAYLOAD)
        for key,value in [('version_command','0.4.0-dev.1'),('authenticode','HashMismatch'),('executable_sha256','f'*64)]:
            bad=copy.deepcopy(good);bad[key]=value
            with self.subTest(key=key),self.assertRaises(BetaError):validate_native(bad,PAYLOAD)
    def test_duplicate_json_cannot_change_gate_meaning(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'input.json';path.write_text('{"result":"failed","result":"passed"}')
            with self.assertRaises(BetaError):load_json(path)

class NativeReceipt(unittest.TestCase):
    def test_junit_requires_every_case_and_no_skips(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'junit.xml'
            root=ET.Element('testsuite')
            for name in REQUIRED_TESTS:ET.SubElement(root,'testcase',name=name,status='run')
            ET.ElementTree(root).write(path)
            self.assertEqual(native_tests(path)['status'],'passed')
            for tag in ('failure','error','skipped'):
                child=ET.SubElement(root[0],tag);ET.ElementTree(root).write(path)
                with self.subTest(tag=tag),self.assertRaises(BetaError):native_tests(path)
                root[0].remove(child)
            root.remove(root[0]);ET.ElementTree(root).write(path)
            with self.assertRaises(BetaError):native_tests(path)

class BuildAndSource(unittest.TestCase):
    def test_observed_sdk_and_conflict(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            self.assertEqual(record.observed_sdk(root,{},{}),('UNVERIFIED',[]))
            (root/'wtop.vcxproj').write_text('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup><WindowsTargetPlatformVersion>10.0.26100.0</WindowsTargetPlatformVersion></PropertyGroup></Project>')
            self.assertEqual(record.observed_sdk(root,{}, {})[0],'10.0.26100.0')
            with self.assertRaises(ValueError):record.observed_sdk(root,{'CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION':'10.0.22621.0'}, {})
    def test_reconstruct_git_tree_without_extracting(self):
        data=subprocess.run(['git','-C',str(ROOT),'archive','--format=tar','--prefix=wtop/','HEAD'],capture_output=True,check=True).stdout
        tree,entries=archive_tree(gzip.compress(data,mtime=0))
        expected=subprocess.run(['git','-C',str(ROOT),'rev-parse','HEAD^{tree}'],capture_output=True,text=True,check=True).stdout.strip()
        self.assertEqual(tree,expected);self.assertIn('LICENSE',entries)
    def test_archive_traversal_and_duplicate_rejected(self):
        for names in [('wtop/../escape',),('wtop/same','wtop/same')]:
            stream=io.BytesIO()
            with tarfile.open(fileobj=stream,mode='w:gz') as archive:
                for name in names:
                    item=tarfile.TarInfo(name);item.size=1;archive.addfile(item,io.BytesIO(b'x'))
            with self.subTest(names=names),self.assertRaises(BetaError):archive_tree(stream.getvalue())

if __name__=='__main__':unittest.main()
