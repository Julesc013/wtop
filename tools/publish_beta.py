#!/usr/bin/env python3
"""Explicit GitHub draft/publish operations after local exact-byte validation.

Uses the caller's GitHub CLI authentication. Never requests broader permissions,
changes repository policy, overwrites assets or publishes from a normal source
edit. Only the dedicated workflow may stage on master; public promotion requires
an explicit invocation and an actual exact-payload desktop review asset.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import urllib.request
from beta_release import BetaError, VERSION, checked_file, load_json, public_blockers, sha256, verify

REPOSITORY='Julesc013/wtop'

def gh(*args, input_data=None):
    result=subprocess.run(['gh',*args],input=input_data,capture_output=True,text=True,timeout=120)
    if result.returncode:
        raise BetaError('GitHub operation failed ('+str(result.returncode)+'): '+result.stderr[-1000:])
    return result.stdout

def api(path, method='GET', data=None):
    args=['api','--method',method,path]
    if data is not None:args+=['--input','-']
    return json.loads(gh(*args,input_data=json.dumps(data) if data is not None else None))

def release_for(tag):
    for page in range(1,11):
        releases=api(f'repos/{REPOSITORY}/releases?per_page=100&page={page}')
        matches=[item for item in releases if item['tag_name']==tag]
        if len(matches)>1:raise BetaError('ambiguous release identity')
        if matches:return matches[0]
        if len(releases)<100:return None
    raise BetaError('release search exceeded explicit pagination bound')

def asset_inventory(directory, manifest):
    return sorted([x['name'] for x in manifest['assets']]+['beta-manifest.json','SHA256SUMS'])

def existing_draft(release, manifest):
    if release.get('draft') is not True or release.get('prerelease') is not True:
        raise BetaError('existing non-draft or non-prerelease version is immutable to this tool')
    if release.get('tag_name')!=manifest['tag'] or release.get('target_commitish')!=manifest['payload']['source']['revision']:
        raise BetaError('draft belongs to a different exact source; assign a new version')

def assets_for(release):
    result=[]
    for page in range(1,11):
        data=api(f'repos/{REPOSITORY}/releases/{release["id"]}/assets?per_page=100&page={page}')
        result+=data
        if len(data)<100:return result
    raise BetaError('release asset count exceeded bound')

def download(tag, directory, names=None):
    args=['release','download',tag,'--repo',REPOSITORY,'--dir',str(directory)]
    if names:
        for name in names:args+=['--pattern',name]
    gh(*args)

def stage(directory):
    manifest=verify(directory);tag=manifest['tag'];revision=manifest['payload']['source']['revision']
    head=api(f'repos/{REPOSITORY}/git/ref/heads/master')['object']['sha']
    if head!=revision:raise BetaError('master moved since candidate build; staging requires an exact master snapshot')
    release=release_for(tag)
    if release is None:
        notes=checked_file(directory/'RELEASE-NOTES.md',1024*1024).decode('utf-8')
        release=api(f'repos/{REPOSITORY}/releases','POST',
                    {'tag_name':tag,'target_commitish':revision,'name':'Wtop '+manifest['version'],
                     'body':notes,'draft':True,'prerelease':True,'make_latest':'false'})
    existing_draft(release,manifest)
    expected=asset_inventory(directory,manifest)
    assets=assets_for(release)
    names=[a['name'] for a in assets]
    if len(names)!=len(set(names)) or set(names)-set(expected)-{'desktop-receipt.json'}:
        raise BetaError('draft has duplicate or unexpected assets; no automatic deletion')
    # On retry verify existing bytes first. Never --clobber an existing asset.
    with tempfile.TemporaryDirectory(prefix='wtop-draft-check-') as temporary:
        if names:
            download(tag,Path(temporary),names)
            for name in set(names)&set(expected):
                if checked_file(Path(temporary)/name)!=checked_file(directory/name):
                    raise BetaError('draft bytes differ: '+name+'; no replacement under the same version')
    missing=[name for name in expected if name not in names]
    if missing:gh('release','upload',tag,*[str(directory/name) for name in missing],'--repo',REPOSITORY)
    with tempfile.TemporaryDirectory(prefix='wtop-draft-acquire-') as temporary:
        download(tag,Path(temporary),expected)
        acquired=verify(Path(temporary))
        if acquired!=manifest:raise BetaError('acquired draft manifest differs')
    # Staging deliberately succeeds with missing public gates, and reports them.
    blockers=public_blockers(manifest,None,api(f'repos/{REPOSITORY}'))
    print(json.dumps({'state':'draft','url':release['html_url'],'version':manifest['version'],
                      'source':revision,'public_eligible':False,'public_blockers':blockers},indent=2))

def publish(tag):
    if not tag.startswith('v') or not VERSION.fullmatch(tag[1:]):raise BetaError('invalid beta tag')
    release=release_for(tag)
    if release is None:raise BetaError('no staged draft exists')
    with tempfile.TemporaryDirectory(prefix='wtop-promotion-') as temporary:
        directory=Path(temporary)
        download(tag,directory)
        manifest=verify(directory);existing_draft(release,manifest)
        source=manifest['payload']['source']['revision']
        subprocess.run(['git','merge-base','--is-ancestor',source,'HEAD'],check=True,capture_output=True)
        expected=set(asset_inventory(directory,manifest))|{'desktop-receipt.json'}
        assets=assets_for(release)
        if len(assets)!=len(expected) or {x['name'] for x in assets}!=expected:
            raise BetaError('publication requires exact asset set including desktop-receipt.json')
        desktop=load_json(directory/'desktop-receipt.json') if (directory/'desktop-receipt.json').exists() else None
        blockers=public_blockers(manifest,desktop,api(f'repos/{REPOSITORY}'))
        if blockers:raise BetaError('public promotion blocked: '+', '.join(blockers))
        # No rebuilding or resigning after the human receipt. Recheck the draft
        # immediately before changing visibility; this is a permission boundary,
        # not protection against a malicious repository administrator.
        current=api(f'repos/{REPOSITORY}/releases/{release["id"]}')
        existing_draft(current,manifest)
        if {(a['id'],a['name'],a.get('digest'),a['size']) for a in assets} != {(a['id'],a['name'],a.get('digest'),a['size']) for a in assets_for(current)}:
            raise BetaError('release assets changed during qualification')
        result=api(f'repos/{REPOSITORY}/releases/{release["id"]}','PATCH',
                   {'draft':False,'prerelease':True,'make_latest':'false'})
        if result.get('draft') is not False or result.get('prerelease') is not True:
            raise BetaError('GitHub did not confirm prerelease publication')
        # Fetch unauthenticated public download URLs, then independently recheck.
        public=assets_for(result)
        with tempfile.TemporaryDirectory(prefix='wtop-public-acquire-') as acquired:
            root=Path(acquired)
            for asset in public:
                name=asset['name']
                if name not in expected:raise BetaError('unexpected published asset')
                url=asset['browser_download_url']
                prefix=f'https://github.com/{REPOSITORY}/releases/download/{tag}/'
                if not url.startswith(prefix):raise BetaError('public asset URL outside declared release')
                with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'Wtop-release-verifier'}),timeout=60) as response:
                    data=response.read(64*1024*1024+1)
                if len(data)>64*1024*1024 or data!=checked_file(directory/name):
                    raise BetaError('PUBLISHED RELEASE INCIDENT: acquired public bytes differ; do not replace, revoke/investigate and use a new version')
                (root/name).write_bytes(data)
            verify(root)
        print(json.dumps({'state':'public-prerelease','url':result['html_url'],'version':manifest['version'],
                          'source':source,'acquired_bytes_verified':True},indent=2))

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='command',required=True)
    p=commands.add_parser('stage');p.add_argument('directory',type=Path)
    p=commands.add_parser('publish');p.add_argument('tag')
    args=parser.parse_args(argv)
    try:
        if os.environ.get('GITHUB_REPOSITORY')!=REPOSITORY or os.environ.get('GITHUB_REF')!='refs/heads/master':
            raise BetaError('publication tooling runs only in the canonical repository on master')
        if args.command=='stage':stage(args.directory)
        else:
            if os.environ.get('GITHUB_EVENT_NAME')!='workflow_dispatch':
                raise BetaError('public promotion requires explicit workflow_dispatch')
            publish(args.tag)
        return 0
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:
        print('Release operation blocked: '+str(error),file=sys.stderr);return 2
if __name__=='__main__':raise SystemExit(main())
