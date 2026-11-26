"""Checks specs.py's spec-file handling and queue choice (no network):

    python -I test_specs.py
"""
import importlib.util
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('specs', os.path.join(HERE, 'specs.py'))
specs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(specs)

failures = 0


def check(condition, message):
    global failures
    if not condition:
        failures += 1
        print('FAIL:', message)


template = open(os.path.join(specs.SPECS, 'TEMPLATE.md'), encoding='utf-8').read()
fields, body = specs.read_header(template)
check(fields == {'issue': ''}, f'the template header is {fields}')
check(specs.title_of(body, 'x') == 'Short name of the feature', 'the template title')
check(specs.section(body, 'Goal').startswith('What this is for'), 'the template Goal')
check(specs.section(body, 'Open questions').startswith('- Anything'), 'the last section')

written = specs.write_issue(template, 42)
check(specs.read_header(written)[0]['issue'] == '42', 'the issue number was not written')
check(specs.read_header(written)[1] == body, 'writing the issue changed the body')
bare = '# Bare spec\n\n## Goal\nDo it.\n'
check(specs.read_header(specs.write_issue(bare, 7)) == ({'issue': '7'}, bare), 'a spec without a header got a wrong one')
check(specs.title_of('no heading', 'fallback') == 'fallback', 'the fallback title')
check(specs.read_header('---\r\nissue: 3\r\n---\r\n# T\r\n')[0] == {'issue': '3'}, 'a CRLF header')

board = [{'status': 'Backlog', 'number': 1, 'title': 'a'}, {'status': 'In progress', 'number': 2, 'title': 'b'},
         {'status': 'Ready', 'number': 5, 'title': 'c'}, {'status': 'Ready', 'number': 3, 'title': 'd'}]
check(specs.next_ready(board)['number'] == 5, 'next is not the first Ready in board order')
check(specs.next_ready(board[:2]) is None, 'next with nothing Ready')
closed = [{'state': 'CLOSED', 'status': 'In review', 'number': 8}, {'state': 'CLOSED', 'status': 'Done', 'number': 9},
          {'state': 'OPEN', 'status': 'Ready', 'number': 10}]
check([i['number'] for i in specs.closed_not_done(closed)] == [8], 'closed items to move to Done')
# A spec whose issue exists but isn't on the board (registration stopped short) is added, not re-created.
check(specs.missing_from_board({8: 'a.md', 11: 'b.md'}, closed) == [11], 'specs missing from the board')

with tempfile.TemporaryDirectory() as tmp:
    for name in ('README.md', 'TEMPLATE.md', 'b-spec.md', 'a-spec.md', 'notes.txt'):
        open(os.path.join(tmp, name), 'w').write('# x\n')
    check(specs.spec_files(tmp) == ['a-spec.md', 'b-spec.md'], f'spec files: {specs.spec_files(tmp)}')

print('specs checks passed' if not failures else f'{failures} specs checks failed')
sys.exit(1 if failures else 0)
