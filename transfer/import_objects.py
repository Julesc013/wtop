"""One-use, hash-bound source transfer. Never modifies a remote ref or executes packet code."""
import base64
import datetime
import hashlib
import json
import lzma
import os
from pathlib import Path
import re
import subprocess
import tempfile
import urllib.request

REPOSITORY = 'Julesc013/wtop'
BASE = 'd0089d6384d878bed0abc5321c0ebcd1cfb28c85'
HEAD = '25411f549dd9508319bc349d0de83d365417ecd7'
PACKET_SHA = '736b4fb0bdebd02f70f1fb24e8bf4f750c213baf5d5b0f2a31f11ebf6ad4ee0b'
ROOT = Path(__file__).resolve().parents[1]


def git(directory, *args, data=None):
    return subprocess.run(['git', '-c', 'core.hooksPath=/dev/null', '-C', str(directory), *args], input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True, timeout=60).stdout


def api(path, payload):
    request = urllib.request.Request('https://api.github.com/repos/' + REPOSITORY + '/git/' + path,
        data=json.dumps(payload).encode(), method='POST', headers={
            'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
            'Accept': 'application/vnd.github+json', 'Content-Type': 'application/json',
            'User-Agent': 'wtop-source-transfer', 'X-GitHub-Api-Version': '2022-11-28'})
    with urllib.request.urlopen(request, timeout=45) as response:
        body = response.read(2 * 1024 * 1024 + 1)
    if len(body) > 2 * 1024 * 1024:
        raise ValueError('oversized Git API response')
    return json.loads(body)


def identity(line):
    match = re.fullmatch(r'(.+) <([^<>]+)> ([0-9]+) \+0000', line)
    if not match:
        raise ValueError('unexpected commit identity')
    return {'name': match[1], 'email': match[2], 'date': datetime.datetime.fromtimestamp(
        int(match[3]), datetime.timezone.utc).isoformat().replace('+00:00', 'Z')}


def main():
    if os.environ.get('GITHUB_REPOSITORY') != REPOSITORY:
        raise ValueError('wrong repository')
    encoded = ''.join((ROOT / 'transfer' / ('part%d.txt' % n)).read_text().strip() for n in (1, 2, 3))
    if len(encoded) != 44832:
        raise ValueError('packet length mismatch')
    packed = base64.b64decode(encoded, validate=True)
    if hashlib.sha256(packed).hexdigest() != PACKET_SHA:
        raise ValueError('packet digest mismatch')
    decoder = lzma.LZMADecompressor(memlimit=128 * 1024 * 1024)
    raw = decoder.decompress(packed, max_length=1024 * 1024)
    if not decoder.eof or decoder.unused_data or len(raw) != 124262:
        raise ValueError('packet expansion mismatch')
    packet = json.loads(raw)
    if packet['base'] != BASE or packet['head'] != HEAD or len(packet['commits']) != 6:
        raise ValueError('unexpected packet scope')
    with tempfile.TemporaryDirectory(prefix='wtop-object-import-') as temporary:
        work = Path(temporary) / 'work'
        subprocess.run(['git', 'clone', '--no-checkout', '--no-hardlinks', str(ROOT), str(work)], check=True, timeout=60)
        git(work, 'checkout', '--detach', BASE)
        previous = BASE
        records = []
        # Validate every local commit and tree before uploading any object.
        for record in packet['commits']:
            raw_commit = record['raw'].encode('utf-8')
            headers, message = record['raw'].split('\n\n', 1)
            fields = dict(line.split(' ', 1) for line in headers.splitlines())
            if set(fields) != {'tree', 'parent', 'author', 'committer'} or fields['parent'] != previous:
                raise ValueError('unexpected commit headers/ancestry')
            if not re.fullmatch('[0-9a-f]{40}', record['sha']):
                raise ValueError('invalid commit SHA')
            git(work, 'apply', '--unidiff-zero', '--index', '-', data=record['patch'].encode('utf-8'))
            tree = git(work, 'write-tree').decode().strip()
            if tree != fields['tree']:
                raise ValueError('tree mismatch: ' + record['sha'])
            sha = git(work, 'hash-object', '-t', 'commit', '-w', '--stdin', data=raw_commit).decode().strip()
            if sha != record['sha']:
                raise ValueError('commit mismatch')
            records.append((sha, fields, message))
            previous = sha
        if previous != HEAD:
            raise ValueError('head mismatch')
        created_blobs = set()
        for sha, fields, message in records:
            parent = fields['parent']
            names = git(work, 'diff', '--name-only', '-z', parent, sha).split(b'\0')
            tree_entries = []
            for raw_name in names:
                if not raw_name:
                    continue
                name = raw_name.decode('utf-8')
                entries = git(work, 'ls-tree', '-z', sha, '--', name)
                if not entries:
                    tree_entries.append({'path': name, 'mode': '100644', 'type': 'blob', 'sha': None})
                    continue
                header, listed_name = entries.rstrip(b'\0').split(b'\t', 1)
                mode, kind, blob_sha = header.decode().split(' ')
                if kind != 'blob' or mode not in ('100644', '100755') or listed_name != raw_name:
                    raise ValueError('unsupported transfer entry')
                if blob_sha not in created_blobs:
                    contents = git(work, 'cat-file', 'blob', blob_sha)
                    observed = api('blobs', {'content': base64.b64encode(contents).decode(), 'encoding': 'base64'})
                    if observed.get('sha') != blob_sha:
                        raise ValueError('remote blob mismatch')
                    created_blobs.add(blob_sha)
                tree_entries.append({'path': name, 'mode': mode, 'type': kind, 'sha': blob_sha})
            parent_tree = git(work, 'rev-parse', parent + '^{tree}').decode().strip()
            observed = api('trees', {'base_tree': parent_tree, 'tree': tree_entries})
            if observed.get('sha') != fields['tree']:
                raise ValueError('remote tree mismatch')
            observed = api('commits', {'message': message.rstrip('\n'), 'tree': fields['tree'],
                'parents': [parent], 'author': identity(fields['author']), 'committer': identity(fields['committer'])})
            if observed.get('sha') != sha:
                raise ValueError('remote commit mismatch: expected ' + sha + ', observed ' + str(observed.get('sha')))
            print('Verified imported commit', sha, flush=True)
        result = {'repository': REPOSITORY, 'base': BASE, 'head': HEAD, 'tree': records[-1][1]['tree'],
                  'commits': [r[0] for r in records], 'packet_sha256': PACKET_SHA, 'remote_refs_modified': False}
        (ROOT / 'import-result.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
