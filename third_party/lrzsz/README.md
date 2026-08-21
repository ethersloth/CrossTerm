# lrzsz 0.12.20

This directory contains the lrzsz 0.12.20 source used as the basis for
CrossTerm's embedded ZModem engine. The original license is in `COPYING`.

CrossTerm currently builds the Windows sender sources into the executable
through the `CrossTermLrzszSender` static target. The source still uses the
original command-line entry point and stdio transport internally, so the
CrossTerm UI does not call it yet. The next porting step is to replace those
stdio and terminal-mode operations with a CrossTerm raw byte-stream adapter.

The optional lrzsz TCP mode is intentionally excluded. CrossTerm uses its
SSH transport instead.