"""Reads a demo (demos/<name>.dm3) recorded by the retail FAKK2 engine or by OpenFAKK2,
which writes the same messages, and prints what is in it (docs/known-differences.md,
"Demos"). The format was read from fakk2.exe 1.02, apart from this file's code, so it
checks what OpenFAKK2 writes; the addresses are that file's.

    python tools/retail-compare/dm3.py build/demos/retail-zoo.dm3 [-v]

The file is a series of [int sequence][int length][length bytes] records, ending with a
length of -1 (CL_Record_f 0x420160, CL_ReadDemoMessage 0x4204e0). Each record is a server
message as the client received it after the netchan header. Every message is read to its
last byte; a message that does not end exactly there means the format here is wrong.
"""
import struct
import sys

# MSG_ReadBits 0x439520: plain bits, least significant first, no Huffman coding.
# readcount counts the bytes begun; aligning (0x439350) moves to the byte after next,
# which the writer (MSG_GetSpace 0x439370) also skips, so strings start a byte later.
class Msg:
    def __init__(self, data):
        self.data, self.cursize, self.readcount, self.bit = data, len(data), 0, 0

    def bits(self, n):
        signed, n = n < 0, abs(n)
        value = i = 0
        while i < n:
            if self.bit == 0:
                self.readcount += 1
            take = min(8 - self.bit, n - i)
            byte = self.data[self.readcount - 1]
            value |= ((byte >> self.bit) & ((1 << take) - 1)) << i
            self.bit = (self.bit + take) & 7
            i += take
        if signed and value & (1 << (n - 1)):
            value -= 1 << n
        return value

    def byte(self):          # 0x439720
        return -1 if self.readcount + 1 > self.cursize else self.bits(8) & 0xff

    def short(self):         # 0x439750
        return -1 if self.readcount + 2 > self.cursize else self.bits(-16)

    def long(self):          # 0x439780
        return -1 if self.readcount + 4 > self.cursize else self.bits(-32)

    def float(self):         # 0x4397b0
        return struct.unpack('<f', struct.pack('<i', self.bits(-32)))[0]

    def align(self):         # 0x439350
        if self.bit:
            self.readcount += 1
            self.bit = 0

    def string(self):        # 0x4397e0: at most 1023 characters, '%' read as '.'
        self.align()
        out = bytearray()
        while len(out) < 1023:
            c = self.byte()
            if c in (-1, 0):
                break
            out.append(0x2e if c == 0x25 else c)
        return out.decode('latin-1')

    def raw(self, n):        # 0x4398a0
        self.align()
        return bytes(self.byte() & 0xff for _ in range(n))

# A float field (0 bits) is a bit then either 13 bits holding an integral value plus 4096,
# or the 32 bits of the float (MSG_WriteDeltaEntity 0x439d7c).
def read_field(msg, bits):
    if bits:
        return msg.bits(bits)
    if msg.bits(1) == 0:
        return float(msg.bits(13) - 4096)
    return struct.unpack('<f', struct.pack('<I', msg.bits(32)))[0]

# entityState_t fields in wire order (0x4ea228: name, offset, bits).
ENTITY_FIELDS = [
    ('eType', 8), ('eFlags', 16), ('pos.trType', 8), ('pos.trTime', 32), ('pos.trDuration', 32),
    ('pos.trBase[0]', 0), ('pos.trBase[1]', 0), ('pos.trBase[2]', 0),
    ('pos.trDelta[0]', 0), ('pos.trDelta[1]', 0), ('pos.trDelta[2]', 0),
    ('apos.trType', 8), ('apos.trTime', 32), ('apos.trDuration', 32),
    ('apos.trBase[0]', 0), ('apos.trBase[1]', 0), ('apos.trBase[2]', 0),
    ('apos.trDelta[0]', 0), ('apos.trDelta[1]', 0), ('apos.trDelta[2]', 0),
    ('netorigin[0]', 0), ('netorigin[1]', 0), ('netorigin[2]', 0),
    ('origin2[0]', 0), ('origin2[1]', 0), ('origin2[2]', 0),
    ('netangles[0]', 0), ('netangles[1]', 0), ('netangles[2]', 0),
    ('constantLight', 32), ('loopSound', 16), ('loopSoundVolume', 0), ('loopSoundMinDist', 0),
    ('parent', 16), ('tag_num', 10), ('attach_use_angles', 1),
    ('attach_offset[0]', 0), ('attach_offset[1]', 0), ('attach_offset[2]', 0),
    ('modelindex', 16), ('skinNum', 16), ('anim', 11), ('frame', 10), ('crossblend_time', -16),
    ('torso_anim', 11), ('torso_frame', 10), ('torso_crossblend_time', -16),
] + [('bone_tag[%d]' % i, -8) for i in range(5)] + [
    ('bone_angles[%d][%d]' % (i, j), 0) for i in range(5) for j in range(3)
] + [
    ('clientNum', 8), ('groundEntityNum', 10), ('solid', 32), ('scale', 0), ('alpha', 0),
    ('renderfx', 32),
]
assert len(ENTITY_FIELDS) == 73

# The common change masks (0x4e6220: count 31, then per entry a use count and 10 bytes).
CHANGE_VECTORS = [bytes.fromhex(h) for h in (
    'e0c00000000000000000', '60c00000000000000000', '60000000000000000000',
    '60800000000000000000', 'e0000000000000000000', '80000000000000000000',
    'e100000e042000000000', 'e0800000000000000000', '60c00000001000000000',
    '00000000001000000000', 'a0000000000000000000', 'e0400000000000000000',
    '60800000001000000000', '60000000001000000000', 'ed070000008000000000',
    '60400000000000000000', '60c00000001002000000', 'a0c00000000000000000',
    'ed070000003000000000', 'c0c00000000000000000', '60800000001002000000',
    '60c00000010000000000', 'e0c00000010000000000', '60000000001002000000',
    'c0000000000000000000', 'e0c00000001002000000', '00800000000000000000',
    '60800000010000000000', 'e100000e002000000000', '00c00000000000000000',
    '20000000000000000000',
)]

# playerState_t fields in wire order (0x4ea598), then six arrays of 32 ints sent as a
# 32-bit change mask and a short per change (MSG_WriteDeltaPlayerstate 0x43a280).
PLAYER_FIELDS = [
    ('commandTime', 32), ('pm_type', 8), ('bobCycle', 8), ('pm_flags', 16), ('pm_time', -16),
    ('pm_runtime', -16), ('origin[0]', 0), ('origin[1]', 0), ('origin[2]', 0),
    ('velocity[0]', 0), ('velocity[1]', 0), ('velocity[2]', 0), ('gravity', 16), ('speed', 16),
    ('delta_angles[0]', 16), ('delta_angles[1]', 16), ('delta_angles[2]', 16),
    ('groundEntityNum', 10), ('feetfalling', 8), ('clientNum', 8),
    ('viewangles[0]', 0), ('viewangles[1]', 0), ('viewangles[2]', 0), ('viewheight', -8),
    ('current_music_mood', 8), ('fallback_music_mood', 8), ('music_volume', 0),
    ('music_volume_fade_time', 0), ('reverb_type', 6), ('reverb_level', 0),
    ('blend[0]', 0), ('blend[1]', 0), ('blend[2]', 0), ('blend[3]', 0), ('fov', 0),
    ('camera_angles[0]', 0), ('camera_angles[1]', 0), ('camera_angles[2]', 0),
    ('camera_origin[0]', 0), ('camera_origin[1]', 0), ('camera_origin[2]', 0),
    ('camera_offset[0]', 0), ('camera_offset[1]', 0), ('camera_offset[2]', 0),
    ('damage_angles[0]', 0), ('damage_angles[1]', 0), ('damage_angles[2]', 0),
    ('camera_time', 0), ('camera_flags', 16),
]
assert len(PLAYER_FIELDS) == 49
PLAYER_ARRAYS = ('stats', 'activeItems', 'inventory_name_index', 'ammo_name_index',
                 'ammo_amount', 'max_ammo_amount')

# The state a baseline is read against (0x43a220), not all zeros.
def null_entity(number=0):
    e = {name: 0 for name, _ in ENTITY_FIELDS}
    e.update({'scale': 1.0, 'alpha': 1.0, 'parent': 0x3ff, 'constantLight': 0xffffff,
              'renderfx': 0x10, 'number': number, 'surfaces': [0] * 32})
    for i in range(5):
        e['bone_tag[%d]' % i] = -1
    return e


def read_delta_entity(msg, old, number):
    """MSG_ReadDeltaEntity 0x439e70 after the caller has read the 10-bit number.
    Returns None when the entity is removed."""
    if msg.bits(1):
        return None
    new = dict(old, surfaces=list(old['surfaces']), number=number)
    if msg.bits(1) == 0:
        return new
    vec = msg.bits(5)
    if vec == 31:
        mask = bytes(msg.byte() for _ in range(9)) + bytes([msg.bits(1)])
    else:
        mask = CHANGE_VECTORS[vec]
    for i, (name, bits) in enumerate(ENTITY_FIELDS):
        if mask[i >> 3] & (1 << (i & 7)):
            new[name] = read_field(msg, bits)
    if msg.bits(1):
        changed = msg.long()
        for i in range(32):
            if changed & (1 << i):
                new['surfaces'][i] = msg.byte()
    return new


def read_delta_playerstate(msg, old):
    """MSG_ReadDeltaPlayerstate 0x43a680."""
    ps = dict(old) if old else {name: 0 for name, _ in PLAYER_FIELDS}
    for name in PLAYER_ARRAYS:
        ps[name] = list(old[name]) if old else [0] * 32
    for name, bits in PLAYER_FIELDS:
        if msg.bits(1):
            ps[name] = read_field(msg, bits)
    for name in PLAYER_ARRAYS:
        if msg.bits(1):
            changed = msg.long()
            for i in range(32):
                if changed & (1 << i):
                    ps[name][i] = msg.short()
    return ps


def read_sounds(msg):
    """The sounds started or stopped since the last snapshot (0x43ac30)."""
    sounds = []
    if not msg.bits(1):
        return sounds
    count = msg.bits(6)
    for _ in range(count if count <= 32 else 0):
        if msg.bits(1):
            sounds.append({'stop': True, 'entity': msg.bits(10), 'channel': msg.bits(6)})
            continue
        s = {'stop': False, 'origin': None}
        if msg.bits(1):
            s['origin'] = (msg.float(), msg.float(), msg.float())
        s['entity'], s['channel'], s['index'] = msg.bits(10), msg.bits(6), msg.bits(8)
        s['volume'] = msg.float() if msg.bits(1) else -1.0
        s['min_dist'] = msg.float() if msg.bits(1) else -1.0
        sounds.append(s)
    return sounds


class Demo:
    def __init__(self):
        self.configstrings, self.baselines = {}, {}
        self.frames = {}        # message number -> (playerstate, {number: entity})
        self.snapshots, self.commands, self.prints, self.sounds = [], [], [], []

    def gamestate(self, msg):
        """CL_ParseGamestate 0x423b60: no client number or checksum feed follow."""
        self.command_sequence = msg.long()
        while True:
            cmd = msg.byte()
            if cmd <= 0:
                return
            if cmd == 3:
                index = msg.short()
                self.configstrings[index] = msg.string()
            elif cmd == 4:
                number = msg.bits(10)
                self.baselines[number] = read_delta_entity(msg, null_entity(number), number)
            else:
                raise ValueError('bad gamestate command %d' % cmd)

    def snapshot(self, msg, message_num):
        """CL_ParseSnapshot 0x423820."""
        snap = {'message': message_num, 'reliable_ack': msg.long(), 'server_time': msg.long(),
                'residual': msg.byte()}
        delta = msg.byte()
        snap['flags'] = msg.byte()
        snap['areamask'] = msg.raw(msg.byte())
        old = self.frames.get(message_num - delta) if delta else None
        if delta and old is None:
            raise ValueError('message %d deltas from missing %d' % (message_num, message_num - delta))
        ps = read_delta_playerstate(msg, old[0] if old else None)
        # CL_ParsePacketEntities 0x423560
        old_ents = sorted(old[1].items()) if old else []
        ents, k = {}, 0
        while True:
            number = msg.bits(10)
            if number == 0x3ff:
                break
            while k < len(old_ents) and old_ents[k][0] < number:
                ents[old_ents[k][0]] = old_ents[k][1]
                k += 1
            if k < len(old_ents) and old_ents[k][0] == number:
                base = old_ents[k][1]
                k += 1
            else:
                base = self.baselines.get(number) or null_entity(number)
            e = read_delta_entity(msg, base, number)
            if e is not None:
                ents[number] = e
        for number, e in old_ents[k:]:
            ents[number] = e
        snap['sounds'] = read_sounds(msg)
        self.sounds += snap['sounds']
        self.frames[message_num] = (ps, ents)
        snap['ps'], snap['entities'] = ps, ents
        self.snapshots.append(snap)

    def message(self, data, message_num):
        """CL_ParseServerMessage 0x423d60: commands until the data runs out; no svc_EOF."""
        msg = Msg(data)
        while True:
            cmd = msg.byte()
            if cmd == -1:
                break
            if cmd == 1:                      # svc_nop
                continue
            if cmd == 2:                      # svc_gamestate
                self.gamestate(msg)
            elif cmd == 5:                    # svc_serverCommand
                self.commands.append((msg.long(), msg.string()))
            elif cmd == 7:                    # svc_snapshot
                self.snapshot(msg, message_num)
            elif cmd == 8:                    # svc_centerprint
                self.prints.append(msg.string())
            elif cmd == 9:                    # svc_locprint
                self.prints.append((msg.short(), msg.short(), msg.string()))
            else:
                raise ValueError('illegible command %d at byte %d' % (cmd, msg.readcount))
        if msg.readcount != msg.cursize:
            raise ValueError('read %d of %d bytes' % (msg.readcount, msg.cursize))


def read_demo(path):
    blob = open(path, 'rb').read()
    demo, pos, records = Demo(), 0, 0
    while pos + 8 <= len(blob):
        sequence, length = struct.unpack_from('<ii', blob, pos)
        pos += 8
        if length == -1:
            break
        demo.message(blob[pos:pos + length], sequence)
        pos += length
        records += 1
    demo.records = records
    return demo


def main():
    demo = read_demo(sys.argv[1])
    verbose = '-v' in sys.argv
    print('%d messages, %d configstrings, %d baselines, %d snapshots, %d server commands, '
          '%d sounds, %d prints' % (demo.records, len(demo.configstrings), len(demo.baselines),
                                    len(demo.snapshots), len(demo.commands), len(demo.sounds),
                                    len(demo.prints)))
    for i in sorted(demo.configstrings)[:8 if not verbose else None]:
        print('  cs %4d: %s' % (i, demo.configstrings[i][:100]))
    snaps = demo.snapshots
    for s in snaps[:: max(1, len(snaps) // 12)] if not verbose else snaps:
        ps = s['ps']
        print('  snap msg %5d time %7d ents %3d  origin (%.1f %.1f %.1f) yaw %.1f  sounds %d' % (
            s['message'], s['server_time'], len(s['entities']), ps['origin[0]'], ps['origin[1]'],
            ps['origin[2]'], ps['viewangles[1]'], len(s['sounds'])))
    for seq, text in demo.commands[:10 if not verbose else None]:
        print('  cmd %d: %s' % (seq, text[:100]))


if __name__ == '__main__':
    main()
