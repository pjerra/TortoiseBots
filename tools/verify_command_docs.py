#!/usr/bin/env python3
"""Every typed .bot command of the Forever build must appear in the command docs.

Reads the verbs the player command handler answers (forever/src/host/player/TbPlayerCmd.cpp,
the `verb == "<name>"` tests) and requires each one to show up in
docs/play/commands/README.md, either as `.bot <name>` or as an alias written next to it.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'forever' / 'src' / 'host' / 'player' / 'TbPlayerCmd.cpp'
DOC = ROOT / 'docs' / 'play' / 'commands' / 'README.md'


def verb_names():
    return sorted(set(re.findall(r'verb == "([^"]+)"', SOURCE.read_text())))


def main():
    names = verb_names()
    if not names:
        print('verify_command_docs: no verbs found in TbPlayerCmd.cpp')
        return 1
    rows = [line for line in DOC.read_text().splitlines() if '.bot ' in line]
    missing = [name for name in names
               if not any(re.search(r'(\.bot |`)' + re.escape(name) + r'\b', row) for row in rows)]
    if missing:
        print('verify_command_docs: commands missing from docs/play/commands/README.md:')
        for name in missing:
            print(f'  .bot {name}')
        return 1
    print(f'verify_command_docs: all {len(names)} .bot verbs documented.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
