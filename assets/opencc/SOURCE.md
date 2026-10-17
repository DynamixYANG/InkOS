# OpenCC character data

Source: [OpenCC](https://github.com/BYVoid/OpenCC), `data/dictionary/TSCharacters.txt`.

Copyright OpenCC contributors. Licensed under [Apache-2.0](LICENSE). The original dictionary is retained unchanged. SHA-256: `9ff46a7d30e5765375eb13d33f2b03a34d298913caf2b120380679f33ae1642d`.

`tools/generate_lyric_folding.py` takes the first single-character alternative for each entry, drops identity mappings and generates the sorted `LyricHan.cpp` table (3,221 pairs). This is search normalization, not the complete context-sensitive OpenCC converter. Original song titles and lyrics remain unchanged on screen.
