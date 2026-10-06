"""C 源码静态结构检查（本机无 C 编译器时的替代验证）。

检查项：
  1. 括号配平
  2. 头文件声明的函数在对应 .c 中都有定义
  3. app/ 未误用 ESP-IDF 头文件（ADR-1 约束）
  4. test/ 的 include 均可解析

注意：这不能替代真实编译，只能排除结构性错误。
"""
import glob
import os
import re
import sys

ROOT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.getcwd()
APP = os.path.join(ROOT, 'main', 'app')
TEST = os.path.join(ROOT, 'test')

ESPIDF_PREFIXES = (
    'esp_', 'driver/', 'freertos/', 'nvs_', 'esp_wifi',
    'esp_timer', 'tinyusb', 'mbedtls', 'esp_hw_support',
)


def strip_code(src):
    """去掉注释和字符串字面量，避免括号统计误判。"""
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ''
        if c == '/' and nxt == '*':
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
            out.append(' ')
        elif c == '/' and nxt == '/':
            j = src.find('\n', i)
            i = n if j < 0 else j
            out.append(' ')
        elif c == '"' or c == "'":
            quote = c
            i += 1
            while i < n:
                if src[i] == '\\':
                    i += 2
                    continue
                if src[i] == quote:
                    i += 1
                    break
                i += 1
            out.append('""')
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def main():
    problems = []
    cfiles = sorted(glob.glob(os.path.join(APP, '*.c')))
    cfiles += sorted(glob.glob(os.path.join(TEST, '*.c')))

    print('=== 1. 括号配平 ===')
    for f in cfiles:
        code = strip_code(open(f, encoding='utf-8').read())
        bad = []
        for op, cl, name in (('{', '}', '花'), ('(', ')', '圆'), ('[', ']', '方')):
            d = code.count(op) - code.count(cl)
            if d != 0:
                bad.append('%s括号差 %+d' % (name, d))
        rel = os.path.relpath(f, ROOT)
        if bad:
            problems.append('%s: %s' % (rel, ', '.join(bad)))
            print('  BAD %s: %s' % (rel, ', '.join(bad)))
        else:
            print('  OK  %s' % rel)

    print()
    print('=== 2. 头文件声明 vs .c 定义 ===')
    for h in sorted(glob.glob(os.path.join(APP, '*.h'))):
        base = os.path.splitext(os.path.basename(h))[0]
        cfile = os.path.join(APP, base + '.c')
        if not os.path.exists(cfile):
            continue
        hsrc = strip_code(open(h, encoding='utf-8').read())
        csrc = strip_code(open(cfile, encoding='utf-8').read())
        decls = set()
        for m in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*;', hsrc):
            decls.add(m.group(1))
        defs = set(re.findall(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*\{', csrc))
        keywords = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'main'}
        missing = sorted(n for n in decls - defs if n not in keywords)
        rel = os.path.relpath(h, ROOT)
        if missing:
            problems.append('%s: 声明未定义 %s' % (rel, missing))
            print('  BAD %s: 缺少 %s' % (rel, missing))
        else:
            print('  OK  %s: %d 个声明全部有定义' % (rel, len(decls)))

    print()
    print('=== 3. app/ 与 test/ 是否误用 ESP-IDF 头 ===')
    bad = []
    for f in cfiles + sorted(glob.glob(os.path.join(APP, '*.h'))):
        for m in re.finditer(r'#\s*include\s*<([^>]+)>',
                             open(f, encoding='utf-8').read()):
            inc = m.group(1)
            if inc.startswith(ESPIDF_PREFIXES):
                bad.append('%s -> %s' % (os.path.relpath(f, ROOT), inc))
    if bad:
        problems.extend(bad)
        print('  BAD ' + '\n       '.join(bad))
    else:
        print('  OK  未引用任何 ESP-IDF 头（ADR-1 分层约束成立）')

    print()
    print('=== 4. test/ include 可解析性 ===')
    ok = True
    for f in sorted(glob.glob(os.path.join(TEST, '*.c'))):
        for m in re.finditer(r'#\s*include\s*"([^"]+)"',
                             open(f, encoding='utf-8').read()):
            inc = m.group(1)
            if not (os.path.exists(os.path.join(TEST, inc))
                    or os.path.exists(os.path.join(APP, inc))):
                print('  BAD %s -> %s' % (os.path.relpath(f, ROOT), inc))
                problems.append('%s -> %s' % (f, inc))
                ok = False
    if ok:
        print('  OK  所有本地 include 均可解析')

    print()
    print('=' * 50)
    if problems:
        print('发现 %d 个问题：' % len(problems))
        for p in problems:
            print('  - ' + p)
        return 1
    print('静态结构检查全部通过')
    print('（不能替代真实编译，仅排除括号/声明/分层等结构性错误）')
    return 0


if __name__ == '__main__':
    sys.exit(main())
