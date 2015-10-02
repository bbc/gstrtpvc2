plugin_LTLIBRARIES = libgstrtpvc2.la

# sources used to compile this plug-in
libgstrtpvc2_la_SOURCES = gstrtp.c gstrtpvc2pay.c gstrtpvc2pay.h gstrtputils.c gstrtputils.h vc2vlcparse.c vc2vlcparse.h gstrtpvc2depay.c gstrtpvc2depay.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstrtpvc2_la_CFLAGS = $(GST_CFLAGS)

libgstrtpvc2_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	-lgstpbutils-@GST_API_VERSION@

libgstrtpvc2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtpvc2_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstrtpvc2pay.h gstrtputils.h gstrtpvc2depay.h
