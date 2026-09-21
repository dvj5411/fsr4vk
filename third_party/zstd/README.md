# Zstandard decoder 1.5.7

Pinned upstream: https://github.com/facebook/zstd/releases/tag/v1.5.7
Source archive: `zstd-1.5.7.tar.gz`, SHA-256
`eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`.

`zstddeclib.c` is generated without local edits by running upstream
`build/single_file_libs/create_single_file_decoder.sh` from its directory.
`zstd.h`, `zstd_errors.h` and `LICENSE` are copied unchanged from the same archive. This project
uses the BSD license option; retain LICENSE with distributions.

File SHA-256 values:

- zstddeclib.c: `5eb38abe0a7eea13674b312a006df72002d4192f0f0b4d09e6f03fb927a1f3d0`
- zstd.h: `9b4bc8245565c98ccfc61c07749928b57e7c0f6fddb0530c4f6aa1971893d88b`
- zstd_errors.h: `66a8c3f71d12ea6e797e4f622f31f3f8f81c41b36f48cad4f5de7d8bfb6aac0a`
- LICENSE: `7055266497633c9025b777c78eb7235af13922117480ed5c674677adc381c9d8`

Only the decompressor is linked into the compressed provider. It is static;
there is no zstd.dll requirement. The uncompressed debug provider does not link
this decoder. Build-time compression requires the Zstandard CLI (tested 1.5.7).
