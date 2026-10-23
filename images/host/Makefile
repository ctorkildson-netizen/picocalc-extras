# Decode a picture on a PC exactly as the PicoCalc would, and write the
# 320x320 screen it would show as a PPM:
#     make && ./host_test picture.jpg screen.ppm
I = ..
P = $(I)/modules/PNGdec/src
SRC = host_test.c $(I)/canvas.c $(I)/formats.c $(I)/fmt_jpeg.c $(I)/fmt_bmp.c $(I)/fmt_png.c \
      $(I)/tjpgd/tjpgd.c $(P)/adler32.c $(P)/crc32.c $(P)/infback.c $(P)/inffast.c \
      $(P)/inflate.c $(P)/inftrees.c $(P)/zutil.c

host_test: $(SRC)
	$(CC) -O2 -w -I$(I) -I$(I)/tjpgd -I$(P) $(SRC) -o $@

clean:
	rm -f host_test
