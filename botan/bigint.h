/*************************************************
* BigInt Header File                             *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_BIGINT_H__
#define BOTAN_BIGINT_H__

#include <botan/base.h>
#include <botan/mp_types.h>
#include <iosfwd>

namespace Botan {

/*************************************************
* BigInt                                         *
*************************************************/
class BigInt
   {
   public:
      enum Base { Octal = 8, Decimal = 10, Hexadecimal = 16, Binary = 256 };
      enum Sign { Negative = 0, Positive = 1 };
      enum NumberType { Random, Power2 };

      struct DivideByZero : public Exception
         { DivideByZero() : Exception("BigInt divide by zero") {} };

      BigInt& operator+=(const BigInt&);
      BigInt& operator-=(const BigInt&);

      BigInt& operator*=(const BigInt&);
      BigInt& operator/=(const BigInt&);
      BigInt& operator%=(const BigInt&);
      word    operator%=(word);
      BigInt& operator<<=(u32bit);
      BigInt& operator>>=(u32bit);

      BigInt& operator++();
      BigInt& operator--();
      BigInt  operator++(int) { BigInt tmp = (*this); ++(*this); return tmp; }
      BigInt  operator--(int) { BigInt tmp = (*this); --(*this); return tmp; }

      BigInt operator-() const;
      bool operator !() const { return (!is_nonzero()); }

      void add(word);
      void sub(word);

      s32bit cmp(const BigInt&, bool = true) const;
      bool is_even() const { return (get_bit(0) == 0); }
      bool is_odd()  const { return (get_bit(0) == 1); }
      bool is_nonzero() const { return (!is_zero()); }
      bool is_zero() const;

      void set_bit(u32bit);
      void clear_bit(u32bit);
      void mask_bits(u32bit);

      bool get_bit(u32bit) const;
      u32bit get_nibble(u32bit, u32bit) const;
      byte byte_at(u32bit) const;
      word word_at(u32bit) const;

      u32bit to_u32bit() const;

      bool is_negative() const { return (sign() == Negative); }
      bool is_positive() const { return (sign() == Positive); }
      Sign sign() const { return (signedness); }
      Sign reverse_sign() const;
      void flip_sign();
      void set_sign(Sign);
      BigInt abs() const;

      u32bit size() const { return reg.size(); }
      u32bit sig_words() const;
      u32bit bytes() const;
      u32bit bits() const;

      const word* data() const { return reg.begin(); }
      SecureVector<word>& get_reg() { return reg; }
      void grow_reg(u32bit n) const { reg.grow_by(n); }

      word& operator[](u32bit index) { return reg[index]; }
      word operator[](u32bit index) const { return reg[index]; }
      void clear() { reg.clear(); }

      void randomize(u32bit = 0, RNG_Quality = SessionKey);

      void binary_encode(byte[]) const;
      void binary_decode(const byte[], u32bit);
      u32bit encoded_size(Base = Binary) const;

      static SecureVector<byte> encode(const BigInt&, Base = Binary);
      static void encode(byte[], const BigInt&, Base = Binary);
      static BigInt decode(const byte[], u32bit, Base = Binary);
      static BigInt decode(const MemoryRegion<byte>&, Base = Binary);
      static SecureVector<byte> encode_1363(const BigInt&, u32bit);

      void swap(BigInt&);

      BigInt(u64bit = 0);
      BigInt(const BigInt&);
      BigInt(const std::string&);
      BigInt(const byte[], u32bit, Base = Binary);
      BigInt(Sign, u32bit);
      BigInt(NumberType, u32bit);
   private:
      friend void modifying_divide(BigInt&, BigInt&, BigInt&);
      void grow_to(u32bit n) const { reg.grow_to(n); }
      Sign signedness;
      SecureVector<word> reg;
   };

/*************************************************
* Arithmetic Operators                           *
*************************************************/
BigInt operator+(const BigInt&, const BigInt&);
BigInt operator-(const BigInt&, const BigInt&);
BigInt operator*(const BigInt&, const BigInt&);
BigInt operator/(const BigInt&, const BigInt&);
BigInt operator%(const BigInt&, const BigInt&);
word   operator%(const BigInt&, word);
BigInt operator<<(const BigInt&, u32bit);
BigInt operator>>(const BigInt&, u32bit);

/*************************************************
* Comparison Operators                           *
*************************************************/
inline bool operator==(const BigInt& a, const BigInt& b)
   { return (a.cmp(b) == 0); }
inline bool operator!=(const BigInt& a, const BigInt& b)
   { return (a.cmp(b) != 0); }
inline bool operator<=(const BigInt& a, const BigInt& b)
   { return (a.cmp(b) <= 0); }
inline bool operator>=(const BigInt& a, const BigInt& b)
   { return (a.cmp(b) >= 0); }
inline bool operator<(const BigInt& a, const BigInt& b)
   { return (a.cmp(b) < 0); }
inline bool operator>(const BigInt& a, const BigInt& b)
   { return (a.cmp(b) > 0); }

/*************************************************
* I/O Operators                                  *
*************************************************/
std::ostream& operator<<(std::ostream&, const BigInt&);
std::istream& operator>>(std::istream&, BigInt&);

}

namespace std {

inline void swap(Botan::BigInt& a, Botan::BigInt& b) { a.swap(b); }

}

#endif
