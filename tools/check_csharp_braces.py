"""C# 源码括号配平检查（快速定位 CS1022 类错误）。"""
import io
import re
import sys


def strip(src):
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ''
        if c == '/' and nxt == '/':
            j = src.find('\n', i)
            i = n if j < 0 else j
            out.append(' ')
        elif c == '/' and nxt == '*':
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
            out.append(' ')
        elif c == '@' and nxt == '"':
            j = i + 2
            while j < n:
                if src[j] == '"':
                    if j + 1 < n and src[j + 1] == '"':
                        j += 2
                        continue
                    break
                j += 1
            out.append('""')
            i = j + 1
        elif c == '$' and nxt == '"':
            j = i + 2
            while j < n:
                if src[j] == '\\':
                    j += 2
                    continue
                if src[j] == '"':
                    break
                j += 1
            out.append('""')
            i = j + 1
        elif c == '"':
            j = i + 1
            while j < n:
                if src[j] == '\\':
                    j += 2
                    continue
                if src[j] == '"':
                    break
                j += 1
            out.append('""')
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n:
                if src[j] == '\\':
                    j += 2
                    continue
                if src[j] == "'":
                    break
                j += 1
            out.append("''")
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def main():
    path = sys.argv[1]
    src = io.open(path, encoding='utf-8').read()
    code = strip(src)

    for op, cl, name in (('{', '}', '花'), ('(', ')', '圆'), ('[', ']', '方')):
        d = code.count(op) - code.count(cl)
        print(f'{name}括号: {op}={code.count(op)} {cl}={code.count(cl)} 差={d}')

    # 逐行跟踪花括号深度，报告异常位置
    depth = 0
    for ln, line in enumerate(code.split('\n'), 1):
        for ch in line:
            if ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
        if depth < 0:
            print(f'  第 {ln} 行 depth 变负：{src.splitlines()[ln-1].strip()[:70]}')
            depth = 0
    print(f'文件结束时的花括号深度: {depth}')
    if depth != 0:
        print('  -> 有未闭合的 { ，这就是 CS1022 的原因')


if __name__ == '__main__':
    main()
