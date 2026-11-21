"""Decrypt a GameSir Nexus "Export Profile" text into its profile JSON.

The export is base64(AES-256-CBC(PKCS7(JSON))). The key and IV are read at run time from YOUR OWN copy of
HJC.GameSir.Nexus2_0.dll (version 2.5.24.0: key at .data+0xdd5c8, IV at .data+0xdd600); nothing of GameSir's
is stored in this repository.

usage: python -I nexus_export.py HJC.GameSir.Nexus2_0.dll EXPORT.txt [OUT.json]
"""
import base64, json, sys
import pefile
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

KEY_OFF, IV_OFF = 0xdd5c8, 0xdd600


def key_iv(dll):
    pe = pefile.PE(dll, fast_load=True)
    data_sec = [s for s in pe.sections if s.Name.startswith(b'.data')][0]
    raw = open(dll, 'rb').read()[data_sec.PointerToRawData:data_sec.PointerToRawData + data_sec.SizeOfRawData]
    return raw[KEY_OFF:KEY_OFF + 32], raw[IV_OFF:IV_OFF + 16]


def decrypt(dll, text):
    key, iv = key_iv(dll)
    ct = base64.b64decode(text.strip())
    d = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    pt = d.update(ct) + d.finalize()
    if not 1 <= pt[-1] <= 16:
        raise ValueError("bad padding: wrong DLL version or not a Nexus export")
    return json.loads(pt[:-pt[-1]].decode('utf-8'))


if __name__ == '__main__':
    profile = decrypt(sys.argv[1], open(sys.argv[2]).read())
    out = json.dumps(profile, indent=1)
    if len(sys.argv) > 3:
        open(sys.argv[3], 'w').write(out)
    else:
        print(out)
