"""Build-time Zstd envelope checks; the C++ runtime remains the decoder oracle.

Header field layout: https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md
"""
import subprocess


def frame_content_size(frame):
    if len(frame) < 6 or frame[:4] != b'\x28\xb5\x2f\xfd':
        raise ValueError('invalid/truncated Zstd frame header')
    descriptor = frame[4]
    if descriptor & 8:
        raise ValueError('reserved Zstd header bit set')
    single = bool(descriptor & 32)
    offset = 5 + (not single) + (0, 1, 2, 4)[descriptor & 3]
    size_bytes = (int(single), 2, 4, 8)[descriptor >> 6]
    if len(frame) < offset + size_bytes:
        raise ValueError('truncated Zstd content-size field')
    if not size_bytes:
        return None
    value = int.from_bytes(frame[offset:offset + size_bytes], 'little')
    return value + (256 if size_bytes == 2 else 0)


def compress_bytes(payload, executable='zstd'):
    if not 0 < len(payload) <= 64 * 1024 * 1024:
        raise ValueError('asset outside runtime size limit')
    # The already-hashed bytes go through stdin, but its size must be pledged.
    # An unknown-size streaming frame is valid Zstd, not a valid provider asset.
    encoded = subprocess.run([executable, '-q', '-19', '--check', '--stdout',
                              '--stream-size=' + str(len(payload))], input=payload,
                             check=True, capture_output=True).stdout
    if frame_content_size(encoded) != len(payload) or not encoded[4] & 4:
        raise ValueError('encoder omitted required content size/checksum')
    restored = subprocess.run([executable, '-q', '-d', '--stdout'], input=encoded,
                              check=True, capture_output=True).stdout
    if restored != payload:
        raise ValueError('compressed asset round-trip failed')
    return encoded
