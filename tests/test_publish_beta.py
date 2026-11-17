"""Test publication refusals without credentials or network access."""
import copy
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import publish_beta as publish
from beta_release import BetaError
class PublicationOperations(unittest.TestCase):
    def test_existing_public_or_different_source_is_never_overwritten(self):
        manifest={'tag':'v0.4.0-beta.1','payload':{'source':{'revision':'a'*40}}}
        draft={'tag_name':manifest['tag'],'target_commitish':'a'*40,'draft':True,'prerelease':True}
        publish.existing_draft(draft,manifest)
        for key,value in [('draft',False),('prerelease',False),('target_commitish','b'*40),('tag_name','v0.4.0-beta.2')]:
            changed=copy.deepcopy(draft);changed[key]=value
            with self.subTest(key=key),self.assertRaises(BetaError):publish.existing_draft(changed,manifest)
    def test_source_push_cannot_publicly_publish(self):
        with patch.dict(os.environ,{'GITHUB_REPOSITORY':'Julesc013/wtop','GITHUB_REF':'refs/heads/master','GITHUB_EVENT_NAME':'push'},clear=True),patch.object(publish,'publish') as action:
            self.assertEqual(publish.main(['publish','v0.4.0-beta.1']),2);action.assert_not_called()
    def test_fork_or_work_branch_cannot_stage(self):
        for repo,ref in [('other/wtop','refs/heads/master'),('Julesc013/wtop','refs/heads/work/test')]:
            with patch.dict(os.environ,{'GITHUB_REPOSITORY':repo,'GITHUB_REF':ref},clear=True),patch.object(publish,'stage') as action:
                self.assertEqual(publish.main(['stage','unused']),2);action.assert_not_called()
    def test_invalid_tag_never_calls_network(self):
        for tag in ['v0.4.0','v0.4.0-beta.01','--anything','v0.4.0-beta.1;echo bad']:
            with patch.object(publish,'release_for') as network,self.assertRaises(BetaError):publish.publish(tag)
            network.assert_not_called()
    def test_paginated_release_lookup(self):
        pages=[[{'tag_name':'other','id':i} for i in range(100)],[{'tag_name':'v0.4.0-beta.1','id':123}]]
        with patch.object(publish,'api',side_effect=pages) as api:
            self.assertEqual(publish.release_for('v0.4.0-beta.1')['id'],123)
            self.assertEqual(api.call_count,2)
if __name__=='__main__':unittest.main()
