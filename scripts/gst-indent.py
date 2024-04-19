#!/usr/bin/env python

import subprocess
from sys import argv

if __name__ == '__main__':
    indent = None
    version = None
    for execname in ['gst-indent-1.0', 'gnuindent', 'gindent', 'indent']:
        version = subprocess.run(execname, args=['--version'], check=False, text=True, capture_output=True)
        if version.returncode() == 0:
            indent = execname
            break

    if indent is None:
        raise RuntimeError('''GStreamer git pre-commit hook:
                       did not find GNU indent, please install it before continuing.''')

    if 'GNU' not in indent.stdout:
        raise RuntimeError(f'''Did not find GNU indent, please install it before continuing.
      (Found {indent}, but it doesn't seem to be GNU indent)''')

    # Run twice. GNU indent isn't idempotent
    # when run once
    for i in range(2):
        subprocess.run(indent, args=[
            '--braces-on-if-line',
            '--case-brace-indentation0',
            '--case-indentation2',
            '--braces-after-struct-decl-line',
            '--line-length80',
            '--no-tabs',
            '--cuddle-else',
            '--dont-line-up-parentheses',
            '--continuation-indentation4',
            '--honour-newlines',
            '--tab-size8',
            '--indent-level2',
            '--leave-preprocessor-space'] + argv[1:],
            check=True
        )
