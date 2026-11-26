"""The spec queue: specs/ files, their GitHub issues, and the "TreeEngine roadmap" project board.

    python specs.py sync                    issues and board items (Backlog) for specs without an issue; closed
                                            issues to Done
    python specs.py queue                   Ready in board order, then Backlog
    python specs.py next                    the top Ready spec
    python specs.py status N "In progress"  move issue N's board item to a column

Needs the GitHub CLI (gh) logged in with the project scope (gh auth refresh -s project). See specs/README.md.
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
SPECS = os.path.join(ROOT, 'specs')
OWNER, REPO = 'lasermike', 'TreeEngine'
BOARD = 'TreeEngine roadmap'
COLUMNS = ['Backlog', 'Ready', 'In progress', 'In review', 'Done']
SKIP = {'README.md', 'TEMPLATE.md'}
GH = os.environ.get('GH', r'C:\Program Files\GitHub CLI\gh.exe' if os.name == 'nt' else 'gh')


# ---------------------------------------------------------------------------------------------------------------
# Spec files (no network).

def read_header(text):
    """The front matter's fields ({} without one) and the body after it."""
    m = re.match(r'---\r?\n(.*?)\r?\n---\r?\n', text, re.S)
    if not m:
        return {}, text
    fields = {}
    for line in m.group(1).splitlines():
        if ':' in line:
            key, value = line.split(':', 1)
            fields[key.strip()] = value.strip()
    return fields, text[m.end():]


def write_issue(text, number):
    """The spec with `issue: number` in its front matter (added if it has none)."""
    fields, body = read_header(text)
    fields['issue'] = str(number)
    header = '\n'.join(f'{k}: {v}' for k, v in fields.items())
    return f'---\n{header}\n---\n{body}'


def title_of(body, fallback):
    m = re.search(r'^#\s+(.+?)\s*$', body, re.M)
    return m.group(1) if m else fallback


def section(body, name):
    """The text under `## name`, up to the next `## ` heading."""
    m = re.search(rf'^##\s+{re.escape(name)}\s*$(.*?)(?=^##\s|\Z)', body, re.M | re.S)
    return m.group(1).strip() if m else ''


def spec_files(folder=SPECS):
    return sorted(f for f in os.listdir(folder) if f.endswith('.md') and f not in SKIP)


def missing_from_board(numbers, items):
    """The spec issue numbers that have no board item."""
    on_board = {i['number'] for i in items}
    return sorted(n for n in numbers if n not in on_board)


def closed_not_done(items):
    """Items whose issue is closed but which aren't in Done (the board's own "Item closed" workflow may be off)."""
    return [i for i in items if i.get('state') == 'CLOSED' and i.get('status') != 'Done']


def next_ready(items):
    """The first Ready item of a board listing (items in board order, each {'status', 'number', 'title'})."""
    for item in items:
        if item.get('status') == 'Ready':
            return item
    return None


# ---------------------------------------------------------------------------------------------------------------
# GitHub.

def gh(*args, input=None):
    result = subprocess.run([GH, *args], capture_output=True, text=True, encoding='utf-8', input=input)
    if result.returncode:
        sys.exit(f'gh {" ".join(args[:3])} failed: {result.stderr.strip()}')
    return result.stdout


def graphql(query, **variables):
    args = ['api', 'graphql', '-f', f'query={query}']
    for k, v in variables.items():
        args += ['-F' if isinstance(v, int) else '-f', f'{k}={v}']
    return json.loads(gh(*args))['data']


def board():
    """The board's id, its Status field id and option ids by name."""
    data = graphql('''query($login: String!) { user(login: $login) { projectsV2(first: 50) { nodes { id title
        field(name: "Status") { ... on ProjectV2SingleSelectField { id options { id name } } } } } } }''', login=OWNER)
    for p in data['user']['projectsV2']['nodes']:
        if p['title'] == BOARD:
            status = p['field']
            return p['id'], status['id'], {o['name']: o['id'] for o in status['options']}
    sys.exit(f'No project "{BOARD}" for {OWNER} (see specs/README.md)')


def items(project_id):
    """The board's items in board order: {'id', 'status', 'number', 'title', 'url', 'state'}."""
    out, cursor = [], None
    while True:
        data = graphql('''query($id: ID!, $after: String) { node(id: $id) { ... on ProjectV2 {
            items(first: 100, after: $after, orderBy: {field: POSITION, direction: ASC}) {
              pageInfo { hasNextPage endCursor }
              nodes { id fieldValueByName(name: "Status") { ... on ProjectV2ItemFieldSingleSelectValue { name } }
                content { ... on Issue { number title url state } } } } } } }''',
                       id=project_id, **({'after': cursor} if cursor else {}))
        page = data['node']['items']
        for n in page['nodes']:
            c = n.get('content') or {}
            if 'number' in c:
                out.append({'id': n['id'], 'status': (n.get('fieldValueByName') or {}).get('name'),
                            'number': c['number'], 'title': c['title'], 'url': c['url'], 'state': c['state']})
        if not page['pageInfo']['hasNextPage']:
            return out
        cursor = page['pageInfo']['endCursor']


def set_status(project_id, field_id, options, item_id, column):
    if column not in options:
        sys.exit(f'No column "{column}" on the board (it has {", ".join(options)})')
    graphql('''mutation($p: ID!, $i: ID!, $f: ID!, $o: String!) { updateProjectV2ItemFieldValue(input:
        {projectId: $p, itemId: $i, fieldId: $f, value: {singleSelectOptionId: $o}}) { projectV2Item { id } } }''',
            p=project_id, i=item_id, f=field_id, o=options[column])


def add_item(project_id, number):
    node = json.loads(gh('api', f'repos/{OWNER}/{REPO}/issues/{number}'))['node_id']
    data = graphql('''mutation($p: ID!, $c: ID!) { addProjectV2ItemById(input: {projectId: $p, contentId: $c})
        { item { id } } }''', p=project_id, c=node)
    return data['addProjectV2ItemById']['item']['id']


# ---------------------------------------------------------------------------------------------------------------
# Commands.

def sync():
    project_id, field_id, options = board()
    made, numbers = 0, {}
    for name in spec_files():
        path = os.path.join(SPECS, name)
        text = open(path, encoding='utf-8').read()
        fields, body = read_header(text)
        if fields.get('issue'):
            numbers[int(fields['issue'].lstrip('#'))] = name
            continue
        title = title_of(body, name[:-3])
        goal = section(body, 'Goal') or '(no Goal yet)'
        issue_body = f'Spec: [specs/{name}](../blob/master/specs/{name})\n\n{goal}\n'
        url = gh('issue', 'create', '--repo', f'{OWNER}/{REPO}', '--title', title, '--label', 'spec',
                 '--body', issue_body).strip().splitlines()[-1]
        number = int(url.rsplit('/', 1)[1])
        # Recorded before the board is touched, so a failure there can't make the next sync open a second issue.
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(write_issue(text, number))
        print(f'#{number} {title} ({name})')
        numbers[number] = name
        made += 1
    print(f'{made} new spec(s) registered' if made else 'every spec has an issue')
    listing = items(project_id)
    # Every spec's issue on the board: new ones, and any whose earlier registration stopped short.
    for number in missing_from_board(numbers, listing):
        set_status(project_id, field_id, options, add_item(project_id, number), 'Backlog')
        print(f'#{number} ({numbers[number]}) -> Backlog')
    for item in closed_not_done(listing):
        set_status(project_id, field_id, options, item['id'], 'Done')
        print(f'#{item["number"]} {item["title"]} closed -> Done')


def queue():
    project_id, _, _ = board()
    listing = items(project_id)
    for column in ('In progress', 'In review', 'Ready', 'Backlog'):
        rows = [i for i in listing if i['status'] == column]
        if rows:
            print(f'{column}:')
            for i in rows:
                print(f'  #{i["number"]} {i["title"]}')


def next_spec():
    project_id, _, _ = board()
    item = next_ready(items(project_id))
    print(f'#{item["number"]} {item["title"]} {item["url"]}' if item else 'Ready is empty')


def status(number, column):
    project_id, field_id, options = board()
    for i in items(project_id):
        if i['number'] == number:
            set_status(project_id, field_id, options, i['id'], column)
            print(f'#{number} -> {column}')
            return
    sys.exit(f'#{number} is not on the board')


def main(argv):
    if not argv or argv[0] in ('-h', '--help'):
        print(__doc__)
        return
    command = argv[0]
    if command == 'sync':
        sync()
    elif command == 'queue':
        queue()
    elif command == 'next':
        next_spec()
    elif command == 'status' and len(argv) == 3:
        status(int(argv[1].lstrip('#')), argv[2])
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
