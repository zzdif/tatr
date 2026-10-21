#!/usr/bin/env python3
"""Black-box contract checks; all task writes stay in an owned temporary directory."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())
passed = 0
with tempfile.TemporaryDirectory(prefix='tatr-scheduler-') as tmp:
    base = Path(tmp).resolve()
    db = base / 'database with spaces'
    db.mkdir()
    def run(cmd, *args, ok=True, code=None, database=db):
        p = subprocess.run([exe, cmd, '-db', str(database), '-json', *args], cwd=base,
                           capture_output=True, text=True)
        try:
            out = json.loads(p.stdout)
        except Exception:
            raise AssertionError((p.args, p.returncode, p.stdout, p.stderr))
        assert p.returncode == (0 if ok else 1), (p.args, out, p.stderr)
        assert out['ok'] is ok and out['schema_version'] == 1, out
        if ok:
            assert p.stderr == '', p.stderr
            assert out['database'] == str(database), out
        else:
            assert out['data'] is None
            if code:
                assert out['diagnostics'][0]['code'] == code, out
        return out['data'] if ok else out
    def fixture(n, *, spec=None, deps=(), status='OPEN', resolution=None, priority='100', extra='', body='\nbody λ " \\ \t\n'):
        id = f'20261020-{n:06}'
        folder = db / id
        folder.mkdir(exist_ok=True)
        text = f'# Task {n}\n\n- STATUS: {status}\n- PRIORITY: {priority}\n- TAGS: one,two\n'
        if spec is not None:
            text += f'- SPEC: {spec}\n'
        if deps:
            text += f'- DEPENDS: {", ".join(deps)}\n'
        if resolution is not None:
            text += f'- RESOLUTION: {resolution}\n'
        (folder / 'TASK.md').write_text(text + extra + body)
        return id
    def read(id):
        return run('find', id)['task']
    def change(id, **props):
        token = read(id)['fingerprint']
        args = ['-expect', token]
        for k, v in props.items():
            args += ['-property', f'{k}={v}']
        return run('set', *args, id)['task']
    def ready(root):
        return run('ready', '-spec', root)
    def proof(name):
        global passed
        passed += 1
        print(f'PASS {passed:02} {name}')

    root = fixture(0)
    a = fixture(1, spec=root, priority='2147483647')
    b = fixture(2, spec=root, deps=[a], priority='-2147483648')
    c = fixture(3, spec=root, deps=[a])
    d = fixture(4, spec=root, deps=[b, c])
    unrelated = fixture(99)
    (db / unrelated / 'TASK.md').write_text('# Legacy\n\nOld prose\n')
    before = {p: p.stat().st_mtime_ns for p in db.rglob('*')}
    r = ready(root)
    assert [t['id'] for t in r['ready']] == [a] and r['state'] == 'ready'
    assert [x['id'] for x in r['tasks']] == [root, a, b, c, d]
    snapshot_bytes = ''.join(t['id']+' '+t['fingerprint']+'\n' for t in r['tasks']).encode()
    assert r['snapshot'] == 'sha256:' + hashlib.sha256(snapshot_bytes).hexdigest()
    assert run('check','-spec',root)['snapshot'] == r['snapshot']
    assert before == {p: p.stat().st_mtime_ns for p in db.rglob('*')}
    proof('A02/A06/A07/A11/A15 explicit spaced database, scoped legacy tolerance, frontier, read-only metadata')
    raw = (db / a / 'TASK.md').read_bytes()
    t = read(a)
    assert t['fingerprint'] == 'sha256:' + hashlib.sha256(raw).hexdigest()
    assert t['body'] == '\nbody λ " \\ \t\n' and t['spec'] == root and t['resolution'] is None
    assert read(a) == t
    (db / a / 'TASK.md').write_bytes(raw + b'\n')
    assert read(a)['fingerprint'] != t['fingerprint']
    proof('A03/A04 Unicode/control escaping, exact parser body, independent hashlib raw-byte oracle')
    change(a, STATUS='CLOSED', RESOLUTION='DONE')
    assert [t['id'] for t in ready(root)['ready']] == [c, b]
    change(b, STATUS='CLOSED', RESOLUTION='DONE')
    change(c, STATUS='CLOSED', RESOLUTION='DONE')
    assert [t['id'] for t in ready(root)['ready']] == [d]
    change(d, STATUS='CLOSED', RESOLUTION='DONE')
    assert ready(root)['state'] == 'complete'
    change(a, STATUS='OPEN')
    r = ready(root)
    assert r['state'] == 'ready' and {x['id'] for x in r['blocked']} == {b, c, d}
    assert read(a)['resolution'] is None
    proof('A07/A09/A10 diamond release, completion, reopening invalidates closed-DONE descendants')
    change(a, STATUS='CLOSED', RESOLUTION='CANCELLED')
    r = ready(root)
    assert r['state'] == 'blocked' and r['unresolved'][0]['reason'] == 'cancelled'
    change(a, RESOLUTION='')
    assert ready(root)['unresolved'][0]['reason'] == 'unspecified_closure'
    other_root = fixture(10)
    external = fixture(11, spec=other_root)
    change(a, STATUS='OPEN', DEPENDS=external)
    r = ready(root)
    assert r['state'] == 'blocked' and external not in [x['id'] for x in r['ready']]
    assert r['blocked'][0]['prerequisites'][0] == {'id': external, 'reason': 'open', 'external': True}
    proof('A08/A09 cross-spec prerequisite explanation, cancelled and legacy closure do not satisfy')
    s1 = run('check', '-spec', root)['snapshot']
    raw = (db / unrelated / 'TASK.md').read_bytes()
    (db / unrelated / 'TASK.md').write_bytes(raw + b'changed\n')
    assert run('check', '-spec', root)['snapshot'] == s1
    added = fixture(12, spec=root)
    assert run('check', '-spec', root)['snapshot'] != s1
    (db / added / 'TASK.md').unlink(); (db / added).rmdir()
    assert run('check', '-spec', root)['snapshot'] == s1
    proof('A20 scope snapshot tracks membership but excludes unrelated record edits')
    path = db / a / 'TASK.md'
    original = path.read_bytes()
    token = read(a)['fingerprint']
    path.write_bytes(original + b'changed\n')
    run('set', '-expect', token, '-property', 'TAGS=x', a, ok=False, code='conflict')
    assert path.read_bytes() == original + b'changed\n'
    path.write_bytes(original)
    stat = path.stat()
    change(a, PRIORITY='0002147483647', DEPENDS=f' {external} ')
    assert path.read_bytes() == original and path.stat().st_mtime_ns == stat.st_mtime_ns
    attachment = path.parent / 'attachment'; attachment.write_bytes(b'keep me')
    change(a, Custom='a=b', TAGS='')
    assert attachment.read_bytes() == b'keep me' and read(a)['properties']['Custom'] == 'a=b'
    assert read(a)['body'] == '\nbody λ " \\ \t\n\n'
    proof('A13/A14 stale token preserves bytes, typed no-op preserves mtime, unknown fields/body/attachments survive')
    token = read(a)['fingerprint']
    original = path.read_bytes()
    run('set', '-expect', token, '-property', 'RESOLUTION=DONE', a, ok=False, code='invalid_task')
    cycle_error = run('set', '-expect', token, '-property', f'DEPENDS={d}', a, ok=False, code='dependency_cycle')
    assert cycle_error['diagnostics'][0]['cycle'] == [a,d,b,a], cycle_error
    run('set', '-expect', token, '-property', f'DEPENDS={root}', a, ok=False, code='invalid_relationship')
    assert path.read_bytes() == original
    proof('A05/A08/A13 contradictory outcome, cycle and container dependency rejected before mutation')
    for value in ['2147483648', '-2147483649', '+1', '0x1', '1x', '', '1 2']:
        fixture(20, priority=value)
        run('find', '20261020-000020', ok=False, code='invalid_task')
    fixture(20, extra='- STATUS: CLOSED\n')
    run('find', '20261020-000020', ok=False, code='invalid_task')
    for key in ['depends', 'DEPENDS_ON', 'Depends', 'SPEC_ID']:
        fixture(20, extra=f'- {key}: {a}\n')
        run('find', '20261020-000020', ok=False, code='invalid_task')
    for deps in [f'{b},', f',{b}', f'{b},,{c}', f'{b},{b}', 'nope', '20261020-000020']:
        fixture(20, extra=f'- DEPENDS: {deps}\n')
        run('find', '20261020-000020', ok=False, code='invalid_relationship')
    fixture(20, extra='- DEPENDS: 20261020-777777\n')
    assert read('20261020-000020')['depends'] == ['20261020-777777']
    fixture(20, spec=root, extra='- DEPENDS: 20261020-777777\n')
    run('check', '-spec', root, ok=False, code='missing_dependency')
    path20 = db / '20261020-000020' / 'TASK.md'
    path20.unlink(); path20.parent.rmdir()
    proof('A05/A11/A12 strict integer grammar, duplicates, near-matches, invalid edges; draft dangling reads vs graph failure')
    held = os.open(db, os.O_RDONLY)
    fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
    run('ready', '-spec', root, ok=False, code='busy')
    run('find', a, ok=False, code='busy')
    os.close(held)
    held = os.open(db, os.O_RDONLY)
    fcntl.flock(held, fcntl.LOCK_SH | fcntl.LOCK_NB)
    assert read(a)['id'] == a
    token = read(a)['fingerprint']
    run('set','-expect',token,'-property','Custom=blocked-by-reader',a,ok=False,code='busy')
    os.close(held)
    proof('A15 held exclusive lock blocks reads; shared locks allow reads but block mutations, without lock artifacts')
    run('find', a, database=base/'missing', ok=False, code='database_not_found')
    symlink = base / 'linked'; symlink.symlink_to(db, target_is_directory=True)
    run('find', a, database=symlink, ok=False, code='unsafe_path')
    original = path.read_bytes(); path.unlink(); path.symlink_to(attachment)
    run('find', a, ok=False, code='unsafe_path')
    path.unlink(); path.write_bytes(original)
    link = base / 'hardlink'; os.link(path, link)
    run('find', a, ok=False, code='unsafe_path')
    link.unlink()
    path.unlink(); os.mkfifo(path)
    run('find', a, ok=False, code='unsafe_path')
    path.unlink(); path.write_bytes(original)
    proof('A02/A17 nonexistent explicit DB never falls back; symlink DB/TASK, hardlink, FIFO rejected')
    bad_db = os.fsencode(base) + b'/invalid-utf8-\xff'
    try:
        os.mkdir(bad_db)
    except OSError:
        print('NOTE: filesystem refused a non-UTF-8 database name')
    else:
        p = subprocess.run([os.fsencode(exe),b'ls',b'-db',bad_db,b'-json'],capture_output=True)
        out = json.loads(p.stdout)
        assert p.returncode == 1 and out['database'] is None and out['diagnostics'][0]['code'] == 'unsafe_path'
        os.rmdir(bad_db)
    for suffix in ['', '../escape', 'a b', '.', 'a/b']:
        run('new', '-s', suffix, 'Bad', ok=False, code='invalid_arguments')
    run('new', 'Bad\n- STATUS: CLOSED', ok=False, code='invalid_arguments')
    body = base/'body'; body.write_text('\n## Criteria\n\nλ\n')
    new = run('new', '-s', 'native-test', '-p', '-2147483648', '-body-file', str(body), '-property', f'SPEC={root}', 'Created task')['task']
    assert new['priority'] == -2147483648 and new['body'] == '\n' + body.read_text()
    assert read(new['id']) == new
    proof('A03/A16 new real record/fingerprint, signed priority, exact body payload; injection/suffix rejection')
    empty = fixture(30)
    assert ready(empty)['state'] == 'empty'
    missing = db / '20261020-000031'; missing.mkdir()
    run('check', '-spec', root, ok=False, code='task_not_found')
    missing.rmdir()
    hidden = fixture(98)
    hidden_path = db/hidden/'TASK.md'
    hidden_path.write_bytes(b'# Hidden\n\n- STATUS: OPEN\n- PRIORITY: 100\n\x00- SPEC: '+root.encode()+b'\n')
    run('check','-spec',root,ok=False,code='invalid_task')
    hidden_path.unlink(); hidden_path.parent.rmdir()
    proof('A06/A10 empty is not complete; incomplete or NUL-unclassifiable records cannot disappear from discovery')
    # A pipe barrier starts two real CLI writers with the same expected token.
    token = read(a)['fingerprint']
    gate_r, gate_w = os.pipe()
    children = []
    launcher = 'import os,sys; os.read(int(sys.argv[1]),1); os.execv(sys.argv[2],sys.argv[2:])'
    for value in ['writer-one', 'writer-two']:
        children.append(subprocess.Popen([sys.executable, '-c', launcher, str(gate_r), exe,
            'set', '-db', str(db), '-json', '-expect', token, '-property', 'Custom='+value, a],
            pass_fds=(gate_r,), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
    os.close(gate_r); os.write(gate_w, b'go'); os.close(gate_w)
    outputs = []
    for child in children:
        stdout, stderr = child.communicate(timeout=10)
        out = json.loads(stdout); outputs.append(out)
        assert child.returncode == (0 if out['ok'] else 1) and stderr == '', (out, stderr)
        if not out['ok']:
            assert out['diagnostics'][0]['code'] in ['busy', 'conflict'], out
    assert sum(x['ok'] for x in outputs) == 1, outputs
    run('set', '-expect', token, '-property', 'Custom=stale-replay', a, ok=False, code='conflict')
    proof('A14 real simultaneous CLI writers, pipe barrier, exactly one successful update; stale replay conflicts')
    # The legacy writer cooperates, preserves metadata and never invents DONE.
    change(a, STATUS='CLOSED', RESOLUTION='DONE')
    for action, closed in [('open', True), ('close', False), ('+legacy-tag', True)]:
        args = [exe, 'ls', '-db', str(db)] + (['-c'] if closed else []) + ['-update', action, a]
        p = subprocess.run(args, cwd=base, capture_output=True, text=True)
        assert p.returncode == 0 and 'UPDATED' in p.stdout, (p.stdout,p.stderr)
        assert read(a)['resolution'] is None
        assert read(a)['spec'] == root and read(a)['depends'] == [external]
    proof('A19 legacy reopen clears DONE; close does not invent it; tag changes preserve relationships')
    change(a, Custom='A'*32000); raw_a = path.read_bytes()
    change(a, Custom='B'*32000); raw_b = path.read_bytes()
    change(a, Custom='A'*32000)
    ack_r, ack_w = os.pipe(); stop_r, stop_w = os.pipe()
    child = os.fork()
    if child == 0:
        os.close(ack_r); os.close(stop_w); os.set_blocking(stop_r, False)
        previous = None
        try:
            while True:
                contents = path.read_bytes()
                assert contents in [raw_a,raw_b], 'partial file visible'
                label = b'A' if contents == raw_a else b'B'
                if label != previous:
                    os.write(ack_w,label); previous = label
                try:
                    if os.read(stop_r,1):
                        break
                except BlockingIOError:
                    pass
            os._exit(0)
        except BaseException:
            os._exit(1)
    os.close(ack_w); os.close(stop_r)
    assert os.read(ack_r,1) == b'A'
    for label in ['B','A','B','A','B','A']:
        change(a, Custom=label*32000)
        assert os.read(ack_r,1) == label.encode(), 'reader must observe each whole replacement'
    os.write(stop_w,b'x'); os.close(stop_w); os.close(ack_r)
    _, status = os.waitpid(child,0)
    assert os.waitstatus_to_exitcode(status) == 0
    proof('A15 pipe-coordinated raw reader sees only complete 32KB old/new files across six real CLI replacements')
    token = read(a)['fingerprint']
    gate_r, gate_w = os.pipe()
    p = subprocess.Popen([sys.executable,'-c',launcher,str(gate_r),exe,'set','-db',str(db),'-json',
        '-expect',token,'-property','Custom=output-failure',a],pass_fds=(gate_r,),stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    p.stdout.close(); os.close(gate_r); os.write(gate_w,b'x'); os.close(gate_w)
    diagnostic = p.stderr.read(); p.wait(timeout=10)
    assert p.returncode == 1 and 'io_error' in diagnostic and 'write_state: committed' in diagnostic, diagnostic
    assert read(a)['properties']['Custom'] == 'output-failure'
    proof('A18 pipe barrier forces broken output after commit; nonzero exit and committed diagnostic require fresh-read reconciliation')
    # Header CRLF is accepted but raw fingerprints/body line endings stay byte-exact.
    crlf = fixture(40)
    crlf_path = db / crlf / 'TASK.md'
    crlf_bytes = crlf_path.read_bytes().replace(b'\n', b'\r\n')
    crlf_path.write_bytes(crlf_bytes)
    assert read(crlf)['fingerprint'] == 'sha256:' + hashlib.sha256(crlf_bytes).hexdigest()
    # Scope discovery tolerates unrelated duplicate non-membership headers and invalid body bytes.
    (db / unrelated / 'TASK.md').write_bytes(b'# Legacy\n\n- CUSTOM: one\n- CUSTOM: two\n\nbody\xff\n')
    assert run('check', '-spec', root)
    # Ordinary creation need not inspect unrelated incomplete records.
    incomplete = db / '20261020-000041'; incomplete.mkdir()
    assert run('new', '-s', 'independent', 'Independent creation')['task']['status'] == 'OPEN'
    incomplete.rmdir()
    rel = os.path.relpath(db, base)
    p = subprocess.run([exe,'find','-db', str(base/'sub'/'..'/rel),'-json',a],capture_output=True,text=True)
    # A missing component is not lexically erased: establish the directory first.
    (base/'sub').mkdir()
    p = subprocess.run([exe,'find','-db', str(base/'sub'/'..'/rel),'-json',a],capture_output=True,text=True)
    assert p.returncode == 0 and json.loads(p.stdout)['database'] == str(db), p.stdout
    proof('A02/A04/A06 CRLF raw hash, scoped unrelated-header tolerance, independent creation, canonical parent paths')
    list_db = base/'list database'; list_db.mkdir()
    assert run('ls', database=list_db)['tasks'] == []
    # Deliberately create in reverse ID order; directory enumeration must not set ties.
    for n, status, priority in [(3,'CLOSED',5),(2,'OPEN',2147483647),(1,'OPEN',2147483647),(0,'OPEN',-2147483648)]:
        folder = list_db / f'20261021-{n:06}'; folder.mkdir()
        (folder/'TASK.md').write_text(f'# List {n}\n\n- STATUS: {status}\n- PRIORITY: {priority}\n- TAGS: chosen\n\nprose\n')
    ids = lambda result: [x['id'] for x in result['tasks']]
    assert ids(run('ls',database=list_db)) == ['20261021-000001','20261021-000002','20261021-000000']
    assert ids(run('ls','-a',database=list_db)) == ['20261021-000000','20261021-000002','20261021-000001']
    assert ids(run('ls','-id',database=list_db)) == ['20261021-000002','20261021-000001','20261021-000000']
    assert ids(run('ls','-id','-a',database=list_db)) == ['20261021-000000','20261021-000001','20261021-000002']
    assert ids(run('ls','-c',database=list_db)) == ['20261021-000003']
    assert ids(run('ls','priority lt 0',database=list_db)) == ['20261021-000000']
    assert len(run('ls','.chosen',database=list_db)['tasks']) == 3
    run('ls','not-a-query',database=list_db,ok=False,code='invalid_arguments')
    p = subprocess.run([exe,'find','-db',str(list_db),'-path-only','20261021-000003'],capture_output=True,text=True)
    assert p.returncode == 0 and p.stdout == str(list_db/'20261021-000003'/'TASK.md')+'\n' and not p.stderr
    p = subprocess.run([exe,'ls','-db',str(list_db),'priority lt 0'],capture_output=True,text=True)
    assert p.returncode == 0 and 'TASK.md:1: OPEN' in p.stdout and 'List 0' in p.stdout
    proof('A01/A03/A11 JSON empty arrays, default/closed/TQL selection, full stable sort reversal; legacy text/path-only')
    list_root = '20261021-000004'; (list_db/list_root).mkdir()
    (list_db/list_root/'TASK.md').write_text('# Closed root\n\n- STATUS: CLOSED\n- PRIORITY: 100\n\n')
    for n in [0,1,2]:
        path_list = list_db/f'20261021-{n:06}'/'TASK.md'
        text = path_list.read_text().replace('\n\nprose',f'\n- SPEC: {list_root}\n\nprose')
        path_list.write_text(text)
    frontier = run('ready','-spec',list_root,database=list_db)
    assert ids({'tasks':frontier['ready']}) == ['20261021-000001','20261021-000002','20261021-000000']
    assert frontier['state'] == 'ready'
    proof('A09/A11 closed root does not complete children; ready priority ties are HUID ordered')
    if len(sys.argv) >= 4:
        target = '20261021-000000'; target_path = list_db/target/'TASK.md'
        original = target_path.read_bytes()
        token = run('find',target,database=list_db)['task']['fingerprint']
        for stage, binary in enumerate(sys.argv[2:4],1):
            p = subprocess.run([str(Path(binary).resolve()),'set','-db',str(list_db),'-json',
                '-expect',token,'-property','TAGS=injected',target],capture_output=True,text=True)
            out = json.loads(p.stdout)
            assert p.returncode == 1 and not out['ok'] and out['data'] is None and not p.stderr, out
            diagnostic = out['diagnostics'][0]
            assert diagnostic['code'] == 'io_error', out
            assert not list(target_path.parent.glob('.tatr-*.tmp'))
            if stage == 1:
                assert target_path.read_bytes() == original and 'write_state' not in diagnostic
            else:
                assert diagnostic['write_state'] == 'committed' and target_path.read_bytes() != original
                assert run('find',target,database=list_db)['task']['tags'] == ['injected']
        proof('A18 black-box injected pre/post-rename failures, JSON write_state, exact bytes, cleanup and fresh-read reconciliation')
        # The test-only binary fixes UTC time to the epoch: no clock race or sleep.
        candidate = '19700101-000000-incoming'; draft = list_db/'20261021-000005'; draft.mkdir()
        (draft/'TASK.md').write_text(f'# Draft\n\n- STATUS: OPEN\n- PRIORITY: 100\n- SPEC: {candidate}\n- DEPENDS: {candidate}\n\n')
        original = (draft/'TASK.md').read_bytes()
        p = subprocess.run([str(Path(sys.argv[2]).resolve()),'new','-db',str(list_db),'-json','-s','incoming','Container'],capture_output=True,text=True)
        out = json.loads(p.stdout)
        assert p.returncode == 1 and out['diagnostics'][0]['code'] == 'invalid_relationship', out
        assert not (list_db/candidate).exists() and (draft/'TASK.md').read_bytes() == original
        proof('A08/A13/A16 deterministic generated ID cannot activate a draft container/dependency violation')
        (draft/'TASK.md').unlink(); draft.rmdir()
    if len(sys.argv) == 5:
        preview = subprocess.run([exe,'ls','-db',str(list_db),'any'],capture_output=True,text=True)
        selected = [Path(line.split('/TASK.md:1:')[0]).name for line in preview.stdout.splitlines()]
        assert len(selected) == 3
        p = subprocess.run([str(Path(sys.argv[4]).resolve()),'ls','-db',str(list_db),'-update','+bulk','any'],capture_output=True,text=True)
        assert p.returncode == 1 and p.stdout.count('UPDATED') == 1
        assert f'bulk update stopped at {selected[1]} after 1 replacements' in p.stderr, p.stderr
        for i,id in enumerate(selected):
            tags_after = run('find',id,database=list_db)['task']['tags']
            assert ('bulk' in tags_after) == (i == 0)
        proof('A19 injected second bulk replacement failure identifies committed first ID/failed ID; untouched remainder and no blind rollback')
    for payload in [b'# Bad\x00title\n\n- STATUS: OPEN\n- PRIORITY: 1\n',
                    b'# Bad\xfftitle\n\n- STATUS: OPEN\n- PRIORITY: 1\n']:
        (db/crlf/'TASK.md').write_bytes(payload)
        run('find',crlf,ok=False,code='invalid_task')
    (db/crlf/'TASK.md').write_bytes(crlf_bytes)
    before = set(db.iterdir())
    for args in [('-property','X=1','-property','X=2'), ('-property','STATUS=CLOSED'),
                 ('-property','Bad_Key=value'), ('-property','X=bad\nvalue')]:
        run('new',*args,'Invalid creation',ok=False,code='invalid_arguments')
    assert set(db.iterdir()) == before
    run('find','-unknown',a,ok=False,code='invalid_arguments')
    proof('A03/A12/A16 invalid UTF-8/NUL records, malformed/duplicate/injected properties rejected without publication')
    # Each new command has help with no database access, including -json -help.
    for cmd in ['new', 'set', 'check', 'ready', 'ls', 'find']:
        p = subprocess.run([exe, cmd, '-help'], cwd=base, capture_output=True, text=True)
        assert p.returncode == 0 and 'Usage:' in p.stdout and p.stderr == '', (cmd,p)
        q = subprocess.run([exe, 'help', cmd], cwd=base, capture_output=True, text=True)
        assert q.returncode == 0 and q.stdout == p.stdout and not q.stderr
    p = subprocess.run([exe,'ls','-debug','any'],cwd=base,capture_output=True,text=True)
    assert p.returncode == 0 and 'QUERY OPS:' in p.stdout
    for cmd in ['set','check','ready']:
        p = subprocess.run([exe,cmd,'-json'],cwd=base,capture_output=True,text=True)
        assert p.returncode == 1 and json.loads(p.stdout)['diagnostics'][0]['code'] == 'invalid_arguments'
    # Invalid combinations and malformed JSON requests are still single objects.
    run('ls', '-update', 'close', ok=False, code='invalid_arguments')
    run('find', '-path-only', a, ok=False, code='invalid_arguments')
    run('ready', ok=False, code='invalid_arguments')
    proof('A03 help needs no database, forbidden JSON combinations yield one error object')
print(f'{passed} scheduler contract groups passed on {sys.platform}')
