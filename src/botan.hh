// Copyright (C) 2017 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

// This header file represents a very tiny wrapper around Botan to abstract
// a bit from the various versions monotone supports.  It certainly
// deduplicates some of the version checks.  Every compilation unit that
// requires Botan should include this file, preferably after base.hh and
// the standard library includes.

#include <botan/botan.h>

#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
  #include <botan/loadstor.h>
  #include <botan/filters.h>
#endif

#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,11,0)
  #include <botan/pubkey.h>
#else
  #include <botan/look_pk.h>
#endif


#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,11,10) \
    && defined(BOTAN_HAS_ZLIB)
  #include <botan/comp_filter.h>
#else
  // use the custom gzip code otherwise
#endif

#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(2,0,0)
  typedef Botan::secure_vector<Botan::byte> secure_byte_vector;
#else
  typedef Botan::SecureVector<Botan::byte> secure_byte_vector;
#endif
