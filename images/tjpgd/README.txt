TJpgDec R0.03, ChaN's Tiny JPEG Decompressor (http://elm-chan.org/fsw/tjpgd/),
as found in Bodmer's TJpg_Decoder (https://github.com/Bodmer/TJpg_Decoder,
commit 71bfc26, src/), which adds a byte-swap option for RGB565 output.

A copy rather than a submodule, because the settings live in tjpgdcnf.h next
to the decoder.  tjpgd.c and tjpgd.h are unchanged.  tjpgdcnf.h has two
settings changed, for speed on the RP2350:

    JD_SZBUF       512 -> 2048   bigger reads from the SD card
    JD_FASTDECODE    1 -> 2      table look-up for Huffman decoding
                                 (needs about 6 KB more working memory)

ChaN's terms are at the top of tjpgd.c: free to use, change and pass on,
keeping the copyright notice.
