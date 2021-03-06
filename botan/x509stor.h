/*************************************************
* X.509 Certificate Store Header File            *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_X509_CERT_STORE_H__
#define BOTAN_X509_CERT_STORE_H__

#include <botan/x509cert.h>
#include <botan/x509_crl.h>
#include <botan/certstor.h>

namespace Botan {

/*************************************************
* X.509 Certificate Store                        *
*************************************************/
class X509_Store
   {
   public:
      class Search_Func
         {
         public:
            virtual bool match(const X509_Certificate&) const = 0;
            virtual ~Search_Func() {}
         };

      enum Cert_Usage {
         ANY              = 0x00,
         TLS_SERVER       = 0x01,
         TLS_CLIENT       = 0x02,
         CODE_SIGNING     = 0x04,
         EMAIL_PROTECTION = 0x08,
         TIME_STAMPING    = 0x10,
         CRL_SIGNING      = 0x20
      };

      X509_Code validate_cert(const X509_Certificate&, Cert_Usage = ANY);

      std::vector<X509_Certificate> get_certs(const Search_Func&) const;
      std::vector<X509_Certificate> get_cert_chain(const X509_Certificate&);
      std::string PEM_encode() const;

      X509_Code add_crl(const X509_CRL&);
      void add_cert(const X509_Certificate&, bool = false);
      void add_certs(DataSource&);
      void add_trusted_certs(DataSource&);

      void add_new_certstore(Certificate_Store*);

      static X509_Code check_sig(const X509_Object&, X509_PublicKey*);

      X509_Store();
      X509_Store(const X509_Store&);
      ~X509_Store();
   private:
      X509_Store& operator=(const X509_Store&) { return (*this); }

      class Cert_Info
         {
         public:
            bool is_verified() const;
            bool is_trusted() const;
            X509_Code verify_result() const;
            void set_result(X509_Code) const;
            Cert_Info(const X509_Certificate&, bool = false);

            X509_Certificate cert;
            bool trusted;
         private:
            mutable bool checked;
            mutable X509_Code result;
            mutable u64bit last_checked;
         };

      class CRL_Data
         {
         public:
            X509_DN issuer;
            MemoryVector<byte> serial, auth_key_id;
            bool operator==(const CRL_Data&) const;
            bool operator!=(const CRL_Data&) const;
            bool operator<(const CRL_Data&) const;
         };

      u32bit find_cert(const X509_DN&, const MemoryRegion<byte>&) const;
      X509_Code check_sig(const Cert_Info&, const Cert_Info&) const;
      void recompute_revoked_info() const;

      void do_add_certs(DataSource&, bool);
      X509_Code construct_cert_chain(const X509_Certificate&,
                                     std::vector<u32bit>&, bool = false);

      u32bit find_parent_of(const X509_Certificate&);
      bool is_revoked(const X509_Certificate&) const;

      static const u32bit NO_CERT_FOUND = 0xFFFFFFFF;
      std::vector<Cert_Info> certs;
      std::vector<CRL_Data> revoked;
      std::vector<Certificate_Store*> stores;
      mutable bool revoked_info_valid;
   };

namespace X509_Store_Search {

/*************************************************
* Methods to search through a X509_Store         *
*************************************************/
std::vector<X509_Certificate> by_email(const X509_Store&, const std::string&);
std::vector<X509_Certificate> by_name(const X509_Store&, const std::string&);
std::vector<X509_Certificate> by_dns(const X509_Store&, const std::string&);
std::vector<X509_Certificate> by_keyid(const X509_Store&, u64bit);
std::vector<X509_Certificate> by_iands(const X509_Store&, const X509_DN&,
                                       const MemoryRegion<byte>&);
std::vector<X509_Certificate> by_SKID(const X509_Store&,
                                      const MemoryRegion<byte>&);

}

}

#endif
