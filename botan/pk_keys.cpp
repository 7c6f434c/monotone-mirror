/*************************************************
* PK Key Types Source File                       *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/pk_keys.h>
#include <botan/conf.h>
#include <botan/oids.h>

namespace Botan {

namespace {

/*************************************************
* Find out how much testing should be performed  *
*************************************************/
bool key_check_level(const std::string& type)
   {
   const std::string setting = Config::get_string("pk/test/" + type);
   if(setting == "basic")
      return false;
   return true;
   }

}

/*************************************************
* Default OID access                             *
*************************************************/
OID PK_Key::get_oid() const
   {
   try {
      return OIDS::lookup(algo_name());
      }
   catch(Lookup_Error)
      {
      throw Lookup_Error("PK algo " + algo_name() + " has no defined OIDs");
      }
   }

/*************************************************
* Run checks on a loaded public key              *
*************************************************/
void PK_Key::check_loaded_public() const
   {
   if(!check_key(key_check_level("public")))
      throw Invalid_Argument(algo_name() + ": Invalid public key");
   }

/*************************************************
* Run checks on a loaded private key             *
*************************************************/
void PK_Key::check_loaded_private() const
   {
   if(!check_key(key_check_level("private")))
      throw Invalid_Argument(algo_name() + ": Invalid private key");
   }

/*************************************************
* Run checks on a generated private key          *
*************************************************/
void PK_Key::check_generated_private() const
   {
   if(!check_key(key_check_level("private_gen")))
      throw Self_Test_Failure(algo_name() + " private key generation failed");
   }

}
