CFLAGS  := $(EXTRA_CFLAGS) -Wall -Wextra -D_GNU_SOURCE -DPIC -fpic
LDFLAGS := $(EXTRA_LDFLAGS) -shared -fpic -Wl,-soname,libbtrace.so

solibs               := libbtrace.so
libbtrace.so-objs    := btrace.o
libbtrace.so-cflags  := $(CFLAGS)
libbtrace.so-ldflags := $(LDFLAGS)
libbtrace.so-pkgconf := libdw

define libbtrace_pkgconf_tmpl
prefix=$(PREFIX)
exec_prefix=$${prefix}
libdir=$${exec_prefix}/lib

Name: libbtrace
Description: Backtrace library
Version: %%PKG_VERSION%%
Requires.private: libdw
Cflags: -rdynamic
Libs: -rdynamic -L$${libdir} -Wl,--no-as-needed,-lbtrace,--as-needed
endef

pkgconfigs        := libbtrace.pc
libbtrace.pc-tmpl := libbtrace_pkgconf_tmpl
