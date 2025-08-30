'''West commands to format the code.'''

from __future__ import annotations

import argparse
import subprocess
import os
from textwrap import dedent

from west import log
from west.commands import WestCommand
from west.manifest import Manifest, MalformedManifest, ImportFlag, \
    MANIFEST_PROJECT_INDEX, Project, QUAL_MANIFEST_REV_BRANCH

import sys

class Format(WestCommand):
    def __init__(self):
        super().__init__(
            'format',
            'Run clang-format on the codebase',
            dedent('''
            '''))

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name, help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description)
        return parser

    def do_run(self, args: argparse.Namespace, unknown_args: list[str]):
        if subprocess.call("find . -path './build' -prune -o -iname '*.h' -o -iname '*.c' -o -iname '*.hpp' -o -iname '*.cpp' | xargs clang-format -i -verbose", shell=True):
            exit(1)
