#!/usr/bin/env python3
# Lesson B21: 校验 TinyOS L61 kernel.elf 的 Multiboot2 graphics request tag。
# 用法：check-gfx-request.py <kernel.elf>
# 在内核文件里找 mb2 header（magic 0xe85250d6，8 对齐），遍历 header tags，
# 断言存在 graphics request tag（type 5）且为 800x600x32（TinyOS L61 请求）。
import struct
import sys

def main(path):
    d = open(path, 'rb').read()
    off = d.find(struct.pack('<I', 0xe85250d6))
    if off < 0 or off % 8 != 0:
        raise SystemExit('mb2 header not found/aligned')
    t = off + 16
    while True:
        typ = struct.unpack_from('<H', d, t)[0]
        size = struct.unpack_from('<I', d, t + 4)[0]
        if typ == 5:
            w, h, dp = struct.unpack_from('<III', d, t + 8)
            if (w, h, dp) != (800, 600, 32):
                raise SystemExit('graphics request %dx%dx%d' % (w, h, dp))
            print('L61 graphics request: %dx%dx%d' % (w, h, dp))
            return 0
        if typ == 0:
            raise SystemExit('no graphics request tag')
        t += (size + 7) & ~7

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
