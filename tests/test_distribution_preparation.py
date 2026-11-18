"""Offline channel metadata contracts; no client is installed or feed contacted."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import prepare_distribution as preparation
from package_candidate import sha256
from release_bundle import profile_names


def fixture():
    profiles=[]; assets=[]
    for architecture in ('x86','x64','arm64'):
        profile='win-nt-'+architecture
        payload={'version':'0.4.0-beta.3','profile':profile,'source':{'revision':'a'*40},
                 'pe':{'architecture':architecture},'executable':{'sha256':sha256(architecture.encode())}}
        profiles.append({'payload':payload})
        assets.append({'name':profile_names('0.4.0-beta.3',profile)['portable'],
                       'sha256':sha256(('zip-'+architecture).encode()),'size':100})
    return {'schema':'wtop.beta/2','version':'0.4.0-beta.3','payload':profiles[1]['payload'],
            'profiles':profiles,'assets':assets}


class DistributionPreparation(unittest.TestCase):
    def test_exact_architectures_hashes_scopes_and_aliases(self):
        manifest=fixture(); output=preparation.documents(manifest)
        raw=output['winget/Julesc013.Wtop.installer.yaml'].decode()
        installer=json.loads(raw.split('\n',1)[1])
        self.assertEqual(installer['PackageVersion'],'0.4.0-beta.3')
        self.assertEqual(installer['NestedInstallerFiles'],[{'RelativeFilePath':'wtop.exe','PortableCommandAlias':'wtop'}])
        self.assertEqual({(item['Architecture'],item['Scope']) for item in installer['Installers']},
                         {(arch,scope) for arch in ('x86','x64','arm64') for scope in ('user','machine')})
        for item in installer['Installers']:
            archive=next(a for a in manifest['assets'] if a['name']==item['InstallerUrl'].rsplit('/',1)[1])
            self.assertEqual(item['InstallerSha256'],archive['sha256'].upper())
        scoop=json.loads(output['scoop/wtop.json'])
        self.assertEqual(set(scoop['architecture']),{'32bit','64bit','arm64'})
        self.assertEqual(scoop['bin'],'wtop.exe')
        self.assertNotIn('env_add_path',scoop)
        self.assertNotIn('persist',scoop)
        package=ET.fromstring(output['chocolatey/wtop.portable.nuspec'])
        self.assertEqual(package.findtext('metadata/id'),'wtop.portable')
        self.assertEqual(package.findtext('metadata/version'),'0.4.0-beta0003')
        self.assertIn('Gian Sass',package.findtext('metadata/authors'))
        self.assertEqual(json.loads(output['preparation.json'])['publication'],'not_requested_not_performed')

    def test_chocolatey_mapping_orders_prerelease_ordinals(self):
        versions=['0.4.0-beta.1','0.4.0-beta.2','0.4.0-beta.9','0.4.0-beta.10','0.4.0-beta.199']
        mapped=[preparation.chocolatey_version(v) for v in versions]
        self.assertEqual(mapped,sorted(mapped))
        self.assertEqual(preparation.chocolatey_version('0.4.0'),'0.4.0')
        with self.assertRaises(ValueError):preparation.chocolatey_version('0.4.0-beta.03')

    def test_changed_generated_bytes_or_extra_file_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); output=root/'prepared'
            with patch.object(preparation,'verify_release',return_value=fixture()):
                preparation.prepare(root/'synthetic-bundle',output)
                preparation.verify(root/'synthetic-bundle',output)
                with self.assertRaises(ValueError):preparation.prepare(root/'synthetic-bundle',output)
                path=output/'scoop/wtop.json'; original=path.read_bytes()
                changed=json.loads(original); changed['architecture']['64bit']['hash']='0'*64
                path.write_text(json.dumps(changed))
                with self.assertRaisesRegex(ValueError,'differs'):preparation.verify(root/'synthetic-bundle',output)
                path.write_bytes(original); (output/'unexpected.ps1').write_text('unexpected')
                with self.assertRaisesRegex(ValueError,'differs'):preparation.verify(root/'synthetic-bundle',output)


if __name__=='__main__':unittest.main()
