// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//               2017 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../../../src/vector.hh"

#include "../unit_tests.hh"
#include "../../../src/transforms.hh"

using std::string;
using std::vector;

UNIT_TEST(gzip_encode)
{
  vector<unsigned char> const
    EXP_HDR = {0x1f, 0x8b, 0x08, 0x00},
    EXP_CRC = {0x37, 0xf0, 0x29, 0xb6},
    EXP_LEN = {0x11, 0, 0, 0};

  gzip<data> gzd;
  encode_gzip(data("the rain in spain"), gzd);

  // gzip header and footer alone weights 18 bytes.
  UNIT_TEST_CHECK(gzd().size() > 18);

  // check the gzip header - first four bytes
  string hdr = gzd().substr(0, 4);
  UNIT_TEST_CHECK(hdr == string(EXP_HDR.begin(), EXP_HDR.end()));

  if (gzd().size() > 8)
    {
      // check the trailor's CRC
      string crc = gzd().substr(gzd().size() - 8, 4);
      UNIT_TEST_CHECK(crc == string(EXP_CRC.begin(), EXP_CRC.end()));

      // check the trailor's length info.
      string len = gzd().substr(gzd().size() - 4);
      UNIT_TEST_CHECK(len == string(EXP_LEN.begin(), EXP_LEN.end()));
    }
}

UNIT_TEST(gzip_decode_old)
{
  // "the rain in spain" gzipped with monotone's old gzip logic, which
  // doesn't set the timestamp and indicates a length of zero in the
  // trailer.
  base64< gzip<data> >
    bgzd("H4sIAAAAAAAA/yvJSFUoSszMUwCi4gIgAwA38Cm2EQAAAA==");
  gzip<data> gzd2;
  gzd2 = decode_base64(bgzd);
  data d2;
  decode_gzip(gzd2, d2);
  UNIT_TEST_CHECK(d2() == "the rain in spain");
}

UNIT_TEST(gzip_roundtrip)
{
  data d2, d1("the rain in spain");
  gzip<data> gzd;
  encode_gzip(d1, gzd);
  decode_gzip(gzd, d2);
  UNIT_TEST_CHECK(d2 == d1);
}

UNIT_TEST(base64_decode)
{
  base64<data> in("dGhlIHJhaW4gaW4gc3BhaW4=");
  data out = decode_base64(in);
  UNIT_TEST_CHECK(out() == "the rain in spain");
}

UNIT_TEST(base64_encode)
{
  data in("the rain in spain");
  base64<data> out = encode_base64(in);
  UNIT_TEST_CHECK(out() == "dGhlIHJhaW4gaW4gc3BhaW4=");
}

UNIT_TEST(gzip_base64_roundtrip)
{
  data d2, d1("the rain in spain");
  gzip<data> gzd1, gzd2;
  base64< gzip<data> > bgzd;
  encode_gzip(d1, gzd1);
  bgzd = encode_base64(gzd1);
  gzd2 = decode_base64(bgzd);
  UNIT_TEST_CHECK(gzd2 == gzd1);
  decode_gzip(gzd2, d2);
  UNIT_TEST_CHECK(d2 == d1);
}

UNIT_TEST(calculate_ident)
{
  data input(string("the only blender which can be turned into the most powerful vaccum cleaner"),
             origin::internal);
  string ident("86e03bdb3870e2a207dfd0dcbfd4c4f2e3bc97bd");
  id output = calculate_ident(input);
  UNIT_TEST_CHECK(output() == decode_hexenc(ident, origin::internal));
}

UNIT_TEST(corruption_check)
{
  data input(string("i'm so fragile, fragile when you're here"),
             origin::internal);
  gzip<data> gzd;
  encode_gzip(input, gzd);

  // fake a single-bit error
  string gzs = gzd();
  string::iterator i = gzs.begin();
  while (*i != '+')
    i++;
  *i = 'k';

  gzip<data> gzbad(gzs, origin::network);
  data output;
  UNIT_TEST_CHECK_THROW(decode_gzip(gzbad, output), recoverable_failure);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
