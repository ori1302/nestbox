"""End to end on a Mac: the simulator (the same engine and HTTP API as the device) runs headless; the test finds it
the way the companion finds the device (Bonjour, through the nestbox command) and talks to it only over the
network. It reports the user's activity as a computer does (POST /v1/user), tells it what is going on (POST
/v1/tell), runs the Claude Code hooks example (integrations/claude-code/hooks.json, jq and curl) on real hook
payloads, pairs, and finally runs the real companion against it. The device's own button and motion sensor go to
POST /v1/input, which the simulator takes from this Mac only.

It checks that the pet walks to its desk before it works, that typing, pauses, being away and a lost computer show,
that an agent's whole session shows in GET /v1 and ends cleanly, that steps and throws play and end, that quiet,
seen and dismissed work, and that the replies say what happened.

The simulator's clock runs SPEED times faster.

    python3 tests/e2e_test.py [--build DIR]
"""
import http.client
import json
import os
import re
import selectors
import shutil
import subprocess
import threading
import time
import urllib.error
import urllib.request

from harness import Simulator, build_dir, check, finish

BUILD = build_dir()
HOOKS_EXAMPLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'integrations', 'claude-code',
                             'hooks.json')
SPEED = 2.0
NAME = f'nestbox-e2e-{os.getpid()}'        # the simulator's id starts with "sim-", so no companion pairs with it
POLL_S = 0.02
SESSION = 'e2e7f3a9c1d2b8e4'


class Pet:
    def __init__(self, url):
        self.url = url

    def call(self, method, path, body=None, raw=None, headers=None):
        data = raw if raw is not None else (json.dumps(body).encode() if body is not None else None)
        req = urllib.request.Request(self.url + path, data=data, method=method, headers=headers or {})
        try:
            with urllib.request.urlopen(req, timeout=3) as r:
                text = r.read().decode()
                return r.status, text
        except urllib.error.HTTPError as e:
            return e.code, (e.read() or b'').decode()

    def post(self, path, body=None, raw=None):
        status, text = self.call('POST', path, body, raw)
        try:
            return status, json.loads(text)
        except ValueError:
            return status, {}

    def tell(self, body):
        return self.post('/v1/tell', body)

    def input(self, body):
        """The device's own button or sensor."""
        return self.post('/v1/input', body)

    def get_json(self, path):
        return json.loads(self.call('GET', path)[1])

    def now(self):
        return self.get_json('/v1')

    def body(self):
        """The body's details: where it stands, what it plays, which phase."""
        return self.get_json('/v1/debug')

    def note(self, sender, id=''):
        return next((n for n in self.now()['notes'] if n['from'] == sender and n.get('id', '') == id), None)


class Computer(threading.Thread):
    """Reports activity like the companion, once a pet-second: keys since the last report, idle seconds, the
    screen's state. `on` false stops the reports, as when the Mac sleeps."""

    def __init__(self, pet):
        super().__init__(daemon=True)
        self.pet, self.keys, self.idle, self.state, self.on = pet, 0, 0, 'awake', True

    def run(self):
        while True:
            if self.on:
                try:
                    self.pet.post('/v1/user', {'from': 'e2e-mac', 'keys': self.keys, 'idle': self.idle,
                                               'state': self.state, 'next': 2})
                except (OSError, ValueError):
                    pass
            self.idle = self.idle + 1 if self.keys == 0 else 0
            time.sleep(1 / SPEED)


class Session:
    def __init__(self, sim, url):
        self.sim, self.url = sim, url
        self.pet = Pet(url)
        self.mac = Computer(self.pet)
        with open(self.device_file(), 'w') as f:     # where the hooks find the pet
            f.write(url + '\n')

    def device_file(self):
        os.makedirs(os.path.join(self.sim.home, '.nestbox'), exist_ok=True)
        return os.path.join(self.sim.home, '.nestbox', 'device')

    def watch(self, seconds, until=None):
        """Polls the body for `seconds` pet-seconds, or until `until(body)`; returns every state seen."""
        seen, end = [], time.time() + seconds / SPEED
        while time.time() < end:
            b = self.pet.body()
            seen.append(b)
            if until and until(b):
                break
            time.sleep(POLL_S)
        return seen

    def hook(self, payload):
        """Runs the hooks example's commands for this payload, as Claude Code would: every command whose event and
        matcher fit it (the matcher is tested against the tool name, or a notification's type)."""
        with open(HOOKS_EXAMPLE) as f:
            entries = json.load(f)['hooks'].get(payload['hook_event_name'], [])
        target = payload.get('tool_name', payload.get('notification_type', ''))
        for entry in entries:
            if 'matcher' in entry and not re.search(entry['matcher'], target):
                continue
            for h in entry['hooks']:
                r = subprocess.run(h['command'], shell=True, input=json.dumps(payload).encode(), capture_output=True,
                                   env=self.sim.env(), timeout=5)
                check(r.returncode == 0, f'the hook for {payload["hook_event_name"]} exits 0')


def discover(sim):
    """Finds the simulator by Bonjour; returns its URL, or '' if it is not found."""
    found = subprocess.run([os.path.join(BUILD, 'nestbox'), 'find', NAME], capture_output=True, text=True,
                           timeout=15, env=sim.env()).stdout
    line = next((l for l in found.splitlines() if l.startswith(NAME)), '')
    url = next((w for w in line.split() if w.startswith('http://')), '')
    check('id=sim-' in line, f'it announces an id to pair with ({line.strip()[:80]})')
    check(url.endswith(f':{sim.port}') and '.local' in url, f'Bonjour finds it by name: {url or "nothing"}')
    return url


def reading(t):
    status, page = t.pet.call('GET', '/api')
    check(status == 200 and 'POST /v1/tell' in page and '/v1/tell -d' in page, 'GET /api serves the help page')
    status, page = t.pet.call('GET', '/')
    check(status == 200 and '<title>' in page and '/v1/pets' in page, 'GET / serves its own page')
    status, guide = t.pet.call('GET', '/agents')
    check(status == 200 and f':{t.sim.port}/v1/tell' in guide and 'Privacy' in guide and 'asking' in guide,
          'GET /agents: the guide, with this pet\'s address')
    words = t.pet.get_json('/v1/words')
    check('working' in words['states'] and 'throw' in words['behaviours'] and words['limits']['request'] == 1024,
          'GET /v1/words: the words and the limits')
    pets = t.pet.get_json('/v1/pets')
    check(pets['out'] == 'nibble' and len(pets['pets']) >= 2, f'GET /v1/pets: Nibble is out ({pets["out"]})')
    nibble = t.pet.get_json('/v1/pets/nibble')
    check('home' in nibble['places'] and 'target' in nibble['places'] and len(nibble['view']) == 4,
          'GET /v1/pets/nibble: its places and view')
    now = t.pet.now()
    check(now['device']['id'].startswith('sim-') and now['device']['tell'] == 'open', 'GET /v1: the device')


def typing(t):
    t.mac.start()
    t.mac.keys = 3
    seen = t.watch(12, until=lambda b: b['look'] == 'type' and b['phase'] == 'acting')
    check(seen[-1]['look'] == 'type' and seen[-1]['phase'] == 'acting', 'keys: it works at its desk')
    check(any(b['phase'] == 'walking' for b in seen) or seen[0]['look'] == 'type', 'it walked there first')
    check(t.pet.now()['user'] == 'typing', 'GET /v1 says the user is typing')
    t.mac.keys = 12
    seen = t.watch(8, until=lambda b: b['look'] == 'type_fast')
    check(seen[-1]['look'] == 'type_fast', 'typing fast: it works hard')
    t.mac.keys = 0
    seen = t.watch(4, until=lambda b: b['why'] == 'the user paused typing')
    check(seen[-1]['look'] in ('type', 'type_fast'), f'a pause keeps it at its desk ({seen[-1]["why"]})')
    seen = t.watch(14, until=lambda b: b['look'] not in ('type', 'type_fast'))
    check(seen[-1]['look'] not in ('type', 'type_fast'), f'then it lets go ({seen[-1]["look"]})')


def away_and_gone(t):
    t.mac.idle, t.mac.keys = 400, 0
    seen = t.watch(10, until=lambda b: b['look'] == 'sleep')
    check(any(b['look'] == 'yawn' for b in seen) and seen[-1]['look'] == 'sleep', 'away: it yawns, then sleeps')
    t.mac.idle = 0
    seen = t.watch(8, until=lambda b: b['look'] not in ('sleep', 'wake'))
    check(any(b['look'] == 'wake' for b in seen), 'back: it wakes up first')
    t.mac.state = 'locked'
    seen = t.watch(10, until=lambda b: b['look'] == 'sleep')
    check(seen[-1]['look'] == 'sleep' and seen[-1]['why'] == 'the screen is locked', 'locked: it sleeps')
    t.mac.state = 'awake'
    t.watch(8, until=lambda b: b['look'] not in ('sleep', 'wake'))
    t.mac.on = False
    seen = t.watch(12, until=lambda b: b['why'] == 'the computer is asleep or out of reach')
    check(seen[-1]['why'] == 'the computer is asleep or out of reach', 'no reports for twice "next": it sleeps')
    t.mac.on = True
    t.watch(8, until=lambda b: b['look'] not in ('sleep', 'wake', 'yawn'))


def claude_code(t):
    # The usage script with saved usage, told not to ask again for an hour: the hooks show it without running the usage command.
    home = os.path.join(t.sim.env()['HOME'], '.nestbox')
    os.makedirs(home, exist_ok=True)
    later = int(time.time()) + 3600
    with open(os.path.join(home, 'claude-usage.json'), 'w') as f:
        json.dump({'five_hour': {'used_percentage': 23.4, 'resets_at': later},
                   'seven_day': {'used_percentage': 91.2, 'resets_at': later}}, f)
    with open(os.path.join(home, 'claude-usage.next'), 'w') as f:
        f.write(f'{later} 0\n')
    shutil.copy(os.path.join(os.path.dirname(HOOKS_EXAMPLE), 'claude-usage.sh'), home)
    base = {'session_id': SESSION, 'transcript_path': '/home/someone/.claude/x.jsonl', 'cwd': '/home/someone/p'}
    t.hook({**base, 'hook_event_name': 'UserPromptSubmit', 'prompt': 'please refactor the secret module'})
    n = t.pet.note('claude', SESSION[:8])
    check(n and n['state'] == 'working' and n['line'] == 'p: thinking', 'a prompt: working, the project and "thinking"')
    five, week = t.pet.note('claude', 'usage/5h'), t.pet.note('claude', 'usage/wk')
    check(five and week and five['showing'] == ['gauge'] and week['showing'] == ['gauge'],
          'the plan\'s usage: two gauges')
    t.hook({**base, 'hook_event_name': 'PreToolUse', 'tool_name': 'Edit',
            'tool_input': {'file_path': '/home/someone/p/secret/app.ts', 'old_string': 'SECRET'}})
    n = t.pet.note('claude', SESSION[:8])
    check(n['line'] == 'p: editing app.ts', f'a tool: the project and the file\'s name only ({n["line"]})')
    t.hook({**base, 'hook_event_name': 'PreToolUse', 'tool_name': 'Bash',
            'tool_input': {'command': 'make secret', 'description': 'Build the app'}})
    n = t.pet.note('claude', SESSION[:8])
    check(n['line'] == 'p: Build the app' and 'make secret' not in json.dumps(n),
          f'a command: Claude\'s description of it, never the command line ({n["line"]})')
    t.hook({**base, 'hook_event_name': 'PermissionRequest', 'tool_name': 'Bash', 'tool_input': {'command': 'rm -rf x'}})
    now = t.pet.now()
    check(now['now'].get('say') == 'p: Allow Bash?' and now['now']['doing'] == 'wave', 'permission: it waves and asks')
    check('rm -rf' not in json.dumps(now), 'the command line never reaches the pet')
    t.hook({**base, 'hook_event_name': 'PostToolUse', 'tool_name': 'Bash'})
    check(t.pet.note('claude', SESSION[:8])['state'] == 'working', 'the tool ran: the question is gone')
    t.hook({**base, 'hook_event_name': 'SubagentStart', 'agent_id': 'a41cdeadbeef0001', 'agent_type': 'Explore'})
    part = t.pet.note('claude', SESSION[:8] + '/a41cdead')
    check(part and part['line'] == 'p (Explore): starting', 'a sub-agent is a part of the session, named by its type')
    t.hook({**base, 'hook_event_name': 'Stop'})
    check(t.pet.note('claude', 'usage/5h') is None, 'Claude stops: the gauges go')
    n = t.pet.note('claude', SESSION[:8])
    check(n['state'] == 'done' and n.get('line') == 'p: done' and 'waiting for 1 part' in n.get('why', ''),
          'done, with the project; it waits for the sub-agent')
    t.hook({**base, 'hook_event_name': 'SubagentStop', 'agent_id': 'a41cdeadbeef0001', 'agent_type': 'Explore'})
    seen = t.watch(3, until=lambda b: b['look'] == 'celebrate')
    check(seen[-1]['look'] == 'celebrate', 'the sub-agent stops: it celebrates')
    t.hook({**base, 'hook_event_name': 'Notification', 'notification_type': 'idle_prompt', 'message': 'waiting'})
    check(t.pet.note('claude') is not None, 'idle: "your turn", for the sender')
    t.hook({**base, 'hook_event_name': 'UserPromptSubmit', 'prompt': 'which one?'})
    t.hook({**base, 'hook_event_name': 'PermissionRequest', 'tool_name': 'AskUserQuestion',
            'tool_input': {'questions': [{'question': 'Use the secret module?'}]}})
    now = t.pet.now()
    check(now['now'].get('say') == 'p: Claude has a question' and 'secret' not in json.dumps(now),
          'Claude\'s own question: fixed words, never the question')
    t.hook({**base, 'hook_event_name': 'PostToolUse', 'tool_name': 'AskUserQuestion'})
    t.hook({**base, 'hook_event_name': 'PostToolUse', 'tool_name': 'mcp__ccd_turn__wrap_up',
            'tool_input': {'recap': 'All done.'}})
    check(t.pet.note('claude', SESSION[:8] + '/ask') is None, 'a turn that ends without a question asks nothing')
    t.hook({**base, 'hook_event_name': 'PostToolUse', 'tool_name': 'mcp__ccd_turn__wrap_up',
            'tool_input': {'recap': 'Three samples.', 'ask': 'Which secret look?', 'blocked': True}})
    t.hook({**base, 'hook_event_name': 'Stop'})
    now = t.pet.now()
    check(t.pet.note('claude', SESSION[:8])['state'] == 'done' and now['now'].get('say') == 'p: Claude has a question'
          and now['now']['doing'] == 'wave', 'a turn that ends with a question: done, and it still waves and asks')
    t.hook({**base, 'hook_event_name': 'UserPromptSubmit', 'prompt': 'the first'})
    check(t.pet.note('claude', SESSION[:8] + '/ask') is None, 'the answer: the question is gone')
    t.hook({**base, 'hook_event_name': 'SessionEnd', 'reason': 'other'})
    check(t.pet.note('claude', SESSION[:8]) is None, 'the session ends: its notes go')
    t.pet.tell({'from': 'claude', 'state': 'over'})


def replies_and_lifetimes(t):
    status, r = t.pet.tell({'from': 'ci', 'id': 'b1', 'state': 'working', 'line': 'building main', 'gauge': 40})
    check(status == 200 and r['note']['showing'] == ['do', 'line', 'gauge'] and r['note']['ends_in'] == 900,
          f'a working note: what shows and when it ends ({r["note"]})')
    status, r = t.pet.tell({'from': 'ci', 'id': 'b1', 'state': 'failed'})
    check(r['now']['doing'] == 'sulk' and r['now'].get('line') == 'ci failed', 'failed: it sulks, a red line')
    status, r = t.pet.tell({'from': 'ci', 'state': 'over'})
    check(r['ended'] == 1, 'over without an id ends all of the sender\'s notes')
    status, r = t.pet.tell({'from': 'x', 'state': 'running', 'colour': 'red', 'say': 'hi'})
    check(status == 200 and len(r['warnings']) == 2, 'bad input: the sensible part, and a warning each')
    check(t.pet.post('/v1/tell', raw=b'{"say":"' + b'a' * 1100 + b'"}')[0] == 413, 'over 1024 bytes: 413')
    check(t.pet.post('/v1/tell', raw=b'not json')[0] == 400, 'not JSON: 400')
    check(t.pet.post('/v1/nope', {})[0] == 404, 'an unknown route: 404')
    check(t.pet.call('GET', '/v1/tell')[0] == 405, 'the wrong method: 405')
    t.pet.tell({'from': 'x', 'state': 'over'})


def steps_and_throws(t):
    status, r = t.pet.tell({'from': 'me', 'do': [{'at': 'target'}, {'do': 'throw', 'times': 2}, {'at': 'home'}],
                            'say': 'pebbles'})
    check(status == 200 and 'do' in r['note']['showing'], 'a sequence is taken')
    check(t.pet.body()['steps_left'] >= 2, 'the steps left are counted')
    seen = t.watch(40, until=lambda b: b['steps_left'] == 0 and b['look'] != 'do')
    check(any('Flick' in b['playing'] for b in seen), 'Nibble throws its pebble')
    check(seen[-1]['steps_left'] == 0, 'and the steps end')
    status, r = t.pet.tell({'from': 'me', 'do': 'dance', 'at': [45, 75], 'for': '3s'})
    seen = t.watch(20, until=lambda b: b['phase'] == 'walking')
    t.watch(20, until=lambda b: b['look'] == 'do' and b['phase'] == 'acting')
    now = t.pet.now()['now']
    check(seen[-1]['phase'] == 'walking', 'it walks there first')
    at = now['at'] if isinstance(now['at'], list) else [-100, -100]
    check(now['doing'] == 'dance' and abs(at[0] - 45) < 6 and abs(at[1] - 75) < 6,
          f'a behaviour at a point in the scene ({now["doing"]} at {now["at"]})')
    t.pet.tell({'from': 'me', 'state': 'over'})


def quiet_and_the_hand(t):
    t.pet.tell({'from': 'cal', 'state': 'quiet', 'for': '45m', 'gauge': 'timer', 'line': 'in a meeting'})
    status, r = t.pet.tell({'from': 'deploy', 'say': 'shipped'})
    check(r['note'].get('why') == 'quiet is on, so this was skipped', 'quiet: a passing moment is skipped')
    status, r = t.pet.tell({'from': 'door', 'state': 'asking', 'say': 'Someone is at the door', 'for': '2m'})
    check(r['now']['doing'] == 'wave' and r['now']['say'] == 'Someone is at the door', 'a question gets through')
    check(t.pet.input({'press': 'single'})[1]['input'] == 'seen', 'a tap: seen')
    n = t.pet.note('door')
    check('line' in n['showing'] and 'say' not in n['showing'], 'the question keeps its line, not its bubble')
    check(t.pet.input({'press': 'double'})[1]['input'] == 'dismissed', 'a double press dismisses')
    check(t.pet.note('door')['showing'] == [], 'the question is hidden')
    t.pet.tell({'from': 'cal', 'state': 'over'})
    t.pet.tell({'from': 'door', 'state': 'over'})
    check(t.pet.input({'press': 'long'})[1]['input'] == 'quiet on', 'a long press: quiet on')
    check(t.pet.input({'press': 'long'})[1]['input'] == 'quiet off', 'again: quiet off')
    check(t.pet.input({'gesture': 'shake'})[1]['input'] == 'dizzy', 'a shake: dizzy')


def the_device(t):
    status, r = t.pet.post('/v1', {'pet': 'gander'})
    check(status == 200 and r['now']['pet'] == 'gander', f'POST /v1 brings out another pet ({r.get("now")})')
    status, r = t.pet.post('/v1', {'pet': 'nibble'})
    status, r = t.pet.post('/v1', {'time': '23:30'})
    check(r.get('time') == '23:30', 'forcing the time of day')
    t.watch(3)
    check(t.pet.body()['daypart'] == 'night', 'the scene follows it')
    t.pet.post('/v1', {'time': 'auto'})
    status, r = t.pet.post('/v1', {'text': 'large'})
    check(status == 200 and t.pet.now()['device']['text'] == 'large', 'text size')
    t.pet.post('/v1', {'text': 'medium'})


def pairing(t):
    status, r = t.pet.post('/v1/pair', {'name': 'e2e'})
    check(r.get('pairing') == 'waiting', 'pairing waits for the button')
    check('Pair with e2e' in t.pet.now()['now'].get('caption', ''), 'the pet asks')
    t.pet.input({'press': 'single'})
    status, r = t.pet.post('/v1/pair', {'name': 'e2e'})
    check(r.get('pairing') == 'done' and r.get('key', '').startswith('nb_'), 'after the button: the key')
    check(t.pet.now()['device']['paired'], 'GET /v1 says it is paired')


def real_companion(t, beats=3, timeout=10):
    """Runs the nestbox command against the pet until it has posted `beats` reports."""
    t.mac.on = False
    companion = subprocess.Popen([os.path.join(BUILD, 'nestbox'), '--device', t.url, '--print'],
                                 stdout=subprocess.PIPE, text=True, env=t.sim.env())
    sent, end = [], time.time() + timeout
    with selectors.DefaultSelector() as sel:
        sel.register(companion.stdout, selectors.EVENT_READ)
        while len(sent) < beats and time.time() < end:
            if sel.select(timeout=max(0, end - time.time())):
                line = companion.stdout.readline()
                if not line:
                    break
                if line.startswith('{"from": '):
                    sent.append(line.rstrip())
    companion.terminate()
    companion.communicate(timeout=5)
    check(len(sent) >= 2 and all('-> 200' in l for l in sent), f'nestbox reports to /v1/user ({len(sent)}, all 200)')
    check(all('"next": 10' in l and '"time": "' in l for l in sent), 'each report promises the next and gives the time')
    if sent:
        print('       ' + sent[-1][:150])


def connections(t):
    host, port = t.url.split('//')[1].split(':')
    kept = http.client.HTTPConnection(host, int(port), timeout=3)
    socks = []
    for _ in range(3):
        kept.request('GET', '/v1')
        r = kept.getresponse()
        r.read()
        socks.append(kept.sock)
    check(r.status == 200 and r.getheader('Connection') == 'keep-alive' and socks[0] is socks[2],
          'a connection stays open for the next request')
    kept.request('POST', '/v1/user', body=json.dumps({'from': 'e2e-kept', 'keys': 0}),
                 headers={'Content-Type': 'application/json'})
    r = kept.getresponse()
    r.read()
    check(r.status == 200 and kept.sock is socks[0], 'a POST on it too')
    other = http.client.HTTPConnection(host, int(port), timeout=3)
    other.request('GET', '/v1', headers={'Connection': 'close'})
    r = other.getresponse()
    r.read()
    check(r.status == 200 and r.getheader('Connection') == 'close', 'another client is served while one stays open')
    other.close()
    kept.close()


SCENARIOS = [
    ('reading: the help, the guide, the words, the pets, the device', reading),
    ('typing: walks to the desk, works, works hard, pauses, lets go', typing),
    ('away, locked, and a computer that is gone', away_and_gone),
    ('Claude Code, through the hooks example', claude_code),
    ('replies and lifetimes', replies_and_lifetimes),
    ('steps and throws', steps_and_throws),
    ('quiet and the user\'s hand', quiet_and_the_hand),
    ('changing the device', the_device),
    ('pairing', pairing),
    ('the real companion', real_companion),
    ('connections kept open', connections),
]


def main():
    sim = Simulator(BUILD, NAME, speed=SPEED)
    try:
        print('discovery')
        url = discover(sim)
        if not url:
            url = f'http://127.0.0.1:{sim.port}'
            print(f'  Bonjour did not find it: the rest runs against {url}')
        t = Session(sim, url)
        for title, scenario in SCENARIOS:
            print(title)
            scenario(t)
    finally:
        log = sim.stop()
        print('\nsimulator log:\n' + '\n'.join('  ' + l for l in log.splitlines()[-60:]))
    finish()


if __name__ == '__main__':
    main()
