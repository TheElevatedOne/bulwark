import json, sys, math
KEYS = ['Up','Under','Left','Right','L1','R1','L3','R3','CRO','CIR','SQU','TRI','PS','Sel','Sta','Camera','FL1','FL2','FR1','FR2']
CODE = {k: i + 1 for i, k in enumerate(KEYS[:16])}; CODE.update({'FL1': 17, 'FR1': 18, 'FL2': 31, 'FR2': 32, 'NC': 0xFF, '': 0})
def rnd(v): return int(math.floor(v + 0.5))
def pts(dz, curve):
    b, e = dz['beginDeadzone'], dz['endDeadzone']; out = []
    for p in curve['points']:
        out += [max(0, min(255, rnd(255 * (p['x'] - b) / (e - b)))), rnd(255 * p['y'] / 100)]
    return out + [255, 255]
def expect(j):
    E = {}
    def put(o, vals, what):
        for i, v in enumerate(vals): E[o + i] = (v, what)
    bi = j['BasicInfo']; m = bi['motor']; a = bi['audio']
    put(0x020, [m[k] * 25 for k in ('leftMotor', 'rightMotor', 'ltMotor', 'rtMotor')], 'motors')
    put(0x026, [int(a['audioEnable']), a['audioVolume'], a['audioMixer'], a['micMute'], a['micSensitivity']], 'audio')
    for i, k in enumerate(KEYS):
        kk = j['KeyProfiles'][k]['key']; t = [CODE.get(x, None) for x in kk['mapKeys']]
        put(0x041 + 7 * i + 1, [len(t)] + (t + [0, 0, 0])[:3], f'key {k}')
    for o, k in ((0x0CD, 'L2'), (0x0E9, 'R2')):
        e = j['KeyProfiles'][k]; dz = e['deadzone']; c = e['curve']; h = e['hairTrigger']
        put(o + 1, [1, dz['beginDeadzone'], dz['endDeadzone'], dz['beginAntiDeadzone'], dz['endAntiDeadzone']], f'{k} deadzone')
        put(o + 0x0B, [h['mode'] if h['enabled'] else 0, h['beginRange'], h['endRange']], f'{k} hair')
        put(o + 0x0E, [1, c['type'], c['curvature']], f'{k} curve hdr')
        put(o + 0x13, pts(dz, c), f'{k} curve pts')
    for o, k in ((0x13C, 'Left'), (0x15C, 'Right')):
        s = j['Sticks'][k]; dz = s['deadzone']; c = s['curve']; am = s['advanceMap']; w = am['wheelMapping']
        put(o + 1, [int(s['rawEnabled'])], f'{k} raw?')
        put(o + 2, [1, dz['beginDeadzone'], dz['endDeadzone'], dz['beginAntiDeadzone'], dz['endAntiDeadzone']], f'{k} deadzone')
        put(o + 7, [1, c['type'], c['curvature']], f'{k} curve hdr')
        put(o + 0x0C, pts(dz, c), f'{k} curve pts')
        put(o + 0x17, [am['axisRatio'], am['mouseDpi'], am['outputType']], f'{k} advance')
        put(o + 0x1B, [CODE.get(w[x], None) for x in ('outline', 'up', 'under', 'left', 'right')], f'{k} wheel')
    for o, k in ((0x19C, 'Aim'), (0x1BE, 'Tilt')):
        s = j['Motion'][k]; dz = s['deadzone']; c = s['curve']; am = s['advanceMap']; w = am['wheelMapping']
        put(o, [s['activateBehavior'], CODE.get(s['triggerKey'], None)], f'{k} activate/trigger')
        put(o + 3, [1, dz['beginDeadzone'], dz['endDeadzone'], dz['beginAntiDeadzone'], dz['endAntiDeadzone']], f'{k} deadzone')
        put(o + 8, [1, c['type'], c['curvature']], f'{k} curve hdr')
        put(o + 0x0D, pts(dz, c), f'{k} curve pts')
        put(o + 0x19, [am['axisRatio'], am['mouseDpi'], am['outputType']], f'{k} advance')
        put(o + 0x1D, [CODE.get(w[x], None) for x in ('outline', 'up', 'under', 'left', 'right')], f'{k} wheel')
    return E
for jf, bf in zip(sys.argv[1::2], sys.argv[2::2]):
    j = json.load(open(jf)); b = open(bf, 'rb').read(); E = expect(j)
    bad = [(o, v, b[o], w) for o, (v, w) in sorted(E.items()) if v != b[o]]
    print(f"{jf.split('/')[-1]}: {len(E)} bytes predicted, {len(E)-len(bad)} match, {len(bad)} differ")
    for o, v, got, w in bad[:12]: print(f"   0x{o:03X} {w:22s} expected {v} got {got}")
    covered = set(E); other = [o for o in range(480) if o not in covered and b[o]]
    print("   non-zero bytes not covered:", ' '.join(f'{o:03X}={b[o]:02X}' for o in other))
