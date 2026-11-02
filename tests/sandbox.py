#!/usr/bin/env python3
"""Exercise real CLI/store code with deterministic EPERM/EACCES directory opens."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='tatr-sandbox-') as tmp:
    base = Path(tmp).resolve()
    ancestor = base / 'ancestor'
    project = ancestor / 'project'
    db = project / 'tasks'
    db.mkdir(parents=True)
    link = base / 'linked'
    link.symlink_to(ancestor, target_is_directory=True)
    ordinary = base / 'ordinary'
    ordinary.write_text('not a directory')
    inside_link = project / 'linked'
    inside_link.symlink_to(db, target_is_directory=True)
    inside_file = project / 'ordinary'
    inside_file.write_text('not a directory')
    env = os.environ.copy()

    def run(cmd, *args, database=db, code=None, identity=db):
        p = subprocess.run([exe, cmd, '-db', str(database), '-json', *args],
                           env=env, capture_output=True, text=True)
        out = json.loads(p.stdout)
        assert p.returncode == (1 if code else 0), (p.args, out, p.stderr)
        assert out['ok'] is (code is None), out
        if code:
            assert out['diagnostics'][0]['code'] == code, out
            assert out['database'] is None, out
        else:
            assert out['database'] == str(identity) and not p.stderr, out
        return out['data']

    for error in ('EPERM', 'EACCES'):
        if error == 'EACCES':
            env['TATR_TEST_EACCES'] = '1'
        env['TATR_TEST_DENY'] = str(ancestor)
        run('ls')
        task = run('new', '-s', error, 'Sandbox task')['task']
        found = run('find', task['id'])['task']
        assert found == task
        changed = run('set', '-expect', task['fingerprint'], '-property', 'TAGS=sandbox', task['id'])['task']
        assert changed['fingerprint'] != task['fingerprint']
        assert run('find', task['id'])['task'] == changed
        run('check', '-spec', task['id'])
        run('ready', '-spec', task['id'])

        # Re-enter the grant after openat("..") loses access; also walk .. without an fd.
        run('ls', database=f'{project}/../project//./tasks/')
        run('ls', database=f'{ancestor}/../ancestor/project/tasks')
        run('ls', database=f'{project}/tasks/..', identity=project)
        env['TATR_TEST_DENY_SECOND'] = str(project)
        run('ls')
        run('ls', database=f'{project}/../project/tasks')
        run('ls', database=f'{project}/tasks/../tasks')
        env['TATR_TEST_DENY'] = str(db)
        run('ls', code='io_error')
        del env['TATR_TEST_DENY_SECOND']
        for denied_path in (db, ancestor):
            env['TATR_TEST_DENY'] = str(denied_path)
            for suffix in ('', '/', '/.'):
                run('ls', database=f'{denied_path}{suffix}', code='io_error')
        env['TATR_TEST_DENY'] = str(ancestor)
        run('ls', database=f'{project}/..', code='io_error')
        run('ls', database=project / 'missing', code='database_not_found')
        for bad in (inside_link, inside_file):
            run('ls', database=bad, code='unsafe_path')
            run('ls', database=f'{bad}/../tasks', code='unsafe_path')
        for bad in (link, ordinary):
            env['TATR_TEST_DENY'] = str(bad)
            run('ls', database=f'{bad}/project/tasks', code='unsafe_path')
            run('ls', database=f'{bad}/../ancestor/project/tasks', code='unsafe_path')
        env['TATR_TEST_DENY'] = str(ancestor)
        env['TATR_TEST_DENY_ROOT'] = '1'
        run('ls')
        run('ls', database='/', code='io_error')
        run('ls', database='/..', code='io_error')
        del env['TATR_TEST_DENY_ROOT']
        print(f'PASS: {error} metadata-only ancestors, CLI reads/writes, parent traversal, final denial, unsafe paths')
