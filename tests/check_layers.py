"""The engine is built in layers, each using only those before it: util, screen, egg, body, mind, door, host.
Fails, naming the file and line, if any engine file includes a header from a higher layer.

    python3 tests/check_layers.py
"""
import os
import re
import sys

LAYERS = ['util', 'screen', 'egg', 'body', 'mind', 'door', 'host']
ENGINE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'engine')
INCLUDE = re.compile(r'\s*#include "([a-z]+)/')


def upward_includes(layer):
    folder = os.path.join(ENGINE, layer)
    if not os.path.isdir(folder):
        yield f'engine/{layer}/ is missing'
        return
    for name in sorted(os.listdir(folder)):
        if not name.endswith(('.h', '.cpp')):
            continue
        with open(os.path.join(folder, name), encoding='utf-8') as f:
            for n, line in enumerate(f, 1):
                m = INCLUDE.match(line)
                if m and m.group(1) in LAYERS and LAYERS.index(m.group(1)) > LAYERS.index(layer):
                    yield f'engine/{layer}/{name}:{n} includes {m.group(1)}/ (a higher layer)'


def main():
    bad = [problem for layer in LAYERS for problem in upward_includes(layer)]
    bad += [f'engine/{d}/ is in no layer (add it to LAYERS)' for d in sorted(os.listdir(ENGINE))
            if d not in LAYERS and os.path.isdir(os.path.join(ENGINE, d))]
    for b in bad:
        print('FAIL  ' + b)
    print(f'{len(LAYERS)} layers, ' + ('upward includes found' if bad else 'nothing includes upward'))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
