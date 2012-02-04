INCLUDES = 			\
	@KDRIVE_INCS@		\
	@KDRIVE_CFLAGS@		\
	-I$(top_srcdir)/exa

noinst_LTLIBRARIES = libepson.la

libepson_la_SOURCES = 	\
	epson13806.c	\
	epson13806.h	\
	epson13806draw.c\
	epson13806draw.h\
	epson13806reg.h

if KDRIVEEPSON
bin_PROGRAMS = Xepson

Xepson_SOURCES = \
	epson13806stub.c

Xepson_LDADD = \
	libepson.la \
	$(top_builddir)/exa/libexa.la \
	@KDRIVE_LIBS@

Xepson_DEPENDENCIES =	\
	libepson.la	\
	@KDRIVE_LOCAL_LIBS@

Xepson_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)

relink:
	$(AM_V_at)rm -f $(bin_PROGRAMS) && $(MAKE) $(bin_PROGRAMS)
endif
