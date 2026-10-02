import struct
import unittest
from pathlib import Path
from build_prepass_literals import specialize


class LiteralTests(unittest.TestCase):
    def test_reviewed_assets(self):
        root=Path(__file__).resolve().parents[2]/'assets/general'
        for tier in ('1080','2160'):
            source=(root/tier/'quality/pass-01.spv').read_bytes()
            weights=(root/tier/'quality/weights.bin').read_bytes()
            changed=specialize(source,weights)
            self.assertNotEqual(changed,source)
            def count(payload,opcode):
                words=struct.unpack('<%dI'%(len(payload)//4),payload);i=5;total=0
                while i<len(words):
                    total+=(words[i]&65535)==opcode;i+=words[i]>>16
                return total
            self.assertEqual(count(source,61)-count(changed,61),16)
            self.assertEqual(count(changed,169)-count(source,169),48)
            # Every original non-load instruction survives byte-for-byte and
            # in order: arithmetic, quad operations, stores and control flow.
            def instructions(payload):
                words=struct.unpack('<%dI'%(len(payload)//4),payload);i=5
                while i<len(words):
                    size=words[i]>>16
                    yield words[i:i+size]
                    i+=size
            remaining=iter(instructions(changed))
            for instruction in instructions(source):
                if instruction[0]&65535==61:continue
                self.assertTrue(any(other==instruction for other in remaining))
            with self.assertRaises(ValueError):specialize(changed,weights)
            with self.assertRaises(ValueError):specialize(source,bytes(1024))

    def test_unreviewed(self):
        with self.assertRaises(ValueError):specialize(b'bad',b'bad')
