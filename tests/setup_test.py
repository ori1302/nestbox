#!/usr/bin/env python3
"""Wi-Fi setup over the air, end to end, against the simulator in setup mode (a pet fresh out of the box with the
fake radio in simulator/sim_radio.h). The test plays the phone: the OS's captive-portal checks, the setup page's
requests, every failure the pet must explain, the handover to home Wi-Fi, and "just be a pet". It reads the pet's
screen (GET /screen.bmp) with macOS's QR reader (build/qr_decode).

Then the rescue, on a pet already on the pretend network Home: its password changes, the network disappears,
BOOT is held to start over, and a computer asks it to forget its Wi-Fi.

    python3 tests/setup_test.py [--build DIR]
"""
import http.client
import json
import os
import re
import subprocess
import tempfile
import time

from harness import Simulator, build_dir, check, finish, wait_for

BUILD = build_dir()
NAME = f'nestbox-setup-sim-{os.getpid()}'
SPEED = 4                                  # the pet's clock runs 4x: a 20 s check takes 5 s here
ALPHABET = set('ABCDEFGHJKLMNPQRSTUVWXYZ23456789')
FRAME_S = 0.12                             # long enough for the pet to draw a frame after a change
HANDOVER_CLOSE_S = 3.0 / SPEED + 0.5       # its own network closes 3 pet-seconds after the handover
PHONE_GONE_S = (10 + 5) / SPEED + 1        # it closes 10 pet-seconds after the last request, checked every 5
bodies = []                                # every text body the pet sent, to look for leaked passwords


class Phone:
    def __init__(self, port, shots):
        self.port = port
        self.shots = shots

    def req(self, method, path, body=None, host=None, raw=False):
        c = http.client.HTTPConnection('127.0.0.1', self.port, timeout=5)
        headers = {'Host': host or f'127.0.0.1:{self.port}'}
        data = None
        if body is not None:
            data = body if isinstance(body, (str, bytes)) else json.dumps(body)
            headers['Content-Type'] = 'application/json'
        c.request(method, path, body=data, headers=headers)
        r = c.getresponse()
        out = r.read()
        c.close()
        if not raw:
            bodies.append(out.decode('utf-8', 'replace'))
        return r.status, (out if raw else out.decode('utf-8', 'replace')), r.getheader('Location')

    def get(self, path, **kw):
        return self.req('GET', path, **kw)

    def post(self, path, body, **kw):
        return self.req('POST', path, body, **kw)

    def status(self):
        s, b, _ = self.get('/setup/status')
        return json.loads(b) if s == 200 else None

    def wait(self, phases, seconds=15):
        def in_phase():
            st = self.status()
            return st if st and st['phase'] in phases else None
        return wait_for(in_phase, seconds, 0.15) or self.status()

    def networks(self, seconds=10):
        def scanned():
            s, b, _ = self.get('/setup/networks')
            return json.loads(b) if s == 200 and not json.loads(b)['scanning'] else None
        return wait_for(scanned, seconds, 0.2)

    def screen_code(self):
        """What the QR code on the pet's screen says, or '' if there is none."""
        time.sleep(FRAME_S)
        _, img, _ = self.get('/screen.bmp', raw=True)
        path = os.path.join(self.shots, 'screen.bmp')
        with open(path, 'wb') as f:
            f.write(img)
        out = subprocess.run([os.path.join(BUILD, 'qr_decode'), path], capture_output=True, text=True).stdout
        found = out.split('\t', 1)[1].strip() if '\t' in out else ''
        return '' if found == 'NO CODE' else found


def start(tag, shots, speed=SPEED, fresh=True):
    sim = Simulator(BUILD, f'{NAME}-{tag}', ['--setup'] if fresh else [], speed=speed)
    return sim, Phone(sim.port, shots)


def wifi_line(phone):
    _, b, _ = phone.get('/v1/debug')
    return json.loads(b)['wifi_line']


def router(phone, state):
    s, _, _ = phone.post('/sim/router', {'router': state})
    check(s == 200, f'the pretend router: {state}')


def setup_open(phone):
    """Its own network is open. A status request counts as a phone being there."""
    return phone.get('/setup/status')[0] == 200


def text_size(phone):
    _, b, _ = phone.get('/v1')
    return json.loads(b)['device']['text']


def wifi_code_shown(phone, seconds):
    return wait_for(lambda: phone.screen_code().startswith('WIFI:') or None, seconds, 0.05)


def clock(tz='Asia/Jerusalem', offset=7200):
    """The time fields the setup page sends. Jerusalem's rule uses an hour past 24 (M3.4.4/26)."""
    return {'tz': tz, 'time': int(time.time() * 1000), 'utc_offset': offset}


def says_hello_first(shots):
    sim, phone = start('hello', shots, speed=1)            # real time: the hello lasts 3 s
    try:
        check(phone.screen_code() == '', 'it says hello first, no code yet')
        check(phone.get('/setup/status')[0] == 404, 'its own network is not open while it looks around')
    finally:
        sim.stop()


def shows_wifi_code(phone):
    def wifi_code():
        code = phone.screen_code()
        return code if code.startswith('WIFI:') else ''
    code = wait_for(wifi_code, 8, 0)
    m = re.fullmatch(r'WIFI:T:WPA;S:(NIBBLE-[A-Z0-9]{4});P:([A-Z0-9]{8});;', code)
    check(bool(m), f'the screen shows a Wi-Fi code phones can join: {code}')
    if m:
        check(set(m.group(2)) <= ALPHABET, 'its password has no look-alike letters (0/O, 1/I)')


def lists_networks(phone):
    nets = phone.networks()
    if not check(nets is not None, 'its own network opens and the page gets the list it scanned'):
        return False
    names = [n['ssid'] for n in nets['networks']]
    check(names[0] == 'Home' and names.count('Home') == 1, f'strongest first, one row per name: {names}')
    check('הבית שלנו' in names, 'a Hebrew network name arrives intact')
    check(any(n['ssid'] == 'Office' and n['enterprise'] for n in nets['networks']),
          'a company network is marked as needing a login')
    check(any(n['ssid'] == 'Cafe Free WiFi' and not n['secure'] for n in nets['networks']),
          'an open network is marked as such')
    check(nets['pet'] == 'Nibble', 'the page learns the pet\'s name')
    return True


def captive_portal_leads_to_page(phone):
    for path, host in [('/hotspot-detect.html', 'captive.apple.com'),
                       ('/generate_204', 'connectivitycheck.gstatic.com'),
                       ('/connecttest.txt', 'www.msftconnecttest.com'), ('/', 'goose.com'),
                       ('/some/where', 'example.org')]:
        s, _, loc = phone.get(path, host=host)
        check(s == 302 and loc == 'http://192.168.4.1/', f'{host}{path} -> {s} {loc}')
    s, page, _ = phone.get('/', host='192.168.4.1')
    check(s == 200 and 'Pick the Wi-Fi' in page and '<input id="pass"' in page, 'http://192.168.4.1/ is the setup page')
    s, svg, _ = phone.get('/setup/pet.svg')
    check(s == 200 and svg.startswith('<svg') and svg.count('<rect') > 50,
          f'the page shows the pet itself ({svg.count("<rect")} pixel runs)')
    check(phone.wait(['page'], 3)['phase'] == 'page', 'the pet knows the page is open')


def explains_failures(phone):
    s, _, _ = phone.post('/setup/connect', 'ssid=Home&password=sunflower1')
    check(s == 400, f'form posts are refused, only JSON: {s}')
    s, b, _ = phone.post('/setup/connect', {'ssid': 'Home', 'password': 'short'})
    check(s == 400 and '8 to 63' in b, 'a password under 8 characters is refused before trying')
    s, _, _ = phone.post('/setup/connect', {'ssid': '', 'password': 'whatever12'})
    check(s == 400, 'an empty network name is refused')
    cases = [({'ssid': 'Home', 'password': 'wrongpass1'}, "That password didn't work for Home."),
             ({'ssid': 'Neighbours 2.4', 'password': 'whatever12'}, 'I can barely hear Neighbours 2.4'),
             ({'ssid': 'Office', 'password': 'whatever12'}, 'needs a login'),
             ({'ssid': 'Home Guest', 'password': 'welcome123'}, "didn't give me an address"),
             ({'ssid': 'Attic', 'password': 'hidden1234'}, "I can't see Attic. I need a 2.4 GHz network")]
    for body, words in cases:
        s, _, _ = phone.post('/setup/connect', {**body, **clock()})
        if body['ssid'] == 'Home':
            s2, _, _ = phone.post('/setup/connect', {**body, **clock()})
            check(s2 == 409, 'a second try while it is still checking is refused')
        st = phone.wait(['failed', 'allset'], 12)
        check(s == 200 and st and st['phase'] == 'failed' and words in st['reason'],
              f'{body["ssid"]}: "{st and st["reason"]}"')
        check(phone.screen_code() == '', '  ...and the screen says so (no code on it)')


def joins_and_hands_over(phone):
    phone.post('/setup/connect', {'ssid': 'Home', 'password': 'sunflower1', **clock()})
    check(phone.status()['phase'] == 'checking', 'it checks while the phone stays connected')
    st = phone.wait(['allset', 'failed'], 12)
    check(st['phase'] == 'allset' and st['ok'] and st['ip'] == '192.168.1.37', f'it joined: {st["phase"]} {st["ip"]}')
    check(st['tz'] == 'IST-2IDT,M3.4.4/26,M10.5.0',
          f'the phone\'s time zone became a rule with summer time: {st["tz"]}')
    url = st['url']
    check(re.fullmatch(r'http://192\.168\.1\.37/welcome\?t=[A-Z0-9]{4}', url) is not None,
          f'the handover address carries a one-time key: {url}')
    check(phone.screen_code() == url, 'the screen\'s "All set" code opens that address')
    s, _, _ = phone.post('/setup/connect', {'ssid': 'Home Guest', 'password': 'welcome123', **clock()})
    check(s == 409, 'once set up, the setup page cannot change its Wi-Fi')
    s, b, _ = phone.post('/setup/finish', {'name': 'גוס'})
    j = json.loads(b)
    check(s == 200 and j['url'] == url and j['ping'] == 'http://192.168.1.37/ping.png',
          'finishing returns the address and a ping image')
    s, png, _ = phone.get('/ping.png', raw=True)
    check(s == 200 and png[:8] == b'\x89PNG\r\n\x1a\n', 'the ping image is a real PNG')
    time.sleep(HANDOVER_CLOSE_S)
    check(phone.get('/setup/status')[0] == 404, 'its own network closes a few seconds after the handover')
    s, _, _ = phone.get('/welcome?t=XXXX')
    check(s == 200 and phone.screen_code() == url, 'a wrong key opens the page but leaves "All set" on the screen')
    s, b, _ = phone.get(url.replace('http://192.168.1.37', ''))
    if not check(s == 200 and '<title>גוס</title>' in b, 'the right key opens its page, in the new name'):
        print(f'       {s} ' + re.sub(r'<[^>]+>', ' ', b)[:300])
    check(phone.screen_code() == '', '...and the screen goes back to the pet')
    _, b, _ = phone.get('/api')
    check(b.startswith('NestBox'), 'its address serves the everyday API')
    s, _, loc = phone.get('/hotspot-detect.html', host='captive.apple.com')
    check(s == 403 and not loc, 'no captive portal once its network is closed: a foreign name is refused')


def sets_up_home_wifi(shots):
    sim, phone = start('a', shots)
    try:
        shows_wifi_code(phone)
        if not lists_networks(phone):
            return
        print('the phone joins: its captive-portal check, and any address, lead to the page')
        captive_portal_leads_to_page(phone)
        print('it explains every failure, and keeps nothing it typed')
        explains_failures(phone)
        print('the right password')
        joins_and_hands_over(phone)
    finally:
        log = sim.stop()
    check('own network NIBBLE-' in log and 'on channel 6' in log,
          'its own network opened on the strongest network\'s channel (6)')
    check(log.count('setup: saved') == 1 and 'setup: saved Home' in log, 'only the network that worked was saved, once')
    check(all(w not in log for w in ('sunflower1', 'wrongpass1', 'welcome123')), 'no Wi-Fi password reaches the log')
    check(all('sunflower1' not in b for b in bodies), 'no Wi-Fi password is sent back')


def lives_offline(shots):
    sim, phone = start('b', shots)
    try:
        phone.networks()
        s, _, _ = phone.post('/setup/offline', clock('Mars/Olympus', 19800))
        st = phone.status()
        check(s == 200 and st['offline'] and st['phase'] == 'off', 'it lives offline')
        check(st['tz'] == '<+0530>-5:30', f'an unknown zone falls back to the phone\'s own offset: {st["tz"]}')
        time.sleep(HANDOVER_CLOSE_S)
        check(phone.get('/setup/status')[0] == 404 and phone.screen_code() == '',
              'its network closes and the pet comes out')
    finally:
        log = sim.stop()
    check('offline = 1' in log, 'it remembers to stay offline')


def open_network_tap_and_phone_leaving(shots):
    sim, phone = start('c', shots)
    try:
        phone.networks()
        phone.post('/setup/connect', {'ssid': 'Cafe Free WiFi', 'password': '', **clock()})
        st = phone.wait(['allset', 'failed'], 12)
        check(st['phase'] == 'allset' and st['ip'] == '10.0.0.23', 'an open network needs no password')
        _, b, _ = phone.post('/v1/input', {'gesture': 'tap'})
        check('setup closed' in b and phone.screen_code() == '', 'a tap closes "All set"')
        time.sleep(PHONE_GONE_S)                             # no requests meanwhile: the phone has gone
        check(phone.get('/setup/status')[0] == 404, 'its network closes 10 s after the phone leaves')
    finally:
        sim.stop()


def rescues_a_changed_password(shots):
    speed = 30                                             # 2 minutes take 4 s
    sim, phone = start('d', shots, speed=speed, fresh=False)
    try:
        check(not setup_open(phone) and wifi_line(phone) == '', 'on Home: no setup, nothing to say')
        router(phone, 'password')
        line = wait_for(lambda: wifi_line(phone) or None, 3, 0.1)
        check(line == 'No Wi-Fi' and not setup_open(phone), f'after half a minute it says so, quietly: {line}')
        line = wait_for(lambda: 'Tap me' in wifi_line(phone) and wifi_line(phone), 6, 0.1)
        check(line == 'Wi-Fi password changed? Tap me', f'after 2 minutes refused, it offers setup: {line}')
        check(not setup_open(phone), '...without opening its own network yet')
        _, b, _ = phone.post('/v1/input', {'gesture': 'tap'})
        check('setup opened' in b and wifi_code_shown(phone, 3), 'a tap opens setup: its code is on the screen')
        check(phone.status()['again'], '...and the page knows it is setup again')
        phone.networks()
        phone.post('/setup/connect', {'ssid': 'Home', 'password': 'sunflower2', **clock()})
        st = phone.wait(['allset', 'failed'], 5)
        check(st['phase'] == 'allset', f'the new password works: {st["phase"]}')
        phone.post('/setup/finish', {})
        time.sleep(3 / speed + 0.3)                        # its network closes 3 pet-seconds after finishing
        check(not setup_open(phone) and wifi_line(phone) == '', 'back on Home: setup closes and the line goes')
        s, b, _ = phone.post('/v1', {'wifi': 'forget'})
        check(s == 200 and json.loads(b)['ok'], 'a computer can ask it to forget its Wi-Fi')
        st = phone.wait(['hello', 'join', 'joined'], 3)
        check(st and st['phase'] in ('hello', 'join', 'joined') and st['again'], f'...and setup starts again: {st}')
    finally:
        log = sim.stop()
    check('stops rejoining Home' in log and 'rejoins Home' in log, 'it stops rejoining Home while setup runs')
    check('saved Home' in log and 'forgot Home' in log, 'the new password was saved, then forgotten on request')
    check('sunflower2' not in log, 'no Wi-Fi password reaches the log')


def rescue_waits_for_a_lost_network(shots):
    speed = 60                                             # 10 minutes take 10 s
    sim, phone = start('e', shots, speed=speed, fresh=False)
    try:
        router(phone, 'gone')
        time.sleep(150 / speed)
        check(wifi_line(phone) == 'No Wi-Fi', 'a network gone for 2 minutes is only "No Wi-Fi" (a router restarting)')
        router(phone, 'ok')
        check(wait_for(lambda: wifi_line(phone) == '' or None, 2, 0.05), 'the network comes back: the line goes')
        router(phone, 'gone')
        line = wait_for(lambda: 'Tap me' in wifi_line(phone) and wifi_line(phone), 14, 0.2)
        check(line == 'No Wi-Fi. Tap me to set it up', f'gone for 10 minutes, it offers setup: {line}')
        phone.post('/v1/input', {'press': 'single'})
        check(wifi_code_shown(phone, 2), 'a press opens setup')
        check(wait_for(lambda: 'Tap me' in wifi_line(phone) or None, 8, 0.2),
              'nobody comes for 5 minutes: setup closes and the offer is back')
        check(not setup_open(phone), '...with its own network closed')
        router(phone, 'ok')
        check(wait_for(lambda: wifi_line(phone) == '' or None, 2, 0.05), 'the network comes back: the offer goes')
    finally:
        sim.stop()


def boot_starts_over(shots):
    sim, phone = start('f', shots, speed=SPEED, fresh=False)
    try:
        phone.post('/v1/input', {'boot': 'down'})
        time.sleep(3 / SPEED)
        phone.post('/v1/input', {'boot': 'up'})
        time.sleep(3 / SPEED)
        check(not setup_open(phone), 'BOOT let go after 3 s: nothing forgotten')
        phone.post('/v1/input', {'boot': 'down'})
        time.sleep(7 / SPEED)
        check(phone.screen_code() == '' and not setup_open(phone), 'held 7 s: a countdown, not setup yet')
        phone.post('/v1/input', {'boot': 'up'})
        check(wifi_code_shown(phone, 4 / SPEED + 2) and phone.status()['again'], 'let go after 5 s: setup starts again')
        s, _, _ = phone.post('/v1/input', {'boot': 'sideways'})
        check(s == 400, 'boot is down or up')
    finally:
        log = sim.stop()
    check(log.count('forgot Home') == 1, 'Home was forgotten once')
    check('reset everything' not in log, 'nothing else was reset')


def boot_resets_everything(shots):
    sim, phone = start('f', shots, speed=SPEED, fresh=False)
    try:
        phone.post('/v1', {'text': 'large'})
        check(text_size(phone) == 'large', 'a setting changed first (text size)')
        phone.post('/v1/input', {'boot': 'down'})
        time.sleep(15 / SPEED)
        check(not setup_open(phone), 'held 15 s: still counting down')
        check(wifi_code_shown(phone, 10 / SPEED + 2), 'held 20 s: setup starts as on a new pet')
        phone.post('/v1/input', {'boot': 'up'})
        time.sleep(1 / SPEED)
        check(setup_open(phone), 'letting go after the reset changes nothing')
        check(text_size(phone) == 'medium', 'the setting is back to new')
    finally:
        log = sim.stop()
    check(log.count('reset everything but the pets') == 1, 'everything but the pets was reset once')
    check('forgot Home' not in log, 'not a plain start-over as well')


def main():
    if not os.path.exists(os.path.join(BUILD, 'qr_decode')):
        check(False, f'{BUILD}/qr_decode is built (macOS only)')
        finish()
    with tempfile.TemporaryDirectory(prefix='nestbox-setup-shots-') as shots:
        print('a pet fresh out of the box')
        says_hello_first(shots)
        sets_up_home_wifi(shots)
        print('"just be a pet", in a time zone the pet does not know')
        lives_offline(shots)
        print('an open network, a tap, and the phone simply leaving')
        open_network_tap_and_phone_leaving(shots)
        print('rescue: Home\'s password changes')
        rescues_a_changed_password(shots)
        print('rescue: Home disappears, for a while and then for good')
        rescue_waits_for_a_lost_network(shots)
        print('starting over with BOOT')
        boot_starts_over(shots)
        print('resetting everything with BOOT')
        boot_resets_everything(shots)
    finish()


if __name__ == '__main__':
    main()
