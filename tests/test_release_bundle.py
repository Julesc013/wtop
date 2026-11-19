"""Synthetic protocol fixtures; these tests never manufacture native evidence.

The small PE fixtures are not runnable programs. Their receipts exist only inside
temporary test directories to exercise rejection of mismatched release inputs.
"""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from beta_release import BetaError, REQUIRED_TESTS, native_tests, public_blockers
from package_candidate import package, json_bytes, sha256, source_identity
from release_bundle import assemble, verify, validate_execution, validate_scan, profile_names, release_notes
from release_metadata import metadata, msi_version, asset_label, cmake_text
from test_packaging import fixture

VERSION='0.4.0-beta.2'
REPOSITORY={'full_name':'Julesc013/wtop','has_issues':True,'private':False,'archived':False}


def receipt(payload,junit):
    arch=payload['pe']['architecture']
    return {'schema':'wtop.beta-native/1','result':'passed','version':payload['version'],
            'profile':payload['profile'],'source_revision':payload['source']['revision'],
            'executable_sha256':payload['executable']['sha256'],'tests':native_tests(junit),
            'version_command':payload['version'],'embedded_version':{'file':payload['version'],'product':payload['version']},
            'embedded_numeric_version':metadata(payload['version'])['native_version'], 'authenticode':'NotSigned',
            'human_visual_acceptance':False,
            'host':{'system':'Windows','major':10,'build':26100,'product_type':1,
                    'native_architecture':'x64' if arch=='x86' else arch,'elevated':True}}


def scan_receipt(payload):
    return {'schema':'wtop.security-scan/1','version':payload['version'],'profile':payload['profile'],
            'source_revision':payload['source']['revision'],'executable_sha256':payload['executable']['sha256'],
            'status':'unavailable','reason':'Synthetic fixture; no scanner ran'}


class VersionLedger(unittest.TestCase):
    def test_native_and_msi_channel_ordering(self):
        sequence=['0.4.0-dev.199','0.4.0-alpha.1','0.4.0-alpha.199','0.4.0-beta.1',
                  '0.4.0-beta.2','0.4.0-beta.199','0.4.0-rc.1','0.4.0-rc.199','0.4.0','0.4.1-dev.1']
        native=[metadata(v)['native_version'] for v in sequence]
        windows_installer=[tuple(map(int,msi_version(v).split('.'))) for v in sequence]
        self.assertEqual(native,sorted(native));self.assertEqual(len(set(map(tuple,native))),len(sequence))
        self.assertEqual(windows_installer,sorted(windows_installer));self.assertEqual(len(set(windows_installer)),len(sequence))
        self.assertEqual(msi_version(VERSION),'0.4.402')
        self.assertEqual(metadata(VERSION)['title'],'Wtop 0.4.0 Beta 2')
    def test_bounds_are_refused_not_truncated(self):
        for version in ('0.4.0-beta.01','0.4.0-beta.0','01.4.0','0.4.0-beta.10000','65536.0.0','0.4.0;exit'):
            with self.subTest(version=version),self.assertRaises(ValueError):metadata(version)
        for version in ('256.0.0','1.256.0','0.4.65','0.4.0-beta.200'):
            self.assertIsNone(metadata(version)['msi_version'])
            with self.subTest(version=version),self.assertRaises(ValueError):msi_version(version)
        self.assertEqual(msi_version('255.255.64'),'255.255.64999')
    def test_human_labels_and_resource_generation(self):
        self.assertEqual(asset_label(VERSION,'win-nt-arm64','Portable ZIP'),
                         'Wtop 0.4.0 Beta 2 for Windows NT (ARM64, 64-bit) Portable ZIP')
        self.assertIn('set(WTOP_VERSION_BUILD "20002")',cmake_text(metadata(VERSION)))
        self.assertIn('set(WTOP_VERSION_FLAGS "0")',cmake_text(metadata('0.4.0')))


class BundleBoundary(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.source=self.root/'source';self.source.mkdir()
        for filename in ('.gitattributes','LICENSE','source/wtop.conf','spec/catalog/targets.json'):
            destination=self.source/filename;destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(ROOT/filename,destination)
        (self.source/'VERSION').write_bytes((VERSION+'\n').encode('ascii'))
        for args in (['init','-q'],['-c','core.autocrlf=false','add','.'],
                     ['-c','user.name=Fixture','-c','user.email=fixture@example.invalid','-c','commit.gpgsign=false','commit','-qm','fixture only']):
            subprocess.run(['git','-C',str(self.source),*args],check=True,capture_output=True)
        self.identity=source_identity(self.source)
        self.notes=self.root/'changes.md';self.notes.write_text('Escape unwinds selection. Ctrl-C requests shutdown.\n')

    def candidate(self,arch):
        data=bytearray(fixture(arch))
        directories=0x98+(96 if arch=='x86' else 112)
        # A bounded empty resource directory for PE structure tests only.
        struct.pack_into('<II',data,directories+16,0x12A0,16)
        executable=self.root/(arch+'.exe');executable.write_bytes(data)
        directory=self.root/('input-'+arch)
        provenance={'compiler':'synthetic-fixture','linker':'synthetic-fixture','sdk':'10.0.26100.0',
                    'runtime':'none','flags':['fixture'],'build_command':['fixture']}
        manifest=package(executable,directory,'win-nt-'+arch,provenance,root=self.source,source=self.identity)
        payload=manifest['payload']
        junit=ET.Element('testsuite')
        for name in sorted(REQUIRED_TESTS):ET.SubElement(junit,'testcase',name=name,status='run')
        ET.ElementTree(junit).write(directory/'native-tests.xml')
        (directory/'native-receipt.json').write_bytes(json_bytes(receipt(payload,directory/'native-tests.xml')))
        (directory/'security-scan.json').write_bytes(json_bytes(scan_receipt(payload)))
        return directory

    def assemble(self,architectures=('x64',)):
        candidates=[self.candidate(arch) for arch in architectures]
        output=self.root/'bundle'
        manifest=assemble(candidates,self.notes,output,root=self.source)
        return output,manifest

    def refresh_outer_hashes(self,directory,manifest):
        for item in manifest['assets']:
            body=(directory/item['name']).read_bytes();item.update(size=len(body),sha256=sha256(body))
        (directory/'beta-manifest.json').write_bytes(json_bytes(manifest))
        names=[i['name'] for i in manifest['assets']]+['beta-manifest.json']
        (directory/'SHA256SUMS').write_text(''.join(f'{sha256((directory/name).read_bytes())}  {name}\n' for name in sorted(names)),newline='\n')

    def test_three_profile_bundle_and_friendly_notes(self):
        output,manifest=self.assemble(('x86','x64','arm64'))
        self.assertEqual(verify(output),manifest)
        self.assertEqual(manifest['omitted_profiles'],[])
        self.assertEqual(len(manifest['assets']),21)
        notes=(output/'RELEASE-NOTES.md').read_text()
        for heading in ('Downloads','Changes','Known limitations','Verification'):
            self.assertEqual(notes.count('## '+heading),1)
        self.assertIn('ARM64, 64-bit',notes)
        self.assertNotIn('.msi)',notes)
        self.assertIn('elevated token true',notes)
        self.assertIn('security scan `unavailable`',notes)

    def test_subset_explicitly_omits_other_profiles(self):
        output,manifest=self.assemble()
        self.assertEqual(manifest['omitted_profiles'],['win-nt-arm64','win-nt-x86'])
        with self.assertRaises(BetaError):assemble([self.root/'input-x64'],self.notes,output,root=self.source)

    def test_beta3_carries_exact_third_party_notices_without_changing_beta2(self):
        # Text-mode writes on Windows would produce CRLF while Git stores LF.
        # Keep carrier/source byte identity and the production attributes policy;
        # the verifier must not normalize away a real payload mismatch.
        (self.source/'VERSION').write_bytes(b'0.4.0-beta.3\n')
        (self.source/'THIRD-PARTY-NOTICES.md').write_bytes(b'Synthetic licensed dependency notice for protocol test only.\n')
        subprocess.run(['git','-C',str(self.source),'add','.'],check=True,capture_output=True)
        subprocess.run(['git','-C',str(self.source),'-c','user.name=Fixture','-c','user.email=fixture@example.invalid',
                        '-c','commit.gpgsign=false','commit','-qm','beta3 fixture'],check=True,capture_output=True)
        self.identity=source_identity(self.source)
        output,manifest=self.assemble()
        self.assertEqual(manifest['metadata']['msi_version'],'0.4.403')
        self.assertEqual((output/'THIRD-PARTY-NOTICES.md').read_bytes(),(self.source/'THIRD-PARTY-NOTICES.md').read_bytes())
        self.assertTrue(any('offline preparation only' in text for text in manifest['omitted_deliverables']))
        self.assertEqual(verify(output),manifest)
        (output/'THIRD-PARTY-NOTICES.md').write_bytes(b'changed notice')
        self.refresh_outer_hashes(output,manifest)
        with self.assertRaisesRegex(BetaError,'third-party notices'):verify(output)

    def test_updated_outer_checksums_do_not_hide_changed_exe(self):
        output,manifest=self.assemble()
        name=profile_names(VERSION,'win-nt-x64')['exe']
        (output/name).write_bytes((output/name).read_bytes()+b'changed')
        self.refresh_outer_hashes(output,manifest)
        with self.assertRaisesRegex(BetaError,'PE/provenance'):verify(output)

    def test_receipts_reject_compile_only_neighbor_and_human_fiction(self):
        directory=self.candidate('arm64')
        payload=json.loads((directory/'payload.json').read_text())
        original=json.loads((directory/'native-receipt.json').read_text())
        for field,value in [('native_architecture','x64'),('elevated',None),('build','26100')]:
            changed=copy.deepcopy(original);changed['host'][field]=value
            with self.subTest(field=field),self.assertRaises(BetaError):validate_execution(changed,payload)
        for field,value in [('embedded_numeric_version',[0,4,0,2]),('human_visual_acceptance',True)]:
            changed=copy.deepcopy(original);changed[field]=value
            with self.subTest(field=field),self.assertRaises(BetaError):validate_execution(changed,payload)

    def test_missing_new_input_or_frame_test_blocks(self):
        directory=self.candidate('x64')
        root=ET.parse(directory/'native-tests.xml')
        root.getroot().remove(next(item for item in root.getroot() if item.get('name')=='native_input'))
        root.write(directory/'native-tests.xml')
        with self.assertRaisesRegex(BetaError,'missing or duplicate'):native_tests(directory/'native-tests.xml')

    def test_scan_is_not_inferred_from_unsigned_or_unavailable(self):
        output,manifest=self.assemble()
        payload=manifest['payload'];scan=scan_receipt(payload)
        validate_scan(scan,payload)
        for status in ('clean','detected','failed','passed'):
            changed=dict(scan,status=status)
            with self.subTest(status=status),self.assertRaises(BetaError):validate_scan(changed,payload)
        blockers=public_blockers(manifest,None,REPOSITORY)
        self.assertEqual(blockers,['security_scan_not_clean:win-nt-x64'])
        synthetic_clean=dict(scan,status='clean',engine='Microsoft Defender',signature_version='fixture',
                             detected_threats=0,scan_exit_code=0,method='custom-no-remediation-ignore-exclusions',
                             scan_completed_at='fixture-only-not-evidence')
        validate_scan(synthetic_clean,payload)
        manifest['profiles'][0]['security_scan']=synthetic_clean
        self.assertEqual(public_blockers(manifest,None,REPOSITORY),[])
        self.assertIn('live_public_issue_feedback_unavailable',public_blockers(manifest,None,{}))

    def test_stable_never_uses_beta_review_exception(self):
        _,manifest=self.assemble()
        manifest['version']='0.4.0'
        self.assertIn('desktop_human_receipt_missing',public_blockers(manifest,None,REPOSITORY))


class SecurityProbe(unittest.TestCase):
    def test_scanner_does_not_modify_security_configuration(self):
        spec=importlib.util.spec_from_file_location('scan_tool',ROOT/'packaging/security_scan.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        path=Path("C:/Folder/$('untrusted')")
        with patch.object(module.subprocess,'run',return_value=subprocess.CompletedProcess([],0,'{"status":"unavailable"}','')) as run:
            self.assertEqual(module.scan(path),{'status':'unavailable'})
        args,kwargs=run.call_args
        self.assertNotIn(str(path),args[0][-1]);self.assertEqual(kwargs['env']['WTOP_SCAN_ROOT'],str(path))
        self.assertEqual(kwargs['timeout'],600);self.assertTrue(kwargs['check'])
        for prohibited in ('Set-MpPreference','Add-MpPreference','Set-ExecutionPolicy'):
            self.assertNotIn(prohibited,module.SCAN_SCRIPT)
        self.assertIn('-Scan -ScanType 3 -File $root -DisableRemediation',module.SCAN_SCRIPT)


if __name__=='__main__':unittest.main()
