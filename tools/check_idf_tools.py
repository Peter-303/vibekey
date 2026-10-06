import json
import os

p = os.path.expanduser('~/.espressif/v5.5/esp-idf/tools/tools.json')
d = json.load(open(p, encoding='utf-8'))
print('ESP-IDF v5.5 tools.json 期望的版本：')
for t in d['tools']:
    print('  %-16s %s' % (t['name'], t.get('version', '(any)')))

print()
print('eim 实际装在 C:\\Espressif\\tools 的版本：')
base = r'C:\Espressif\tools'
for name in ('ninja', 'idf-exe', 'ccache', 'dfu-util', 'esp-rom-elfs'):
    d2 = os.path.join(base, name)
    if os.path.isdir(d2):
        subs = [s for s in os.listdir(d2) if os.path.isdir(os.path.join(d2, s))]
        print('  %-16s %s' % (name, subs))
    else:
        print('  %-16s (目录不存在)' % name)
