import re
import sys
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

BIN = r'C:\Users\gpmpo\cs2-internal\re\cs2_212C3300000\cs2_212C3300000.bin'
d = open(BIN, 'rb').read()
low = d.lower()

kws = ['aim', 'aimbot', 'silent', 'trigger', 'esp', 'wallhack', 'chams', 'glow', 'backtrack',
       'fakelag', 'desync', 'resolver', 'bhop', 'strafe', 'hitchance', 'multipoint', 'autowall',
       'penetration', 'fov', 'smooth', 'hitbox', 'grenade', 'radar', 'watermark', 'indicator',
       'config', 'script', 'lua', 'skin', 'inventory', 'clantag', 'matchmaking', 'premier',
       'prediction', 'extrapolate', 'ping', 'spoof', 'hwid', 'steamid', 'rank', 'kill', 'mvp',
       'menu', 'overlay', 'visual', 'misc', 'rage', 'legit', 'antiaim', 'anti-aim', 'exploit',
       'the ', 'you ', 'error', 'failed', 'invalid', 'unable', 'please', 'cannot']

for k in kws:
    c = low.count(k.encode())
    if c:
        print('%-14s %d' % (k, c))
