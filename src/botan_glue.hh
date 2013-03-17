// Copyright (C) 2013 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __BOTAN_GLUE_HH__
#define __BOTAN_GLUE_HH__

#include <botan/botan.h>

#include "sanity.hh"

// Botan prior to 1.11.0 used SecureVector, it then changed to define
// secure_vector instead - with the benefit of it being more like
// std::vector. To reduce the amount of Botan version dependent code, we
// define secure_byte_vector as required.
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,11,0)
typedef Botan::secure_vector<Botan::byte> secure_byte_vector;
#else
typedef Botan::SecureVector<Botan::byte> secure_byte_vector;
#endif

#endif // __BOTAN_PIPE_CACHE_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
