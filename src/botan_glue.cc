// Copyright (C) 2017 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include <memory>

#include "botan_glue.hh"
#include <botan/pkcs8.h>
#include <botan/pk_keys.h>
#include <botan/auto_rng.h>

#include <botan/x509_key.h>
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,9,11) && \
    BOTAN_VERSION_CODE < BOTAN_VERSION_CODE_FOR(1,11,0)
  #include <botan/ui.h>
#endif

#include "gzip.hh"
#include "lazy_rng.hh"

using std::make_shared;
using std::shared_ptr;
using std::string;
using Botan::PKCS8_PrivateKey;


void
initialize_botan(bool for_testing)
{
#if BOTAN_VERSION_CODE < BOTAN_VERSION_CODE_FOR(1,11,14)
  // Set up some global state, like secure memory allocation etc.
  Botan::LibraryInitializer acquire_botan(
    for_testing
      ? "thread_safe=0 selftest=1 seed_rng=1 use_engines=0 "
          "secure_memory=1 fips140=0"
      : "thread_safe=0 selftest=0 seed_rng=1 use_engines=0 "
          "secure_memory=1 fips140=0"
  );
#endif
}


// A helper class implementing Botan::User_Interface - which doesn't really
// interface with the user, but provides the necessary plumbing for Botan.
//
// See Botan commit 2d09d7d0cd4bd0e7155d001dd65a4f29103b158c
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,9,11) && \
  BOTAN_VERSION_CODE < BOTAN_VERSION_CODE_FOR(1,11,0)
class Dummy_UI : public Botan::User_Interface
{
public:
  virtual std::string get_passphrase(const std::string &, const std::string &,
                                     Botan::User_Interface::UI_Result &) const;
};

std::string
Dummy_UI::get_passphrase(const std::string &, const std::string &,
                         Botan::User_Interface::UI_Result&) const
{
  throw Passphrase_Required("Passphrase required");
}
#endif


// A simplistic function throwing the Passphrase_Required exception. To be
// passed to Botan as a callback.
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(2,0,0)
std::function<std::string ()> pass_req_throw_func =
  [] ()
    {
      throw Passphrase_Required("Passphrase required");
      return string();
    };
#elif BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,11,0)
std::function<std::pair<bool, std::string> ()> pass_req_throw_func =
  [] ()
    {
      throw Passphrase_Required("Passphrase required");
      return std::pair<bool, string>();
    };
#endif


// A Botan-version agnostic key loader function trying to load an
// unprotected key, i.e. one that loads without any password.
PKCS8_PrivateKey *
load_pkcs8_key(string const & priv_key)
{
  shared_ptr<PKCS8_PrivateKey> pkcs8_key;
  Botan::DataSource_Memory ds(priv_key);
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,11,0)
  return Botan::PKCS8::load_key(ds, lazy_rng::get(),
                                pass_req_throw_func);
#elif BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,9,11)
  return Botan::PKCS8::load_key(ds, lazy_rng::get(),
                                         Dummy_UI());
#else
  return Botan::PKCS8::load_key(ds, lazy_rng::get(), "");
#endif
}
