AUTOMAKE_OPTIONS=subdir-objects 1.7.1
ACLOCAL_AMFLAGS = -I m4

MOST_SOURCES =								\
	 app_state.cc app_state.hh					\
         commands.cc commands.hh					\
         diff_patch.cc diff_patch.hh					\
         lua.cc lua.hh							\
         transforms.cc transforms.hh					\
         update.cc update.hh						\
         work.cc work.hh						\
         cert.cc cert.hh						\
         database.cc database.hh					\
         key_store.cc key_store.hh					\
         file_io.cc file_io.hh						\
         keys.cc keys.hh						\
         manifest.cc manifest.hh					\
         packet.cc packet.hh						\
         sanity.cc sanity.hh						\
         vocab.cc vocab.hh vocab_terms.hh numeric_vocab.hh		\
         rcs_file.cc rcs_file.hh					\
         xdelta.cc xdelta.hh						\
         ui.cc ui.hh							\
         schema_migration.cc schema_migration.hh			\
         constants.cc constants.hh					\
         netsync.cc netsync.hh						\
         netcmd.cc netcmd.hh						\
         merkle_tree.cc merkle_tree.hh					\
         basic_io.cc basic_io.hh					\
         mkstemp.cc mkstemp.hh						\
         lcs.cc lcs.hh							\
         rcs_import.cc rcs_import.hh					\
         revision.cc revision.hh					\
	 change_set.cc change_set.hh					\
         mt_version.cc mt_version.hh					\
         automate.cc automate.hh					\
         database_check.cc database_check.hh				\
         epoch.cc epoch.hh						\
         inodeprint.cc inodeprint.hh					\
         selectors.cc selectors.hh					\
         annotate.cc annotate.hh					\
         restrictions.cc restrictions.hh				\
         hmac.cc hmac.hh                                                \
         globish.cc globish.hh                                          \
         string_queue.cc string_queue.hh                                \
         paths.cc paths.hh                                              \
                                                                        \
	 cleanup.hh unit_tests.hh interner.hh				\
         cycle_detector.hh randomfile.hh adler32.hh quick_alloc.hh	\
	 netio.hh smap.hh gettext.h					\
         package_revision.c package_revision.h				\
         package_full_revision.c package_full_revision.h options.hh     \
         i18n.h hash_map.hh

NETXX_SOURCES =									\
	netxx/accept.cxx netxx/accept.h netxx/address.cxx			\
	netxx/common.h netxx/compat.h netxx/datagram.cxx			\
	netxx/datagramserver.cxx netxx/osutil.cxx netxx/osutil.h		\
	netxx/peer.cxx netxx/probe.cxx netxx/probe_impl.h			\
	netxx/probe_select.cxx netxx/recvfrom.cxx netxx/recvfrom.h		\
	netxx/resolve.h netxx/serverbase.cxx netxx/serverbase.h			\
        netxx/sockaddr.cxx netxx/sockaddr.h netxx/socket.cxx			\
	netxx/socket.h netxx/sockopt.cxx netxx/stream.cxx			\
	netxx/streambase.cxx netxx/streamserver.cxx				\
	netxx/address.h netxx/datagram.h netxx/datagramserver.h			\
	netxx/netbuf.h netxx/netxx.h netxx/peer.h netxx/probe.h			\
	netxx/probeinfo.h netxx/sockopt.h netxx/stream.h netxx/streambase.h	\
	netxx/streamserver.h netxx/timeout.h netxx/types.h

BOTAN_SOURCES =                                                                    \
        botan/aes.cpp botan/aes_tab.cpp botan/algolist.cpp		\
        botan/allocate.cpp botan/arc4.cpp botan/asn1_alg.cpp		\
        botan/asn1_alt.cpp botan/asn1_att.cpp botan/asn1_dn.cpp		\
        botan/asn1_ext.cpp botan/asn1_ku.cpp botan/asn1_oid.cpp		\
        botan/asn1_str.cpp botan/asn1_tm.cpp botan/barrett.cpp		\
        botan/base.cpp botan/base64.cpp botan/basefilt.cpp		\
        botan/ber_code.cpp botan/ber_dec.cpp botan/big_base.cpp		\
        botan/big_code.cpp botan/big_io.cpp botan/big_ops2.cpp		\
        botan/big_ops3.cpp botan/blinding.cpp botan/buf_es.cpp		\
        botan/buf_filt.cpp botan/cbc.cpp botan/certstore.cpp		\
        botan/cfb.cpp botan/charset.cpp botan/conf.cpp botan/crc32.cpp	\
        botan/crl_ent.cpp botan/ctr.cpp botan/cts.cpp			\
        botan/data_snk.cpp botan/data_src.cpp \
        botan/def_ops.cpp botan/defalloc.cpp botan/der_code.cpp		\
        botan/der_enc.cpp botan/divide.cpp botan/dl_algo.cpp		\
        botan/dl_group.cpp botan/dl_param.cpp		\
        botan/dl_cache.cpp \
        botan/dlies.cpp botan/eax.cpp botan/ecb.cpp botan/eme1.cpp	\
        botan/eme_pkcs.cpp botan/emsa1.cpp botan/emsa2.cpp		\
        botan/emsa3.cpp botan/emsa4.cpp botan/emsa_raw.cpp		\
        botan/eng_base.cpp botan/engine.cpp botan/es_file.cpp		\
        botan/exceptn.cpp botan/filter.cpp botan/filters.cpp		\
        botan/fips140.cpp botan/fused.cpp		\
        botan/get_algo.cpp botan/get_enc.cpp botan/get_pbe.cpp		\
        botan/gzip.cpp botan/hash_id.cpp botan/hex.cpp botan/hmac.cpp	\
        botan/if_algo.cpp botan/inifile.cpp botan/init.cpp		\
        botan/kdf.cpp botan/keypair.cpp botan/look_pk.cpp		\
        botan/lookup.cpp botan/make_prm.cpp botan/mdx_hash.cpp		\
        botan/mem_pool.cpp botan/mgf1.cpp botan/mlock.cpp		\
        botan/mod_exp.cpp botan/mode_pad.cpp botan/modebase.cpp		\
        botan/mp_comba.cpp botan/mp_core.cpp botan/mp_fkmul.cpp		\
        botan/mp_misc.cpp botan/mp_mul.cpp botan/mp_shift.cpp		\
        botan/mp_smul.cpp botan/mutex.cpp botan/numthry.cpp		\
        botan/ofb.cpp botan/oids.cpp botan/parsing.cpp botan/pbes1.cpp	\
        botan/pbes2.cpp botan/pem.cpp botan/pipe.cpp botan/pipe_io.cpp	\
        botan/pipe_rw.cpp botan/pk_algs.cpp botan/pk_core.cpp		\
        botan/pk_filts.cpp botan/pk_keys.cpp botan/pk_util.cpp		\
        botan/pkcs10.cpp botan/pkcs8.cpp botan/policy.cpp		\
        botan/pow_mod.cpp \
        botan/prf_x942.cpp botan/primes.cpp botan/pubkey.cpp		\
        botan/randpool.cpp botan/reducer.cpp botan/rng.cpp		\
        botan/rsa.cpp botan/s2k.cpp botan/secqueue.cpp			\
        botan/sha160.cpp botan/symkey.cpp botan/timers.cpp		\
        botan/ui.cpp botan/util.cpp botan/x509_ca.cpp			\
        botan/x509_crl.cpp botan/x509_key.cpp botan/x509_obj.cpp	\
        botan/x509cert.cpp botan/x509find.cpp botan/x509opt.cpp		\
        botan/des.cpp botan/des_tab.cpp botan/pkcs5.cpp 		\
        botan/x509self.cpp botan/x509stor.cpp 				\
        botan/def_alg.cpp botan/def_mode.cpp botan/sha256.cpp 		\
        botan/x931_rng.cpp botan/out_buf.cpp botan/bit_ops.cpp \
        \
        botan/aes.h botan/botan.h botan/def_eng.h botan/es_win32.h	\
        botan/lookup.h botan/randpool.h botan/version.h	\
        botan/algolist.h botan/buf_es.h botan/der_enc.h			\
        botan/exceptn.h botan/mdx_hash.h botan/par_hash.h		\
        botan/reducer.h botan/x509_ca.h botan/allocate.h		\
        botan/buf_filt.h botan/dh.h botan/filter.h botan/mem_ops.h	\
        botan/pbe.h botan/rng.h botan/x509cert.h botan/arc4.h		\
        botan/cbc.h botan/dl_algo.h botan/filters.h botan/mem_pool.h	\
        botan/pbe_pkcs.h botan/rsa.h botan/x509_crl.h botan/asn1.h	\
        botan/certstor.h botan/dlies.h botan/fips140.h botan/mgf1.h	\
        botan/pem.h botan/s2k.h botan/x509_key.h botan/asn1_obj.h	\
        botan/cfb.h botan/dl_param.h botan/modebase.h	\
        botan/pipe.h botan/x509_obj.h			\
        botan/asn1_oid.h botan/conf.h botan/eax.h botan/gzip.h		\
        botan/mode_pad.h botan/pk_algs.h botan/secmem.h			\
        botan/x509self.h botan/barrett.h botan/config.h botan/ecb.h	\
        botan/hex.h botan/mod_exp.h botan/pk_core.h botan/secqueue.h	\
        botan/x509stor.h botan/base64.h botan/crc32.h botan/eme.h	\
        botan/hmac.h botan/mp_core.h botan/pkcs10.h botan/sha160.h	\
        botan/crl_ent.h botan/emsa.h	\
        botan/if_algo.h botan/mp_madd.h botan/pkcs8.h \
        botan/x919_mac.h botan/basefilt.h botan/ctr.h botan/engine.h	\
        botan/init.h botan/mp_types.h botan/pk_filts.h botan/symkey.h	\
        botan/base.h botan/cts.h botan/enums.h botan/kdf.h		\
        botan/mutex.h botan/pk_keys.h botan/timers.h botan/ber_dec.h	\
        botan/data_snk.h botan/es_capi.h botan/keypair.h		\
        botan/numthry.h botan/pk_ops.h botan/types.h botan/bigint.h	\
        botan/data_src.h botan/es_egd.h botan/look_add.h botan/ofb.h	\
        botan/pk_util.h botan/ui.h botan/blinding.h botan/defalloc.h	\
        botan/es_file.h botan/look_pk.h botan/oids.h botan/pubkey.h	\
        botan/util.h botan/charset.h botan/hash_id.h 			\
        botan/des.h botan/pkcs5.h					\
        botan/x931_rng.h botan/sha256.h \
        botan/parsing.h botan/out_buf.h botan/bit_ops.h



BOOST_SANDBOX_SOURCES = \
        boost/circular_buffer_adaptor.hpp \
	boost/circular_buffer_base.hpp \
	boost/circular_buffer_fwd.hpp \
	boost/circular_buffer.hpp

SQLITE_SOURCES = \
        sqlite/alter.c sqlite/attach.c sqlite/auth.c sqlite/btree.c \
        sqlite/build.c sqlite/date.c sqlite/delete.c sqlite/expr.c sqlite/func.c \
        sqlite/hash.c sqlite/insert.c \
        sqlite/legacy.c sqlite/main.c sqlite/opcodes.c \
        sqlite/os_unix.c sqlite/os_win.c \
        sqlite/pager.c sqlite/parse.c sqlite/pragma.c sqlite/printf.c \
        sqlite/random.c sqlite/select.c sqlite/table.c sqlite/tokenize.c \
        sqlite/trigger.c sqlite/update.c sqlite/utf.c \
        sqlite/util.c sqlite/vacuum.c \
        sqlite/vdbe.c sqlite/vdbeapi.c sqlite/vdbeaux.c \
        sqlite/vdbemem.c sqlite/where.c \
	sqlite/prepare.c sqlite/callback.c \
	sqlite/analyze.c sqlite/vdbefifo.c \
	sqlite/complete.c \
	\
        sqlite/btree.h sqlite/config.h sqlite/hash.h sqlite/opcodes.h sqlite/os.h \
        sqlite/os_common.h sqlite/os_unix.h sqlite/os_win.h \
        sqlite/parse.h sqlite/sqlite3.h sqlite/sqliteInt.h sqlite/vdbe.h sqlite/vdbeInt.h \
	sqlite/pager.h

LUA_SOURCES = \
	lua/lapi.c lua/lapi.h lua/lauxlib.c lua/lauxlib.h lua/lbaselib.c \
	lua/lcode.c lua/lcode.h lua/ldblib.c lua/ldebug.c lua/ldebug.h lua/ldo.c \
	lua/ldo.h lua/ldump.c lua/lfunc.c lua/lfunc.h lua/lgc.c lua/lgc.h \
	lua/liolib.c lua/llex.c lua/llex.h lua/llimits.h lua/lmathlib.c lua/lmem.c \
	lua/lmem.h lua/loadlib.c lua/lobject.c lua/lobject.h lua/lopcodes.c \
	lua/lopcodes.h lua/lparser.c lua/lparser.h lua/lstate.c lua/lstate.h \
	lua/lstring.c lua/lstring.h lua/lstrlib.c lua/ltable.c lua/ltable.h \
	lua/ltablib.c lua/ltests.c lua/ltm.c lua/ltm.h lua/lua.h lua/lualib.h \
	lua/lundump.c lua/lundump.h lua/lvm.c lua/lvm.h lua/lzio.c lua/lzio.h

IDNA_SOURCES = \
	idna/gunibreak.h idna/gunidecomp.h idna/idna.h idna/nfkc.c	\
	idna/punycode.c idna/rfc3454.c idna/stringprep.h idna/version.c \
	idna/gunicomp.h idna/idna.c idna/idn-int.h idna/profiles.c	\
	idna/punycode.h idna/stringprep.c idna/toutf8.c

POPT_SOURCES = \
	popt/findme.c  popt/poptconfig.c  popt/poptint.h   \
	popt/findme.h  popt/popt.h        popt/poptparse.c \
	popt/popt.c    popt/popthelp.c    popt/system.h

UNIX_PLATFORM_SOURCES = \
	unix/read_password.cc unix/get_system_flavour.cc unix/process.cc unix/terminal.cc \
	unix/inodeprint.cc unix/fs.cc

WIN32_PLATFORM_SOURCES = \
	win32/read_password.cc win32/get_system_flavour.cc win32/process.cc win32/terminal.cc \
	win32/inodeprint.cc win32/fs.cc win32/wcwidth.c

# primaries

bin_PROGRAMS = monotone
check_PROGRAMS = unit_tests
noinst_PROGRAMS = txt2c 

monotone_SOURCES = $(MOST_SOURCES) monotone.cc main.cc
unit_tests_SOURCES = $(MOST_SOURCES) unit_tests.cc crypto_tests.cc

txt2c_SOURCES = txt2c.cc

noinst_LIBRARIES = libplatform.a lib3rdparty.a 
libplatform_a_SOURCES = platform.hh
lib3rdparty_a_SOURCES = $(BOOST_SANDBOX_SOURCES) \
			$(BOTAN_SOURCES) \
			$(IDNA_SOURCES) \
			$(POPT_SOURCES) \
			$(NETXX_SOURCES) \
			$(LUA_SOURCES) \
			$(SQLITE_SOURCES)

# flags

if BUILD_PCH

PCH_SOURCE = $(srcdir)/pch.hh
# the .gch file only will be seen if it's in the same dir as the .hh file,
# so need to copy it for separate build directories
PCH_BUILD = pch-build.hh
PCH_FLAGS = -Winvalid-pch -include $(PCH_BUILD)
PCH_FILE = $(PCH_BUILD).gch
sinclude $(PCH_FILE).dep

*.o *.lo *.obj: $(PCH_FILE)

$(PCH_BUILD): $(PCH_SOURCE)
	cp $(PCH_SOURCE) $(PCH_BUILD)

$(PCH_FILE): $(PCH_BUILD)
	$(MAKE) PCH_FLAGS= pch-file

pch-file:
	$(CXXCOMPILE) -x c++-header $(PCH_BUILD) -o $(PCH_FILE) \
        -MT $(PCH_FILE)  -MD -MP -MF "./$(PCH_FILE).Tdep" \
        && mv "./$(PCH_FILE).Tdep" "./$(PCH_FILE).dep" \
        || rm "./$(PCH_FILE).Tdep"
endif

SUFFIXES = .gch

AM_CPPFLAGS =
AM_CFLAGS = $(AM_CPPFLAGS) -DTEMP_STORE=1 -DNDEBUG -DBOOST_DISABLE_THREADS -DBOOST_SP_DISABLE_THREADS -DSQLITE_OMIT_CURSOR $(SQLITE_CPPFLAGS)
AM_CXXFLAGS = $(AM_CPPFLAGS) -DNDEBUG -DBOOST_DISABLE_THREADS -DBOOST_SP_DISABLE_THREADS
AM_LDFLAGS = 

lib3rdparty_a_CPPFLAGS =
lib3rdparty_a_CFLAGS = $(AM_CFLAGS) $(LIB3RDPARTY_CFLAGS)
lib3rdparty_a_CXXFLAGS = $(AM_CXXFLAGS) $(LIB3RDPARTY_CXXFLAGS)

monotone_LDFLAGS = 
monotone_CPPFLAGS = -I$(top_srcdir)/lua -I$(top_srcdir)/sqlite
monotone_CFLAGS = $(AM_CFLAGS) $(MONOTONE_CFLAGS)
monotone_CXXFLAGS = $(AM_CXXFLAGS) $(PCH_FLAGS) $(MONOTONE_CXXFLAGS)

unit_tests_LDFLAGS = 
unit_tests_CPPFLAGS = -DBUILD_UNIT_TESTS -I$(top_srcdir)/lua -I$(top_srcdir)/sqlite
unit_tests_CXXFLAGS = $(AM_CXXFLAGS) $(PCH_FLAGS)

# conditionals from configury

if STATIC_BOOST
  BOOSTLIBS = @BOOST_LIBDIR@/libboost_filesystem$(BOOST_SUFFIX).a \
              @BOOST_LIBDIR@/libboost_date_time$(BOOST_SUFFIX).a \
              @BOOST_LIBDIR@/libboost_regex$(BOOST_SUFFIX).a
  monotone_LDADD = lib3rdparty.a $(BOOSTLIBS)
  unit_tests_LDADD = lib3rdparty.a $(BOOSTLIBS) \
                     @BOOST_LIBDIR@/libboost_unit_test_framework$(BOOST_SUFFIX).a
else
  monotone_LDADD = lib3rdparty.a
  unit_tests_LDADD = lib3rdparty.a -lboost_unit_test_framework$(BOOST_SUFFIX)
endif

monotone_LDADD += libplatform.a $(LIBICONV) $(LIBINTL)
unit_tests_LDADD += libplatform.a $(LIBICONV) $(LIBINTL)

if WIN32_PLATFORM
  libplatform_a_SOURCES += $(WIN32_PLATFORM_SOURCES)
  monotone_LDADD += -lshfolder -lws2_32 -lintl -liconv -liphlpapi
  unit_tests_LDADD += -lshfolder -lws2_32 -lintl -liconv -liphlpapi
  lib3rdparty_a_CPPFLAGS += -DWIN32 -DBOTAN_EXT_ENTROPY_SRC_CAPI -DBOTAN_EXT_ENTROPY_SRC_WIN32
  lib3rdparty_a_SOURCES += botan/es_capi.cpp botan/es_win32.cpp
else
  libplatform_a_SOURCES += $(UNIX_PLATFORM_SOURCES)
endif

if MISSING_INET_PTON
  lib3rdparty_a_SOURCES += netxx/inet_pton.cxx netxx/inet_pton.h
  lib3rdparty_a_CPPFLAGS += -DNETXX_NO_PTON
endif    

if MISSING_INET_NTOP
  lib3rdparty_a_SOURCES += netxx/inet_ntop.cxx netxx/inet_ntop.h
  lib3rdparty_a_CPPFLAGS += -DNETXX_NO_NTOP
endif    

if MISSING_GETADDRINFO
  lib3rdparty_a_SOURCES += netxx/resolve_gethostbyname.cxx netxx/resolve_getservbyname.cxx
else
  lib3rdparty_a_SOURCES += netxx/resolve_getaddrinfo.cxx
endif    

if MISSING_INET6
  lib3rdparty_a_CPPFLAGS += -DNETXX_NO_INET6
endif

# i18n support

SUBDIRS = po
datadir = @datadir@
localedir = $(datadir)/locale
DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@

# extra files

PDF_FIGURES= figures/branch-heads.pdf figures/cert.pdf				\
	figures/difference-between-versions.pdf					\
	figures/file-id-manifest-id.pdf figures/fork.pdf			\
	figures/general-workflow.pdf figures/linear-history.pdf			\
	figures/local-workflow.pdf figures/manifest.pdf figures/merge.pdf	\
	figures/network-workflow.pdf figures/parent-child-hashes.pdf		\
	figures/parent-child-names-hashes.pdf figures/parent-child.pdf		\
	figures/revision-chaining.pdf figures/revision.pdf			\
	figures/statement.pdf figures/three-versions.pdf			\
	figures/two-branches.pdf

PNG_FIGURES=$(addprefix $(top_builddir)/figures/,$(notdir $(PDF_FIGURES:.pdf=.png)))
EPS_FIGURES=$(addprefix $(top_builddir)/figures/,$(notdir $(PDF_FIGURES:.pdf=.eps)))

man1_MANS = monotone.1
info_TEXINFOS = monotone.texi
EXTRA_DIST = $(man1_MANS) \
	     $(PDF_FIGURES) \
	     $(PNG_FIGURES) \
             INSTALL \
             README.changesets \
             UPGRADE \
             monotone.spec \
             monotone.html \
             texinfo.css \
	     schema.sql views.sql \
	     std_hooks.lua test_hooks.lua \
	     testsuite.at \
	     $(wildcard $(srcdir)/tests/t_*.at) \
	     testsuite \
             tests/atconfig.in \
             tests/atlocal.in  \
	     package.m4 \
	     package_revision.txt package_full_revision_dist.txt \
             $(wildcard $(srcdir)/m4/*.m4) \
             popt/README popt/CHANGES popt/COPYING popt/popt.3 popt/popt.ps \
             $(wildcard $(srcdir)/popt/test?.c) popt/testit.sh \
             sqlite/keywordhash.h \
             contrib/README \
             contrib/monoprof.sh \
             contrib/monotone-notify.pl \
             contrib/monotone-import.pl \
             contrib/ciabot_monotone.py \
             contrib/monotone.el \
             contrib/monotone-nav.el \
             contrib/monotone.bash_completion \
             contrib/monotone.zsh_completion \
             contrib/mtbrowse.sh \
             contrib/colorize \
             contrib/color-logs.sh \
             contrib/color-logs.conf \
             contrib/get_stdio.pl \
             contrib/ciabot_monotone_hookversion.py \
             contrib/monotone-cvs-ignore.lua \
             contrib/usher.cc \
             win32/monotone.iss \
             $(wildcard $(srcdir)/debian/*)

MAKEINFOFLAGS=-I $(top_builddir)

monotone.info: monotone.texi version.texi std_hooks.lua

monotone.pdf: monotone.texi version.texi std_hooks.lua $(PDF_FIGURES)

monotone.dvi: monotone.texi version.texi std_hooks.lua $(EPS_FIGURES)

#%.eps: %.epsi
#	mv -f $< $@
#
#%.epsi: %.ps
#	ps2epsi $< $@

%.eps : %.ps
	ps2eps <$< >$@

$(top_builddir)/figures/%.ps: $(srcdir)/figures/%.pdf
	mkdir -p $(top_builddir)/figures
#	acroread -toPostScript -pairs $< $@
	pdftops $< $@


# testsuite stuff (could this possibly be more ugly?)

if BUILD_UNIT_TESTS
TESTS = unit_tests $(srcdir)/testsuite
else
TESTS = $(srcdir)/testsuite
endif

TESTS_ENVIRONMENT=AUTOTEST_PATH="."

mostlyclean-local:
	rm -rf testsuite.dir testsuite.log package_full_revision_dist.txt

DISTCLEANFILES = tests/atconfig mt-stdint.h xgettext.opts pch-build.hh.gch.dep

# distcheck stuff

DISTCHECK_CONFIGURE_FLAGS = \
			 --with-bundled-sqlite \
			 --with-bundled-lua \
			 --enable-static-boost

# note: these targets are all in srcdir; they are *not* built during
# the VPATH build part of distcheck (with non-writable source
# directory) but only during the initial dist stage.

$(srcdir)/package.m4: $(top_srcdir)/configure.ac
	{                                      \
	  echo '# Signature of the current package.'; \
	  echo 'm4_define([AT_PACKAGE_NAME],      [@PACKAGE_NAME@])'; \
	  echo 'm4_define([AT_PACKAGE_TARNAME],   [@PACKAGE_TARNAME@])'; \
	  echo 'm4_define([AT_PACKAGE_VERSION],   [@PACKAGE_VERSION@])'; \
	  echo 'm4_define([AT_PACKAGE_STRING],    [@PACKAGE_STRING@])'; \
	  echo 'm4_define([AT_PACKAGE_BUGREPORT], [@PACKAGE_BUGREPORT@])'; \
	} >$(srcdir)/package.m4

$(srcdir)/testsuite: $(srcdir)/testsuite.at $(wildcard $(srcdir)/tests/t_*.at) \
                     $(srcdir)/package.m4 
	autom4te --language=Autotest -I $(srcdir) $< -o $@.tmp
	mv -f $@.tmp $@

# we generate some headers to copy data into the executable

BUILT_SOURCES_CLEAN = std_hooks.h test_hooks.h schema.h views.h \
                      package_revision.txt package_revision.c \
                      package_full_revision.txt package_full_revision.c \
                      $(PCH_FILE) $(PCH_BUILD)
BUILT_SOURCES_NOCLEAN = package_revision.txt package_full_revision_dist.txt
BUILT_SOURCES = $(BUILT_SOURCES_CLEAN) $(BUILT_SOURCES_NOCLEAN)
CLEANFILES = $(BUILT_SOURCES_CLEAN) $(EPS_FIGURES) 

txt2c: txt2c.cc Makefile
	$(CXX) $(CXXFLAGS) -o $@ $<
	chmod 0755 $@$(EXEEXT)

apidocs:
	doxygen doxygen.cfg

%.h: %.sql txt2c Makefile
	./txt2c $< $(*F) >$@.tmp
	cmp -s $@.tmp $@ || mv -f $@.tmp $@
	rm -f $@.tmp

%.h: %.lua txt2c Makefile
	./txt2c $< $(*F) >$@.tmp
	cmp -s $@.tmp $@ || mv -f $@.tmp $@
	rm -f $@.tmp

# A fake target so we can use MT/revision as a prerequisite, yet not
# crash if MT/revision does not exist.
$(srcdir)/MT/revision:
	true
package_revision.txt: $(srcdir)/MT/revision
	if [ -f $< ]; then cp $< $@; fi
	if [ ! -f $@ ]; then echo "unknown" > $@; fi
package_revision.c: package_revision.txt txt2c Makefile
	./txt2c --strip-trailing --no-static $< package_revision >$@.tmp
	cmp -s $@.tmp $@ || mv -f $@.tmp $@
	rm -f $@.tmp

.PHONY: package_full_revision.txt apidocs
package_full_revision.txt:
	REAL_BLDDIR=$$PWD/$(top_builddir); \
	(cd $(srcdir) && $$REAL_BLDDIR/monotone status) 2>/dev/null >$@ \
	|| (cd $(srcdir) && monotone status) 2>/dev/null >$@ \
	|| (cp $(srcdir)/package_full_revision_dist.txt 2>/dev/null $@) \
	|| (echo "unknown" >$@)
package_full_revision_dist.txt: package_full_revision.txt
	cp $< $@
	(echo ''; \
	 echo '  Generated from data cached in the distribution;'; \
	 echo '  further changes may have been made.') >> $@
package_full_revision.c: package_full_revision.txt txt2c Makefile
	./txt2c --no-static $< package_full_revision >$@.tmp
	cmp -s $@.tmp $@ || mv -f $@.tmp $@
	rm -f $@.tmp

# automake doesn't build html docs

monotone.html: monotone.texi version.texi std_hooks.lua texinfo.css
	makeinfo --no-split --no-headers --output $@ --html $<
	perl -i.perlbak -pe 's@<head>@<head>\n<link type="text/css" rel="stylesheet" href="texinfo.css" />@' $@
	rm -f monotone.html.perlbak

# The .perlbak thing is needed, otherwise the perl executions fails on MinGW

html: monotone.texi version.texi std_hooks.lua texinfo.css
	mkdir -p html
	makeinfo --number-sections --html --output html $<
	perl -i.perlbak -pe 's@<head>@<head>\n<link type="text/css" rel="stylesheet" href="texinfo.css" />@' html/*.html
	rm -f html/*.perlbak
	cp texinfo.css html
