/*************************************************
* FIPS 140 Header File                           *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_FIPS140_H__
#define BOTAN_FIPS140_H__

#include <botan/base.h>

namespace Botan {

namespace FIPS140 {

/*************************************************
* FIPS 140-2 Self Tests                          *
*************************************************/
bool passes_self_tests();
bool good_edc(const std::string&, const std::string&);

}

}

#endif
