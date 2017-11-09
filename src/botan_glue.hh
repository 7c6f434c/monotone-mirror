// Copyright (C) 2017 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __BOTAN_GLUE_H__
#define __BOTAN_GLUE_H__

// This header file represents a very tiny wrapper around Botan to abstract
// a bit from the various versions monotone supports.  It certainly
// deduplicates some of the version checks.  All compilation units that
// would otherwise require at least one BOTAN_VERSION_CODE condition should
// instead include this header, so the conditional code is centralized.

#include <memory>
#include <stdexcept>

#include <botan/version.h>
#include <botan/loadstor.h>
#include <botan/filters.h>

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


#if BOTAN_VERSION_CODE < BOTAN_VERSION_CODE_FOR(1,11,14)
  #include <botan/init.h>
#endif


#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(2,0,0)
  typedef Botan::secure_vector<Botan::byte> secure_byte_vector;
#else
  typedef Botan::SecureVector<Botan::byte> secure_byte_vector;
#endif

extern void initialize_botan(bool for_testing = false);

// Botan version agnostic helper functions for loading a private key.
class Passphrase_Required : public std::runtime_error
  { using std::runtime_error::runtime_error; };

extern std::shared_ptr<Botan::PKCS8_PrivateKey>
load_pkcs8_key(std::string const & name, std::string const & kp);

#endif   // __BOTAN_GLUE_H__
