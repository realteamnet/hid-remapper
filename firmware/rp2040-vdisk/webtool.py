#!/usr/bin/env python3

# Makes one self-contained HTML file out of config-tool-web, so that it works
# when opened from a drive (file://): module imports and CDN links are inlined.

import base64
import hashlib
import re
import sys
import urllib.request

src, out = sys.argv[1], sys.argv[2]


def read(name):
    with open(src + '/' + name, encoding='utf-8') as f:
        return f.read()


def fetch(match):
    url = match.group('url')
    try:
        data = urllib.request.urlopen(url, timeout=30).read()
        digest = base64.b64encode(hashlib.sha384(data).digest()).decode()
        if 'sha384-' + digest != match.group('integrity'):
            raise ValueError('integrity mismatch')
        return data.decode('utf-8')
    except Exception as e:
        print('webtool.py: keeping the link to %s (%s)' % (url, e), file=sys.stderr)
        return None


def inline_css(match):
    text = fetch(match)
    return match.group(0) if text is None else '<style>' + text + '</style>'


def script(text, attrs=''):
    return '<script' + attrs + '>' + text.replace('</script', '<\\/script') + '</script>'


def inline_js(match):
    text = fetch(match)
    return match.group(0) if text is None else script(text)


modules = '\n'.join([
    read('crc.js').replace('export default function', 'function'),
    re.sub(r'^export default \w+;\s*$', '', read('usages.js'), flags=re.M),
    re.sub(r'^export default \w+;\s*$', '', read('examples.js'), flags=re.M),
    re.sub(r'^import .*$', '', read('code.js'), flags=re.M),
])
assert not re.search(r'^(import|export) ', modules, flags=re.M)

html = read('index.html')
html, n = re.subn(r'<link href="(?P<url>https://[^"]+\.css)" rel="stylesheet" integrity="(?P<integrity>[^"]+)"[^>]*>',
                  inline_css, html)
assert n == 1
html, n = re.subn(r'<script src="(?P<url>https://[^"]+\.js)" integrity="(?P<integrity>[^"]+)"[^>]*></script>',
                  inline_js, html)
assert n == 1
html, n = re.subn(r'<script src="code.js" type="module"></script>',
                  lambda m: script(modules, ' type="module"'), html)
assert n == 1

with open(out, 'w', encoding='utf-8', newline='\n') as f:
    f.write(html)
