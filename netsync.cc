// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <string>
#include <memory>
#include <list>
#include <deque>
#include <stack>

#include <time.h>

#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/regex.hpp>

#include "app_state.hh"
#include "cert.hh"
#include "constants.hh"
#include "keys.hh"
#include "merkle_tree.hh"
#include "netcmd.hh"
#include "netio.hh"
#include "netsync.hh"
#include "numeric_vocab.hh"
#include "packet.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"
#include "xdelta.hh"
#include "epoch.hh"
#include "platform.hh"
#include "hmac.hh"
#include "globish.hh"

#include "botan/botan.h"

#include "netxx/address.h"
#include "netxx/peer.h"
#include "netxx/probe.h"
#include "netxx/socket.h"
#include "netxx/stream.h"
#include "netxx/streamserver.h"
#include "netxx/timeout.h"

// TODO: things to do that will break protocol compatibility
//   -- need some way to upgrade anonymous to keyed pull, without user having
//      to explicitly specify which they want
//      just having a way to respond "access denied, try again" might work
//      but perhaps better to have the anonymous command include a note "I
//      _could_ use key <...> if you prefer", and if that would lead to more
//      access, could reply "I do prefer".  (Does this lead to too much
//      information exposure?  Allows anonymous people to probe what branches
//      a key has access to.)
//   -- "warning" packet type?
//   -- Richard Levitte wants, when you (e.g.) request '*' but don't access to
//      all of it, you just get the parts you have access to (maybe with
//      warnings about skipped branches).  to do this right, should have a way
//      for the server to send back to the client "right, you're not getting
//      the following branches: ...", so the client will not include them in
//      its merkle trie.
//   -- add some sort of vhost field to the client's first packet, saying who
//      they expect to talk to
//   -- connection teardown is flawed:
//      -- simple bug: often connections "fail" even though they succeeded.
//         should figure out why.  (Possibly one side doesn't wait for their
//         goodbye packet to drain before closing the socket?)
//      -- subtle misdesign: "goodbye" packets indicate completion of data
//         transfer.  they do not indicate that data has been written to
//         disk.  there should be some way to indicate that data has been
//         successfully written to disk.  See message (and thread)
//         <E0420553-34F3-45E8-9DA4-D8A5CB9B0600@hsdev.com> on
//         monotone-devel.
//   -- apparently we have a IANA approved port: 4691.  I guess we should
//      switch to using that.

//
// this is the "new" network synchronization (netsync) system in
// monotone. it is based on synchronizing a pair of merkle trees over an
// interactive connection.
//
// a netsync process between peers treats each peer as either a source, a
// sink, or both. when a peer is only a source, it will not write any new
// items to its database. when a peer is only a sink, it will not send any
// items from its database. when a peer is both a source and sink, it may
// send and write items freely.
//
// the post-state of a netsync is that each sink contains a superset of the
// items in its corresponding source; when peers are behaving as both
// source and sink, this means that the post-state of the sync is for the
// peers to have identical item sets.
//
// a peer can be a sink in at most one netsync process at a time; it can
// however be a source for multiple netsyncs simultaneously.
//
//
// data structure
// --------------
//
// each node in a merkle tree contains a fixed number of slots. this number
// is derived from a global parameter of the protocol -- the tree fanout --
// such that the number of slots is 2^fanout. for now we will assume that
// fanout is 4 thus there are 16 slots in a node, because this makes
// illustration easier. the other parameter of the protocol is the size of
// a hash; we use SHA1 so the hash is 20 bytes (160 bits) long.
//
// each slot in a merkle tree node is in one of 4 states:
//
//   - empty
//   - live leaf
//   - dead leaf
//   - subtree
//   
// in addition, each live or dead leaf contains a hash code which
// identifies an element of the set being synchronized. each subtree slot
// contains a hash code of the node immediately beneath it in the merkle
// tree. empty slots contain no hash codes.
//
// each node also summarizes, for sake of statistic-gathering, the number
// of set elements and total number of bytes in all of its subtrees, each
// stored as a size_t and sent as a uleb128.
//
// since empty slots have no hash code, they are represented implicitly by
// a bitmap at the head of each merkle tree node. as an additional
// integrity check, each merkle tree node contains a label indicating its
// prefix in the tree, and a hash of its own contents.
//
// in total, then, the byte-level representation of a <160,4> merkle tree
// node is as follows:
//
//      20 bytes       - hash of the remaining bytes in the node
//       1 byte        - type of this node (manifest, file, key, mcert, fcert)
//     1-N bytes       - level of this node in the tree (0 == "root", uleb128)
//    0-20 bytes       - the prefix of this node, 4 bits * level, 
//                       rounded up to a byte
//     1-N bytes       - number of leaves under this node (uleb128)
//       4 bytes       - slot-state bitmap of the node
//   0-320 bytes       - between 0 and 16 live slots in the node
//
// so, in the worst case such a node is 367 bytes, with these parameters.
//
//
// protocol
// --------
//
// The protocol is a simple binary command-packet system over TCP;
// each packet consists of a single byte which identifies the protocol
// version, a byte which identifies the command name inside that
// version, a size_t sent as a uleb128 indicating the length of the
// packet, that many bytes of payload, and finally 20 bytes of SHA-1
// HMAC calculated over the payload.  The key for the SHA-1 HMAC is 20
// bytes of 0 during authentication, and a 20-byte random key chosen
// by the client after authentication (discussed below).
// decoding involves simply buffering until a sufficient number of bytes are
// received, then advancing the buffer pointer. any time an integrity check
// (the HMAC) fails, the protocol is assumed to have lost synchronization, and
// the connection is dropped. the parties are free to drop the tcp stream at
// any point, if too much data is received or too much idle time passes; no
// commitments or transactions are made.
//
// one special command, "bye", is used to shut down a connection
// gracefully.  once each side has received all the data they want, they
// can send a "bye" command to the other side. as soon as either side has
// both sent and received a "bye" command, they drop the connection. if
// either side sees an i/o failure (dropped connection) after they have
// sent a "bye" command, they consider the shutdown successful.
//
// the exchange begins in a non-authenticated state. the server sends a
// "hello <id> <nonce>" command, which identifies the server's RSA key and
// issues a nonce which must be used for a subsequent authentication.
//
// The client then responds with either:
//
// An "auth (source|sink|both) <include_pattern> <exclude_pattern> <id>
// <nonce1> <hmac key> <sig>" command, which identifies its RSA key, notes the
// role it wishes to play in the synchronization, identifies the pattern it
// wishes to sync with, signs the previous nonce with its own key, and informs
// the server of the HMAC key it wishes to use for this session (encrypted
// with the server's public key); or
//
// An "anonymous (source|sink|both) <include_pattern> <exclude_pattern>
// <hmac key>" command, which identifies the role it wishes to play in the
// synchronization, the pattern it ishes to sync with, and the HMAC key it
// wishes to use for this session (also encrypted with the server's public
// key).
//
// The server then replies with a "confirm" command, which contains no
// other data but will only have the correct HMAC integrity code if
// the server received and properly decrypted the HMAC key offered by
// the client.  This transitions the peers into an authenticated state
// and begins refinement.
//
// refinement begins with the client sending its root public key and
// manifest certificate merkle nodes to the server. the server then
// compares the root to each slot in *its* root node, and for each slot
// either sends refined subtrees to the client, or (if it detects a missing
// item in one pattern or the other) sends either "data" or "send_data"
// commands corresponding to the role of the missing item (source or
// sink). the client then receives each refined subtree and compares it
// with its own, performing similar description/request behavior depending
// on role, and the cycle continues.
//
// detecting the end of refinement is subtle: after sending the refinement
// of the root node, the server sends a "done 0" command (queued behind all
// the other refinement traffic). when either peer receives a "done N"
// command it immediately responds with a "done N+1" command. when two done
// commands for a given merkle tree arrive with no interveining refinements,
// the entire merkle tree is considered complete.
//
// any "send_data" command received prompts a "data" command in response,
// if the requested item exists. if an item does not exist, a "nonexistant"
// response command is sent. 
//
// once a response is received for each requested key and revision cert
// (either data or nonexistant) the requesting party walks the graph of
// received revision certs and transmits send_data or send_delta commands
// for all the revisions mentionned in the certs which it does not already
// have in its database.
//
// for each revision it receives, the recipient requests all the file data or
// deltas described in that revision.
//
// once all requested files, manifests, revisions and certs are received (or
// noted as nonexistant), the recipient closes its connection.
//
// (aside: this protocol is raw binary because coding density is actually
// important here, and each packet consists of very information-dense
// material that you wouldn't have a hope of typing in manually anyways)
//

using namespace std;
using boost::shared_ptr;
using boost::lexical_cast;

static inline void 
require(bool check, string const & context)
{
  if (!check) 
    throw bad_decode(F("check of '%s' failed") % context);
}

struct 
done_marker
{
  bool current_level_had_refinements;
  bool tree_is_done;
  done_marker() : 
    current_level_had_refinements(false), 
    tree_is_done(false) 
  {}
};

struct 
session
{
  protocol_role role;
  protocol_voice const voice;
  utf8 const & our_include_pattern;
  utf8 const & our_exclude_pattern;
  globish_matcher our_matcher;
  app_state & app;

  string peer_id;
  Netxx::socket_type fd;
  Netxx::Stream str;  

  string_queue inbuf; 
  // deque of pair<string data, size_t cur_pos>
  deque< pair<string,size_t> > outbuf; 
  // the total data stored in outbuf - this is
  // used as a valve to stop too much data
  // backing up
  size_t outbuf_size;

  netcmd cmd;
  bool armed;
  bool arm();

  id remote_peer_key_hash;
  rsa_keypair_id remote_peer_key_name;
  netsync_session_key session_key;
  chained_hmac read_hmac;
  chained_hmac write_hmac;
  bool authenticated;

  time_t last_io_time;
  auto_ptr<ticker> byte_in_ticker;
  auto_ptr<ticker> byte_out_ticker;
  auto_ptr<ticker> cert_in_ticker;
  auto_ptr<ticker> cert_out_ticker;
  auto_ptr<ticker> revision_in_ticker;
  auto_ptr<ticker> revision_out_ticker;
  auto_ptr<ticker> revision_checked_ticker;
  
  vector<revision_id> written_revisions;
  vector<rsa_keypair_id> written_keys;
  vector<cert> written_certs;

  map<netcmd_item_type, boost::shared_ptr<merkle_table> > merkle_tables;

  map<netcmd_item_type, done_marker> done_refinements;
  map<netcmd_item_type, boost::shared_ptr< set<id> > > requested_items;
  map<netcmd_item_type, boost::shared_ptr< set<id> > > received_items;
  map<netcmd_item_type, boost::shared_ptr< set<id> > > full_delta_items;
  map<revision_id, boost::shared_ptr< pair<revision_data, revision_set> > > ancestry;
  map<revision_id, map<cert_name, vector<cert> > > received_certs;
  set< pair<id, id> > reverse_delta_requests;
  bool analyzed_ancestry;

  id saved_nonce;
  bool received_goodbye;
  bool sent_goodbye;

  packet_db_valve dbw;

  session(protocol_role role,
          protocol_voice voice,
          utf8 const & our_include_pattern,
          utf8 const & our_exclude_pattern,
          app_state & app,
          string const & peer,
          Netxx::socket_type sock, 
          Netxx::Timeout const & to);

  virtual ~session();
  
  void rev_written_callback(revision_id rid);
  void key_written_callback(rsa_keypair_id kid);
  void cert_written_callback(cert const & c);

  id mk_nonce();
  void mark_recent_io();

  void set_session_key(string const & key);
  void set_session_key(rsa_oaep_sha_data const & key_encrypted);

  void setup_client_tickers();

  bool done_all_refinements();
  bool cert_refinement_done();
  bool all_requested_revisions_received();

  void note_item_requested(netcmd_item_type ty, id const & i);
  void note_item_full_delta(netcmd_item_type ty, id const & ident);
  bool item_already_requested(netcmd_item_type ty, id const & i);
  void note_item_arrived(netcmd_item_type ty, id const & i);

  void maybe_note_epochs_finished();

  void note_item_sent(netcmd_item_type ty, id const & i);

  bool got_all_data();
  void maybe_say_goodbye();

  void get_heads_and_consume_certs(set<revision_id> & heads);

  void analyze_ancestry_graph();
  void analyze_manifest(manifest_map const & man);

  Netxx::Probe::ready_type which_events() const;
  bool read_some();
  bool write_some();

  bool encountered_error;
  void error(string const & errmsg);

  void write_netcmd_and_try_flush(netcmd const & cmd);
  void queue_bye_cmd();
  void queue_error_cmd(string const & errmsg);
  void queue_done_cmd(size_t level, netcmd_item_type type);
  void queue_hello_cmd(rsa_keypair_id const & key_name,
                       base64<rsa_pub_key> const & pub_encoded, 
                       id const & nonce);
  void queue_anonymous_cmd(protocol_role role, 
                           utf8 const & include_pattern, 
                           utf8 const & exclude_pattern, 
                           id const & nonce2,
                           base64<rsa_pub_key> server_key_encoded);
  void queue_auth_cmd(protocol_role role, 
                      utf8 const & include_pattern, 
                      utf8 const & exclude_pattern, 
                      id const & client, 
                      id const & nonce1, 
                      id const & nonce2, 
                      string const & signature,
                      base64<rsa_pub_key> server_key_encoded);
  void queue_confirm_cmd();
  void queue_refine_cmd(merkle_node const & node);
  void queue_send_data_cmd(netcmd_item_type type, 
                           id const & item);
  void queue_send_delta_cmd(netcmd_item_type type, 
                            id const & base, 
                            id const & ident);
  void queue_data_cmd(netcmd_item_type type, 
                      id const & item,
                      string const & dat);
  void queue_delta_cmd(netcmd_item_type type, 
                       id const & base, 
                       id const & ident, 
                       delta const & del);
  void queue_nonexistant_cmd(netcmd_item_type type, 
                             id const & item);

  bool process_bye_cmd();
  bool process_error_cmd(string const & errmsg);
  bool process_done_cmd(size_t level, netcmd_item_type type);
  bool process_hello_cmd(rsa_keypair_id const & server_keyname,
                         rsa_pub_key const & server_key,
                         id const & nonce);
  bool process_anonymous_cmd(protocol_role role, 
                             utf8 const & their_include_pattern,
                             utf8 const & their_exclude_pattern);
  bool process_auth_cmd(protocol_role role, 
                        utf8 const & their_include_pattern, 
                        utf8 const & their_exclude_pattern, 
                        id const & client, 
                        id const & nonce1, 
                        string const & signature);
  bool process_confirm_cmd(string const & signature);
  void respond_to_confirm_cmd();
  bool process_refine_cmd(merkle_node const & node);
  bool process_send_data_cmd(netcmd_item_type type,
                             id const & item);
  bool process_send_delta_cmd(netcmd_item_type type,
                              id const & base, 
                              id const & ident);
  bool process_data_cmd(netcmd_item_type type,
                        id const & item, 
                        string const & dat);
  bool process_delta_cmd(netcmd_item_type type,
                         id const & base, 
                         id const & ident, 
                         delta const & del);
  bool process_nonexistant_cmd(netcmd_item_type type,
                               id const & item);
  bool process_usher_cmd(utf8 const & msg);

  bool merkle_node_exists(netcmd_item_type type,
                          size_t level,
                          prefix const & pref);

  void load_merkle_node(netcmd_item_type type,
                        size_t level,
                        prefix const & pref,
                        merkle_ptr & node);

  void rebuild_merkle_trees(app_state & app,
                            set<utf8> const & branches);

  bool dispatch_payload(netcmd const & cmd);
  void begin_service();
  bool process();
};

struct
ancestry_fetcher
{
  session & sess;

  // map children to parents
  multimap< file_id, file_id > rev_file_deltas;
  multimap< manifest_id, manifest_id > rev_manifest_deltas;
  // map an ancestor to a child
  multimap< file_id, file_id > fwd_file_deltas;
  multimap< manifest_id, manifest_id > fwd_manifest_deltas;

  set< file_id > seen_files;

  ancestry_fetcher(session & s);
  // analysing the ancestry graph
  void traverse_files(change_set const & cset);
  void traverse_manifest(manifest_id const & child_man,
                         manifest_id const & parent_man);
  void traverse_ancestry(set<revision_id> const & heads);

  // requesting the data
  void request_rev_file_deltas(file_id const & start, 
                               set<file_id> & done_files);
  void request_files();
  void request_rev_manifest_deltas(manifest_id const & start,
                                   set<manifest_id> & done_manifests);
  void request_manifests();


};


struct 
root_prefix
{
  prefix val;
  root_prefix() : val("")
  {}
};

static root_prefix const & 
get_root_prefix()
{ 
  // this is not a static variable for a bizarre reason: mac OSX runs
  // static initializers in the "wrong" order (application before
  // libraries), so the initializer for a static string in cryptopp runs
  // after the initializer for a static variable outside a function
  // here. therefore encode_hexenc() fails in the static initializer here
  // and the program crashes. curious, eh?
  static root_prefix ROOT_PREFIX;
  return ROOT_PREFIX;
}

static file_id null_ident;
  
session::session(protocol_role role,
                 protocol_voice voice,
                 utf8 const & our_include_pattern,
                 utf8 const & our_exclude_pattern,
                 app_state & app,
                 string const & peer,
                 Netxx::socket_type sock, 
                 Netxx::Timeout const & to) : 
  role(role),
  voice(voice),
  our_include_pattern(our_include_pattern),
  our_exclude_pattern(our_exclude_pattern),
  our_matcher(our_include_pattern, our_exclude_pattern),
  app(app),
  peer_id(peer),
  fd(sock),
  str(sock, to),
  inbuf(),
  outbuf_size(0),
  armed(false),
  remote_peer_key_hash(""),
  remote_peer_key_name(""),
  session_key(constants::netsync_key_initializer),
  read_hmac(constants::netsync_key_initializer),
  write_hmac(constants::netsync_key_initializer),
  authenticated(false),
  last_io_time(::time(NULL)),
  byte_in_ticker(NULL),
  byte_out_ticker(NULL),
  cert_in_ticker(NULL),
  cert_out_ticker(NULL),
  revision_in_ticker(NULL),
  revision_out_ticker(NULL),
  revision_checked_ticker(NULL),
  analyzed_ancestry(false),
  saved_nonce(""),
  received_goodbye(false),
  sent_goodbye(false),
  dbw(app, true),
  encountered_error(false)
{
  dbw.set_on_revision_written(boost::bind(&session::rev_written_callback,
                                          this, _1));
  dbw.set_on_cert_written(boost::bind(&session::cert_written_callback,
                                          this, _1));
  dbw.set_on_pubkey_written(boost::bind(&session::key_written_callback,
                                          this, _1));

  done_refinements.insert(make_pair(cert_item, done_marker()));
  done_refinements.insert(make_pair(key_item, done_marker()));
  done_refinements.insert(make_pair(epoch_item, done_marker()));
  
  requested_items.insert(make_pair(cert_item, boost::shared_ptr< set<id> >(new set<id>())));
  requested_items.insert(make_pair(key_item, boost::shared_ptr< set<id> >(new set<id>())));
  requested_items.insert(make_pair(revision_item, boost::shared_ptr< set<id> >(new set<id>())));
  requested_items.insert(make_pair(manifest_item, boost::shared_ptr< set<id> >(new set<id>())));
  requested_items.insert(make_pair(file_item, boost::shared_ptr< set<id> >(new set<id>())));
  requested_items.insert(make_pair(epoch_item, boost::shared_ptr< set<id> >(new set<id>())));

  received_items.insert(make_pair(cert_item, boost::shared_ptr< set<id> >(new set<id>())));
  received_items.insert(make_pair(key_item, boost::shared_ptr< set<id> >(new set<id>())));
  received_items.insert(make_pair(revision_item, boost::shared_ptr< set<id> >(new set<id>())));
  received_items.insert(make_pair(manifest_item, boost::shared_ptr< set<id> >(new set<id>())));
  received_items.insert(make_pair(file_item, boost::shared_ptr< set<id> >(new set<id>())));
  received_items.insert(make_pair(epoch_item, boost::shared_ptr< set<id> >(new set<id>())));

  full_delta_items.insert(make_pair(manifest_item, boost::shared_ptr< set<id> >(new set<id>())));
  full_delta_items.insert(make_pair(file_item, boost::shared_ptr< set<id> >(new set<id>())));
}

session::~session()
{
  vector<cert> unattached_certs;
  map<revision_id, vector<cert> > revcerts;
  for (vector<revision_id>::iterator i = written_revisions.begin();
       i != written_revisions.end(); ++i)
    revcerts.insert(make_pair(*i, vector<cert>()));
  for (vector<cert>::iterator i = written_certs.begin();
       i != written_certs.end(); ++i)
    {
      map<revision_id, vector<cert> >::iterator j;
      j = revcerts.find(i->ident);
      if (j == revcerts.end())
        unattached_certs.push_back(*i);
      else
        j->second.push_back(*i);
    }

  //Keys
  for (vector<rsa_keypair_id>::iterator i = written_keys.begin();
       i != written_keys.end(); ++i)
    {
      app.lua.hook_note_netsync_pubkey_received(*i);
    }
  //Revisions
  for (vector<revision_id>::iterator i = written_revisions.begin();
      i != written_revisions.end(); ++i)
    {
      vector<cert> & ctmp(revcerts[*i]);
      set<pair<rsa_keypair_id, pair<cert_name, cert_value> > > certs;
      for (vector<cert>::const_iterator j = ctmp.begin();
           j != ctmp.end(); ++j)
        {
          cert_value vtmp;
          decode_base64(j->value, vtmp);
          certs.insert(make_pair(j->key, make_pair(j->name, vtmp)));
        }
      revision_data rdat;
      app.db.get_revision(*i, rdat);
      app.lua.hook_note_netsync_revision_received(*i, rdat, certs);
    }
  //Certs (not attached to a new revision)
  for (vector<cert>::iterator i = unattached_certs.begin();
      i != unattached_certs.end(); ++i)
    {
      cert_value tmp;
      decode_base64(i->value, tmp);
      app.lua.hook_note_netsync_cert_received(i->ident, i->key,
                                              i->name, tmp);

    }
}

void session::rev_written_callback(revision_id rid)
{
  if (revision_checked_ticker.get())
    ++(*revision_checked_ticker);
  written_revisions.push_back(rid);
}

void session::key_written_callback(rsa_keypair_id kid)
{
  written_keys.push_back(kid);
}

void session::cert_written_callback(cert const & c)
{
  written_certs.push_back(c);
}

id 
session::mk_nonce()
{
  I(this->saved_nonce().size() == 0);
  char buf[constants::merkle_hash_length_in_bytes];
  Botan::Global_RNG::randomize(reinterpret_cast<Botan::byte *>(buf),
          constants::merkle_hash_length_in_bytes);
  this->saved_nonce = string(buf, buf + constants::merkle_hash_length_in_bytes);
  I(this->saved_nonce().size() == constants::merkle_hash_length_in_bytes);
  return this->saved_nonce;
}

void 
session::mark_recent_io()
{
  last_io_time = ::time(NULL);
}

void
session::set_session_key(string const & key)
{
  session_key = netsync_session_key(key);
  read_hmac.set_key(session_key);
  write_hmac.set_key(session_key);
}

void
session::set_session_key(rsa_oaep_sha_data const & hmac_key_encrypted)
{
  keypair our_kp;
  load_key_pair(app, app.signing_key, our_kp);
  string hmac_key;
  decrypt_rsa(app.lua, app.signing_key, our_kp.priv,
              hmac_key_encrypted, hmac_key);
  set_session_key(hmac_key);
}

void
session::setup_client_tickers()
{
  // xgettext: please use short message and try to avoid multibytes chars
  byte_in_ticker.reset(new ticker(_("bytes in"), ">", 1024, true));
  // xgettext: please use short message and try to avoid multibytes chars
  byte_out_ticker.reset(new ticker(_("bytes out"), "<", 1024, true));
  if (role == sink_role)
    {
      // xgettext: please use short message and try to avoid multibytes chars
      revision_checked_ticker.reset(new ticker(_("revs written"), "w", 1));
      // xgettext: please use short message and try to avoid multibytes chars
      cert_in_ticker.reset(new ticker(_("certs in"), "c", 3));
      // xgettext: please use short message and try to avoid multibytes chars
      revision_in_ticker.reset(new ticker(_("revs in"), "r", 1));
    }
  else if (role == source_role)
    {
      // xgettext: please use short message and try to avoid multibytes chars
      cert_out_ticker.reset(new ticker(_("certs out"), "C", 3));
      // xgettext: please use short message and try to avoid multibytes chars
      revision_out_ticker.reset(new ticker(_("revs out"), "R", 1));
    }
  else
    {
      I(role == source_and_sink_role);
      // xgettext: please use short message and try to avoid multibytes chars
      revision_checked_ticker.reset(new ticker(_("revs written"), "w", 1));
      // xgettext: please use short message and try to avoid multibytes chars
      revision_in_ticker.reset(new ticker(_("revs in"), "r", 1));
      // xgettext: please use short message and try to avoid multibytes chars
      revision_out_ticker.reset(new ticker(_("revs out"), "R", 1));
    }
}

bool 
session::done_all_refinements()
{
  bool all = true;
  for (map<netcmd_item_type, done_marker>::const_iterator j =
         done_refinements.begin(); j != done_refinements.end(); ++j)
    {
      if (j->second.tree_is_done == false)
        all = false;
    }
  return all;
}


bool 
session::cert_refinement_done()
{
  return done_refinements[cert_item].tree_is_done;
}

bool 
session::got_all_data()
{
  for (map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator i =
         requested_items.begin(); i != requested_items.end(); ++i)
    {
      if (! i->second->empty())
        return false;
    }
  return true;
}

bool 
session::all_requested_revisions_received()
{
  map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator 
    i = requested_items.find(revision_item);
  I(i != requested_items.end());
  return i->second->empty();
}

void
session::maybe_note_epochs_finished()
{
  map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator 
    i = requested_items.find(epoch_item);
  I(i != requested_items.end());
  // Maybe there are outstanding epoch requests.
  if (!i->second->empty())
    return;
  // And maybe we haven't even finished the refinement.
  if (!done_refinements[epoch_item].tree_is_done)
    return;
  // But otherwise, we're ready to go!
  L(F("all epochs processed, opening database valve\n"));
  this->dbw.open_valve();
}

void
session::note_item_requested(netcmd_item_type ty, id const & ident)
{
  map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator 
    i = requested_items.find(ty);
  I(i != requested_items.end());
  i->second->insert(ident);
}

void
session::note_item_full_delta(netcmd_item_type ty, id const & ident)
{
  map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator 
    i = full_delta_items.find(ty);
  I(i != full_delta_items.end());
  i->second->insert(ident);
}

void
session::note_item_arrived(netcmd_item_type ty, id const & ident)
{
  map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator 
    i = requested_items.find(ty);
  I(i != requested_items.end());
  i->second->erase(ident);
  map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator 
    j = received_items.find(ty);
  I(j != received_items.end());
  j->second->insert(ident);
  

  switch (ty)
    {
    case cert_item:
      if (cert_in_ticker.get() != NULL)
        ++(*cert_in_ticker);
      break;
    case revision_item:
      if (revision_in_ticker.get() != NULL)
        ++(*revision_in_ticker);
      break;
    default:
      // No ticker for other things.
      break;
    }
}

bool 
session::item_already_requested(netcmd_item_type ty, id const & ident)
{
  map<netcmd_item_type, boost::shared_ptr< set<id> > >::const_iterator i;
  i = requested_items.find(ty);
  I(i != requested_items.end());
  if (i->second->find(ident) != i->second->end())
    return true;
  i = received_items.find(ty);
  I(i != received_items.end());
  if (i->second->find(ident) != i->second->end())
    return true;
  return false;
}


void
session::note_item_sent(netcmd_item_type ty, id const & ident)
{
  switch (ty)
    {
    case cert_item:
      if (cert_out_ticker.get() != NULL)
        ++(*cert_out_ticker);
      break;
    case revision_item:
      if (revision_out_ticker.get() != NULL)
        ++(*revision_out_ticker);
      break;
    default:
      // No ticker for other things.
      break;
    }
}

void 
session::write_netcmd_and_try_flush(netcmd const & cmd)
{
  if (!encountered_error)
  {
    string buf;
    cmd.write(buf, write_hmac);
    outbuf.push_back(make_pair(buf, 0));
    outbuf_size += buf.size();
  }
  else
    L(F("dropping outgoing netcmd (because we're in error unwind mode)\n"));
  // FIXME: this helps keep the protocol pipeline full but it seems to
  // interfere with initial and final sequences. careful with it.
  // write_some();
  // read_some();
}

// This method triggers a special "error unwind" mode to netsync.  In this
// mode, all received data is ignored, and no new data is queued.  We simply
// stay connected long enough for the current write buffer to be flushed, to
// ensure that our peer receives the error message.
// WARNING WARNING WARNING (FIXME): this does _not_ throw an exception.  if
// while processing any given netcmd packet you encounter an error, you can
// _only_ call this method if you have not touched the database, because if
// you have touched the database then you need to throw an exception to
// trigger a rollback.
// you could, of course, call this method and then throw an exception, but
// there is no point in doing that, because throwing the exception will cause
// the connection to be immediately terminated, so your call to error() will
// actually have no effect (except to cause your error message to be printed
// twice).
void
session::error(std::string const & errmsg)
{
  W(F("error: %s\n") % errmsg);
  queue_error_cmd(errmsg);
  encountered_error = true;
}

void 
session::analyze_manifest(manifest_map const & man)
{
  L(F("analyzing %d entries in manifest\n") % man.size());
  for (manifest_map::const_iterator i = man.begin();
       i != man.end(); ++i)
    {
      if (! this->app.db.file_version_exists(manifest_entry_id(i)))
        {
          id tmp;
          decode_hexenc(manifest_entry_id(i).inner(), tmp);
          queue_send_data_cmd(file_item, tmp);
        }
    }
}

inline static id
plain_id(manifest_id const & i)
{
  id tmp;
  hexenc<id> htmp(i.inner());
  decode_hexenc(htmp, tmp);
  return tmp;
}

inline static id
plain_id(file_id const & i)
{
  id tmp;
  hexenc<id> htmp(i.inner());
  decode_hexenc(htmp, tmp);
  return tmp;
}

void
session::get_heads_and_consume_certs( set<revision_id> & heads )
{
  typedef map<revision_id, boost::shared_ptr< pair<revision_data, revision_set> > > ancestryT;
  typedef map<cert_name, vector<cert> > cert_map;

  set<revision_id> nodes, parents;
  map<revision_id, int> chld_num;
  L(F("analyzing %d ancestry edges\n") % ancestry.size());

  for (ancestryT::const_iterator i = ancestry.begin(); i != ancestry.end(); ++i)
    {
      nodes.insert(i->first);
      for (edge_map::const_iterator j = i->second->second.edges.begin();
           j != i->second->second.edges.end(); ++j)
        {
          parents.insert(edge_old_revision(j));
          map<revision_id, int>::iterator n;
          n = chld_num.find(edge_old_revision(j));
          if (n == chld_num.end())
            chld_num.insert(make_pair(edge_old_revision(j), 1));
          else
            ++(n->second);
        }
    }
  
  set_difference(nodes.begin(), nodes.end(),
                 parents.begin(), parents.end(),
                 inserter(heads, heads.begin()));

  L(F("intermediate set_difference heads size %d") % heads.size());

  // Write permissions checking:
  // remove heads w/o proper certs, add their children to heads
  // 1) remove unwanted branch certs from consideration
  // 2) remove heads w/o a branch tag, process new exposed heads
  // 3) repeat 2 until no change

  //1
  set<string> ok_branches, bad_branches;
  cert_name bcert_name(branch_cert_name);
  cert_name tcert_name(tag_cert_name);
  for (map<revision_id, cert_map>::iterator i = received_certs.begin();
      i != received_certs.end(); ++i)
    {
      //branches
      vector<cert> & bcerts(i->second[bcert_name]);
      vector<cert> keeping;
      for (vector<cert>::iterator j = bcerts.begin(); j != bcerts.end(); ++j)
        {
          cert_value name;
          decode_base64(j->value, name);
          if (ok_branches.find(name()) != ok_branches.end())
            keeping.push_back(*j);
          else if (bad_branches.find(name()) != bad_branches.end())
            ;
          else
            {
              if (our_matcher(name()))
                {
                  ok_branches.insert(name());
                  keeping.push_back(*j);
                }
              else
                {
                  bad_branches.insert(name());
                  W(F("Dropping branch certs for unwanted branch %s")
                    % name);
                }
            }
        }
      bcerts = keeping;
    }
  //2
  list<revision_id> tmp;
  for (set<revision_id>::iterator i = heads.begin(); i != heads.end(); ++i)
    {
      if (!received_certs[*i][bcert_name].size())
        tmp.push_back(*i);
    }
  for (list<revision_id>::iterator i = tmp.begin(); i != tmp.end(); ++i)
    heads.erase(*i);

  L(F("after step 2, heads size %d") % heads.size());
  //3
  while (tmp.size())
    {
      ancestryT::const_iterator i = ancestry.find(tmp.front());
      if (i != ancestry.end())
        {
          for (edge_map::const_iterator j = i->second->second.edges.begin();
               j != i->second->second.edges.end(); ++j)
            {
              if (!--chld_num[edge_old_revision(j)])
                {
                  if (received_certs[i->first][bcert_name].size())
                    heads.insert(i->first);
                  else
                    tmp.push_back(edge_old_revision(j));
                }
            }
          // since we don't want this rev, we don't want it's certs either
          received_certs[tmp.front()] = cert_map();
        }
        tmp.pop_front();
    }

  L(F("after step 3, heads size %d") % heads.size());
  // We've reduced the certs to those we want now, send them to dbw.
  for (map<revision_id, cert_map>::iterator i = received_certs.begin();
      i != received_certs.end(); ++i)
    {
      for (cert_map::iterator j = i->second.begin();
          j != i->second.end(); ++j)
        {
          for (vector<cert>::iterator k = j->second.begin();
              k != j->second.end(); ++k)
            {
              this->dbw.consume_revision_cert(revision<cert>(*k));
            }
        }
    }
}

void
session::analyze_ancestry_graph()
{
  L(F("analyze_ancestry_graph"));
  if (! (all_requested_revisions_received() && cert_refinement_done()))
    {
      L(F("not all done in analyze_ancestry_graph"));
      return;
    }

  if (analyzed_ancestry)
    {
      L(F("already analyzed_ancestry in analyze_ancestry_graph"));
      return;
    }

  L(F("analyze_ancestry_graph fetching"));

  ancestry_fetcher fetch(*this);

  analyzed_ancestry = true;
}

Netxx::Probe::ready_type 
session::which_events() const
{
  // Only ask to read if we're not armed.
  if (outbuf.empty())
    {
      if (inbuf.size() < constants::netcmd_maxsz && !armed)
        return Netxx::Probe::ready_read | Netxx::Probe::ready_oobd;
      else
        return Netxx::Probe::ready_oobd;
    }
  else
    {
      if (inbuf.size() < constants::netcmd_maxsz && !armed)
        return Netxx::Probe::ready_write | Netxx::Probe::ready_read | Netxx::Probe::ready_oobd;
      else
        return Netxx::Probe::ready_write | Netxx::Probe::ready_oobd;
    }       
}

bool 
session::read_some()
{
  I(inbuf.size() < constants::netcmd_maxsz);
  char tmp[constants::bufsz];
  Netxx::signed_size_type count = str.read(tmp, sizeof(tmp));
  if (count > 0)
    {
      L(F("read %d bytes from fd %d (peer %s)\n") % count % fd % peer_id);
      if (encountered_error)
        {
          L(F("in error unwind mode, so throwing them into the bit bucket\n"));
          return true;
        }
      inbuf.append(tmp,count);
      mark_recent_io();
      if (byte_in_ticker.get() != NULL)
        (*byte_in_ticker) += count;
      return true;
    }
  else
    return false;
}

bool 
session::write_some()
{
  I(!outbuf.empty());    
  size_t writelen = outbuf.front().first.size() - outbuf.front().second;
  Netxx::signed_size_type count = str.write(outbuf.front().first.data() + outbuf.front().second, 
                                            std::min(writelen,
                                            constants::bufsz));
  if (count > 0)
    {
      if ((size_t)count == writelen)
        {
          outbuf_size -= outbuf.front().first.size();
          outbuf.pop_front();
        }
      else
        {
          outbuf.front().second += count;
        }
      L(F("wrote %d bytes to fd %d (peer %s)\n")
        % count % fd % peer_id);
      mark_recent_io();
      if (byte_out_ticker.get() != NULL)
        (*byte_out_ticker) += count;
      if (encountered_error && outbuf.empty())
        {
          // we've flushed our error message, so it's time to get out.
          L(F("finished flushing output queue in error unwind mode, disconnecting\n"));
          return false;
        }
      return true;
    }
  else
    return false;
}

// senders

void 
session::queue_bye_cmd() 
{
  L(F("queueing 'bye' command\n"));
  netcmd cmd;
  cmd.write_bye_cmd();
  write_netcmd_and_try_flush(cmd);
  this->sent_goodbye = true;
}

void 
session::queue_error_cmd(string const & errmsg)
{
  L(F("queueing 'error' command\n"));
  netcmd cmd;
  cmd.write_error_cmd(errmsg);
  write_netcmd_and_try_flush(cmd);
  this->sent_goodbye = true;
}

void 
session::queue_done_cmd(size_t level, 
                        netcmd_item_type type) 
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  L(F("queueing 'done' command for %s level %s\n") % typestr % level);
  netcmd cmd;
  cmd.write_done_cmd(level, type);
  write_netcmd_and_try_flush(cmd);
}

void 
session::queue_hello_cmd(rsa_keypair_id const & key_name,
                         base64<rsa_pub_key> const & pub_encoded, 
                         id const & nonce) 
{
  rsa_pub_key pub;
  decode_base64(pub_encoded, pub);
  cmd.write_hello_cmd(key_name, pub, nonce);
  write_netcmd_and_try_flush(cmd);
}

void 
session::queue_anonymous_cmd(protocol_role role, 
                             utf8 const & include_pattern, 
                             utf8 const & exclude_pattern, 
                             id const & nonce2,
                             base64<rsa_pub_key> server_key_encoded)
{
  netcmd cmd;
  rsa_oaep_sha_data hmac_key_encrypted;
  encrypt_rsa(app.lua, remote_peer_key_name, server_key_encoded,
              nonce2(), hmac_key_encrypted);
  cmd.write_anonymous_cmd(role, include_pattern, exclude_pattern,
                          hmac_key_encrypted);
  write_netcmd_and_try_flush(cmd);
  set_session_key(nonce2());
}

void 
session::queue_auth_cmd(protocol_role role, 
                        utf8 const & include_pattern, 
                        utf8 const & exclude_pattern, 
                        id const & client, 
                        id const & nonce1, 
                        id const & nonce2, 
                        string const & signature,
                        base64<rsa_pub_key> server_key_encoded)
{
  netcmd cmd;
  rsa_oaep_sha_data hmac_key_encrypted;
  encrypt_rsa(app.lua, remote_peer_key_name, server_key_encoded,
              nonce2(), hmac_key_encrypted);
  cmd.write_auth_cmd(role, include_pattern, exclude_pattern, client,
                     nonce1, hmac_key_encrypted, signature);
  write_netcmd_and_try_flush(cmd);
  set_session_key(nonce2());
}

void
session::queue_confirm_cmd()
{
  netcmd cmd;
  cmd.write_confirm_cmd();
  write_netcmd_and_try_flush(cmd);
}

void 
session::queue_refine_cmd(merkle_node const & node)
{
  string typestr;
  hexenc<prefix> hpref;
  node.get_hex_prefix(hpref);
  netcmd_item_type_to_string(node.type, typestr);
  L(F("queueing request for refinement of %s node '%s', level %d\n")
    % typestr % hpref % static_cast<int>(node.level));
  netcmd cmd;
  cmd.write_refine_cmd(node);
  write_netcmd_and_try_flush(cmd);
}

void 
session::queue_send_data_cmd(netcmd_item_type type,
                             id const & item)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hid;
  encode_hexenc(item, hid);

  if (role == source_role)
    {
      L(F("not queueing request for %s '%s' as we are in pure source role\n") 
        % typestr % hid);
      return;
    }

  if (item_already_requested(type, item))
    {
      L(F("not queueing request for %s '%s' as we already requested it\n") 
        % typestr % hid);
      return;
    }

  L(F("queueing request for data of %s item '%s'\n")
    % typestr % hid);
  netcmd cmd;
  cmd.write_send_data_cmd(type, item);
  write_netcmd_and_try_flush(cmd);
  note_item_requested(type, item);
}
    
void 
session::queue_send_delta_cmd(netcmd_item_type type,
                              id const & base, 
                              id const & ident)
{
  I(type == manifest_item || type == file_item);

  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> base_hid;
  encode_hexenc(base, base_hid);
  hexenc<id> ident_hid;
  encode_hexenc(ident, ident_hid);
  
  if (role == source_role)
    {
      L(F("not queueing request for %s delta '%s' -> '%s' as we are in pure source role\n") 
        % typestr % base_hid % ident_hid);
      return;
    }

  if (item_already_requested(type, ident))
    {
      L(F("not queueing request for %s delta '%s' -> '%s' as we already requested the target\n") 
        % typestr % base_hid % ident_hid);
      return;
    }

  L(F("queueing request for contents of %s delta '%s' -> '%s'\n")
    % typestr % base_hid % ident_hid);
  netcmd cmd;
  cmd.write_send_delta_cmd(type, base, ident);
  write_netcmd_and_try_flush(cmd);
  note_item_requested(type, ident);
}

void 
session::queue_data_cmd(netcmd_item_type type,
                        id const & item, 
                        string const & dat)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hid;
  encode_hexenc(item, hid);

  if (role == sink_role)
    {
      L(F("not queueing %s data for '%s' as we are in pure sink role\n") 
        % typestr % hid);
      return;
    }

  L(F("queueing %d bytes of data for %s item '%s'\n")
    % dat.size() % typestr % hid);

  netcmd cmd;
  // TODO: This pair of functions will make two copies of a large
  // file, the first in cmd.write_data_cmd, and the second in
  // write_netcmd_and_try_flush when the data is copied from the
  // cmd.payload variable to the string buffer for output.  This 
  // double copy should be collapsed out, it may be better to use
  // a string_queue for output as well as input, as that will reduce
  // the amount of mallocs that happen when the string queue is large
  // enough to just store the data.
  cmd.write_data_cmd(type, item, dat);
  write_netcmd_and_try_flush(cmd);
  note_item_sent(type, item);
}

void
session::queue_delta_cmd(netcmd_item_type type,
                         id const & base, 
                         id const & ident, 
                         delta const & del)
{
  I(type == manifest_item || type == file_item);
  I(! del().empty() || ident == base);
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> base_hid;
  encode_hexenc(base, base_hid);
  hexenc<id> ident_hid;
  encode_hexenc(ident, ident_hid);

  if (role == sink_role)
    {
      L(F("not queueing %s delta '%s' -> '%s' as we are in pure sink role\n") 
        % typestr % base_hid % ident_hid);
      return;
    }

  L(F("queueing %s delta '%s' -> '%s'\n")
    % typestr % base_hid % ident_hid);
  netcmd cmd;
  cmd.write_delta_cmd(type, base, ident, del);
  write_netcmd_and_try_flush(cmd);
  note_item_sent(type, ident);
}

void 
session::queue_nonexistant_cmd(netcmd_item_type type,
                               id const & item)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hid;
  encode_hexenc(item, hid);
  if (role == sink_role)
    {
      L(F("not queueing note of nonexistence of %s item '%s' as we are in pure sink role\n") 
        % typestr % hid);
      return;
    }

  L(F("queueing note of nonexistance of %s item '%s'\n")
    % typestr % hid);
  netcmd cmd;
  cmd.write_nonexistant_cmd(type, item);
  write_netcmd_and_try_flush(cmd);
}

// processors

bool 
session::process_bye_cmd() 
{
  L(F("received 'bye' netcmd\n"));
  this->received_goodbye = true;
  return true;
}

bool 
session::process_error_cmd(string const & errmsg) 
{
  throw bad_decode(F("received network error: %s") % errmsg);
}

bool 
session::process_done_cmd(size_t level, netcmd_item_type type) 
{

  map< netcmd_item_type, done_marker>::iterator i = done_refinements.find(type);
  I(i != done_refinements.end());

  string typestr;
  netcmd_item_type_to_string(type, typestr);

  if ((! i->second.current_level_had_refinements) || (level >= 0xff))
    {
      // we received *no* refinements on this level -- or we ran out of
      // levels -- so refinement for this type is finished.
      L(F("received 'done' for empty %s level %d, marking as complete\n") 
        % typestr % static_cast<int>(level));

      // possibly echo it back one last time, for shutdown purposes
      if (!i->second.tree_is_done)
        queue_done_cmd(level + 1, type);

      // tombstone it
      i->second.current_level_had_refinements = false;
      i->second.tree_is_done = true;

      if (all_requested_revisions_received())
        analyze_ancestry_graph();      

      maybe_note_epochs_finished();
    }

  else if (i->second.current_level_had_refinements 
      && (! i->second.tree_is_done))
    {
      // we *did* receive some refinements on this level, reset to zero and
      // queue an echo of the 'done' marker.
      L(F("received 'done' for %s level %d, which had refinements; "
          "sending echo of done for level %d\n") 
        % typestr 
        % static_cast<int>(level) 
        % static_cast<int>(level + 1));
      i->second.current_level_had_refinements = false;
      queue_done_cmd(level + 1, type);
      return true;
    }
  return true;
}

void
get_branches(app_state & app, vector<string> & names)
{
  app.db.get_branches(names);
  sort(names.begin(), names.end());
}

static const var_domain known_servers_domain = var_domain("known-servers");

bool 
session::process_hello_cmd(rsa_keypair_id const & their_keyname,
                           rsa_pub_key const & their_key,
                           id const & nonce) 
{
  I(this->remote_peer_key_hash().size() == 0);
  I(this->saved_nonce().size() == 0);
  
  hexenc<id> their_key_hash;
  base64<rsa_pub_key> their_key_encoded;
  encode_base64(their_key, their_key_encoded);
  key_hash_code(their_keyname, their_key_encoded, their_key_hash);
  L(F("server key has name %s, hash %s\n") % their_keyname % their_key_hash);
  var_key their_key_key(known_servers_domain, var_name(peer_id));
  if (app.db.var_exists(their_key_key))
    {
      var_value expected_key_hash;
      app.db.get_var(their_key_key, expected_key_hash);
      if (expected_key_hash() != their_key_hash())
        {
          P(F("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
	      "@ WARNING: SERVER IDENTIFICATION HAS CHANGED              @\n"
	      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
	      "IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY\n"
	      "it is also possible that the server key has just been changed\n"
	      "remote host sent key %s\n"
	      "I expected %s\n"
	      "'monotone unset %s %s' overrides this check\n")
	    % their_key_hash % expected_key_hash
            % their_key_key.first % their_key_key.second);
          E(false, F("server key changed"));
        }
    }
  else
    {
      P(F("first time connecting to server %s\n"
	  "I'll assume it's really them, but you might want to double-check\n"
	  "their key's fingerprint: %s\n") % peer_id % their_key_hash);
      app.db.set_var(their_key_key, var_value(their_key_hash()));
    }
  if (!app.db.public_key_exists(their_key_hash))
    {
      W(F("saving public key for %s to database\n") % their_keyname);
      app.db.put_key(their_keyname, their_key_encoded);
    }
  
  {
    hexenc<id> hnonce;
    encode_hexenc(nonce, hnonce);
    L(F("received 'hello' netcmd from server '%s' with nonce '%s'\n") 
      % their_key_hash % hnonce);
  }
  
  I(app.db.public_key_exists(their_key_hash));
  
  // save their identity 
  {
    id their_key_hash_decoded;
    decode_hexenc(their_key_hash, their_key_hash_decoded);
    this->remote_peer_key_hash = their_key_hash_decoded;
  }

  // clients always include in the synchronization set, every branch that the
  // user requested
  vector<string> branchnames;
  set<utf8> ok_branches;
  get_branches(app, branchnames);
  for (vector<string>::const_iterator i = branchnames.begin();
      i != branchnames.end(); i++)
    {
      if (our_matcher(*i))
        ok_branches.insert(utf8(*i));
    }
  rebuild_merkle_trees(app, ok_branches);

  setup_client_tickers();
    
  if (app.signing_key() != "")
    {
      // get our key pair
      keypair our_kp;
      load_key_pair(app, app.signing_key, our_kp);

      // get the hash identifier for our pubkey
      hexenc<id> our_key_hash;
      id our_key_hash_raw;
      key_hash_code(app.signing_key, our_kp.pub, our_key_hash);
      decode_hexenc(our_key_hash, our_key_hash_raw);
      
      // make a signature
      base64<rsa_sha1_signature> sig;
      rsa_sha1_signature sig_raw;
      make_signature(app, app.signing_key, our_kp.priv, nonce(), sig);
      decode_base64(sig, sig_raw);
      
      // make a new nonce of our own and send off the 'auth'
      queue_auth_cmd(this->role, our_include_pattern, our_exclude_pattern,
                     our_key_hash_raw, nonce, mk_nonce(), sig_raw(),
                     their_key_encoded);
    }
  else
    {
      queue_anonymous_cmd(this->role, our_include_pattern,
                          our_exclude_pattern, mk_nonce(), their_key_encoded);
    }
  return true;
}

bool 
session::process_anonymous_cmd(protocol_role role, 
                               utf8 const & their_include_pattern,
                               utf8 const & their_exclude_pattern)
{
  //
  // internally netsync thinks in terms of sources and sinks. users like
  // thinking of repositories as "readonly", "readwrite", or "writeonly".
  //
  // we therefore use the read/write terminology when dealing with the UI:
  // if the user asks to run a "read only" service, this means they are
  // willing to be a source but not a sink.
  //
  // nb: the "role" here is the role the *client* wants to play
  //     so we need to check that the opposite role is allowed for us,
  //     in our this->role field.
  //

  // client must be a sink and server must be a source (anonymous read-only)

  if (role != sink_role)
    {
      W(F("rejected attempt at anonymous connection for write\n"));
      this->saved_nonce = id("");
      return false;
    }

  if (this->role != source_role && this->role != source_and_sink_role)
    {
      W(F("rejected attempt at anonymous connection while running as sink\n"));
      this->saved_nonce = id("");
      return false;
    }

  vector<string> branchnames;
  set<utf8> ok_branches;
  get_branches(app, branchnames);
  globish_matcher their_matcher(their_include_pattern, their_exclude_pattern);
  for (vector<string>::const_iterator i = branchnames.begin();
      i != branchnames.end(); i++)
    {
      if (their_matcher(*i))
        if (our_matcher(*i) && app.lua.hook_get_netsync_read_permitted(*i))
          ok_branches.insert(utf8(*i));
        else
          {
            error((F("anonymous access to branch '%s' denied by server") % *i).str());
            return true;
          }
    }

  P(F("allowed anonymous read permission for '%s' excluding '%s'\n")
    % their_include_pattern % their_exclude_pattern);

  rebuild_merkle_trees(app, ok_branches);

  this->remote_peer_key_name = rsa_keypair_id("");
  this->authenticated = true;
  this->role = source_role;
  return true;
}

bool
session::process_auth_cmd(protocol_role their_role,
                          utf8 const & their_include_pattern,
                          utf8 const & their_exclude_pattern,
                          id const & client,
                          id const & nonce1,
                          string const & signature)
{
  I(this->remote_peer_key_hash().size() == 0);
  I(this->saved_nonce().size() == constants::merkle_hash_length_in_bytes);
  
  hexenc<id> their_key_hash;
  encode_hexenc(client, their_key_hash);
  set<utf8> ok_branches;
  vector<string> branchnames;
  get_branches(app, branchnames);
  globish_matcher their_matcher(their_include_pattern, their_exclude_pattern);
  
  // check that they replied with the nonce we asked for
  if (!(nonce1 == this->saved_nonce))
    {
      W(F("detected replay attack in auth netcmd\n"));
      this->saved_nonce = id("");
      return false;
    }


  //
  // internally netsync thinks in terms of sources and sinks. users like
  // thinking of repositories as "readonly", "readwrite", or "writeonly".
  //
  // we therefore use the read/write terminology when dealing with the UI:
  // if the user asks to run a "read only" service, this means they are
  // willing to be a source but not a sink.
  //
  // nb: the "their_role" here is the role the *client* wants to play
  //     so we need to check that the opposite role is allowed for us,
  //     in our this->role field.
  //

  if (!app.db.public_key_exists(their_key_hash))
    {
      // if it's not in the db, it still could be in the keystore if we
      // have the private key that goes with it
      if (!app.keys.try_ensure_in_db(their_key_hash))
        {
          W(F("remote public key hash '%s' is unknown\n") % their_key_hash);
          this->saved_nonce = id("");
          return false;
        }
    }
  
  // get their public key
  rsa_keypair_id their_id;
  base64<rsa_pub_key> their_key;
  app.db.get_pubkey(their_key_hash, their_id, their_key);

  // client as sink, server as source (reading)

  if (their_role == sink_role || their_role == source_and_sink_role)
    {
      if (this->role != source_role && this->role != source_and_sink_role)
        {
          W(F("denied '%s' read permission for '%s' excluding '%s' while running as pure sink\n") 
            % their_id % their_include_pattern % their_exclude_pattern);
          this->saved_nonce = id("");
          return false;
        }
    }

  for (vector<string>::const_iterator i = branchnames.begin();
       i != branchnames.end(); i++)
    {
      if (their_matcher(*i))
        {
          if (our_matcher(*i) && app.lua.hook_get_netsync_read_permitted(*i, their_id))
            ok_branches.insert(utf8(*i));
          else
            {
              W(F("denied '%s' read permission for '%s' excluding '%s' because of branch '%s'\n") 
                % their_id % their_include_pattern % their_exclude_pattern % *i);
              error((F("access to branch '%s' denied by server") % *i).str());
              return true;
            }
        }
    }

  //if we're source_and_sink_role, continue even with no branches readable
  //ex: serve --db=empty.db
  P(F("allowed '%s' read permission for '%s' excluding '%s'\n")
    % their_id % their_include_pattern % their_exclude_pattern);

  // client as source, server as sink (writing)

  if (their_role == source_role || their_role == source_and_sink_role)
    {
      if (this->role != sink_role && this->role != source_and_sink_role)
        {
          W(F("denied '%s' write permission for '%s' excluding '%s' while running as pure source\n")
            % their_id % their_include_pattern % their_exclude_pattern);
          this->saved_nonce = id("");
          return false;
        }

      if (!app.lua.hook_get_netsync_write_permitted(their_id))
        {
          W(F("denied '%s' write permission for '%s' excluding '%s'\n")
            % their_id % their_include_pattern % their_exclude_pattern);
          this->saved_nonce = id("");
          return false;
        }

      P(F("allowed '%s' write permission for '%s' excluding '%s'\n")
        % their_id % their_include_pattern % their_exclude_pattern);
    }

  rebuild_merkle_trees(app, ok_branches);

  // save their identity 
  this->remote_peer_key_hash = client;

  // check the signature
  base64<rsa_sha1_signature> sig;
  encode_base64(rsa_sha1_signature(signature), sig);
  if (check_signature(app, their_id, their_key, nonce1(), sig))
    {
      // get our private key and sign back
      L(F("client signature OK, accepting authentication\n"));
      this->authenticated = true;
      this->remote_peer_key_name = their_id;
      // assume the (possibly degraded) opposite role
      switch (their_role)
        {
        case source_role:
          I(this->role != source_role);
          this->role = sink_role;
          break;
        case source_and_sink_role:
          I(this->role == source_and_sink_role);
          break;
        case sink_role:
          I(this->role != sink_role);
          this->role = source_role;
          break;          
        }
      return true;
    }
  else
    {
      W(F("bad client signature\n"));         
    }  
  return false;
}

bool 
session::process_confirm_cmd(string const & signature)
{
  I(this->remote_peer_key_hash().size() == constants::merkle_hash_length_in_bytes);
  I(this->saved_nonce().size() == constants::merkle_hash_length_in_bytes);
  
  hexenc<id> their_key_hash;
  encode_hexenc(id(remote_peer_key_hash), their_key_hash);
  
  // nb. this->role is our role, the server is in the opposite role
  L(F("received 'confirm' netcmd from server '%s' for pattern '%s' exclude '%s' in %s mode\n")
    % their_key_hash % our_include_pattern % our_exclude_pattern
    % (this->role == source_and_sink_role ? _("source and sink") :
       (this->role == source_role ? _("sink") : _("source"))));
  
  // check their signature
  if (app.db.public_key_exists(their_key_hash))
    {
      // get their public key and check the signature
      rsa_keypair_id their_id;
      base64<rsa_pub_key> their_key;
      app.db.get_pubkey(their_key_hash, their_id, their_key);
      base64<rsa_sha1_signature> sig;
      encode_base64(rsa_sha1_signature(signature), sig);
      if (check_signature(app, their_id, their_key, this->saved_nonce(), sig))
        {
          L(F("server signature OK, accepting authentication\n"));
          return true;
        }
      else
        {
          W(F("bad server signature\n"));             
        }
    }
  else
    {
      W(F("unknown server key\n"));
    }
  return false;
}

void
session::respond_to_confirm_cmd()
{
  merkle_ptr root;
  load_merkle_node(epoch_item, 0, get_root_prefix().val, root);
  queue_refine_cmd(*root);
  queue_done_cmd(0, epoch_item);

  load_merkle_node(key_item, 0, get_root_prefix().val, root);
  queue_refine_cmd(*root);
  queue_done_cmd(0, key_item);

  load_merkle_node(cert_item, 0, get_root_prefix().val, root);
  queue_refine_cmd(*root);
  queue_done_cmd(0, cert_item);
}

static bool 
data_exists(netcmd_item_type type, 
            id const & item, 
            app_state & app)
{
  hexenc<id> hitem;
  encode_hexenc(item, hitem);
  switch (type)
    {
    case key_item:
      return app.db.public_key_exists(hitem);
    case manifest_item:
      return app.db.manifest_version_exists(manifest_id(hitem));
    case file_item:
      return app.db.file_version_exists(file_id(hitem));
    case revision_item:
      return app.db.revision_exists(revision_id(hitem));
    case cert_item:
      return app.db.revision_cert_exists(hitem);
    case epoch_item:
      return app.db.epoch_exists(epoch_id(hitem));
    }
  return false;
}

static void 
load_data(netcmd_item_type type, 
          id const & item, 
          app_state & app, 
          string & out)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hitem;
  encode_hexenc(item, hitem);
  switch (type)
    {
    case epoch_item:
      if (app.db.epoch_exists(epoch_id(hitem)))
      {
        cert_value branch;
        epoch_data epoch;
        app.db.get_epoch(epoch_id(hitem), branch, epoch);
        write_epoch(branch, epoch, out);
      }
      else
        {
          throw bad_decode(F("epoch with hash '%s' does not exist in our database")
                           % hitem);
        }
      break;
    case key_item:
      if (app.db.public_key_exists(hitem))
        {
          rsa_keypair_id keyid;
          base64<rsa_pub_key> pub_encoded;
          app.db.get_pubkey(hitem, keyid, pub_encoded);
          L(F("public key '%s' is also called '%s'\n") % hitem % keyid);
          write_pubkey(keyid, pub_encoded, out);
        }
      else
        {
          throw bad_decode(F("no public key '%s' found in database") % hitem);
        }
      break;

    case revision_item:
      if (app.db.revision_exists(revision_id(hitem)))
        {
          revision_data mdat;
          data dat;
          app.db.get_revision(revision_id(hitem), mdat);
          out = mdat.inner()();
        }
      else
        {
          throw bad_decode(F("revision '%s' does not exist in our database") % hitem);
        }
      break;

    case manifest_item:
      if (app.db.manifest_version_exists(manifest_id(hitem)))
        {
          manifest_data mdat;
          data dat;
          app.db.get_manifest_version(manifest_id(hitem), mdat);
          out = mdat.inner()();
        }
      else
        {
          throw bad_decode(F("manifest '%s' does not exist in our database") % hitem);
        }
      break;

    case file_item:
      if (app.db.file_version_exists(file_id(hitem)))
        {
          file_data fdat;
          data dat;
          app.db.get_file_version(file_id(hitem), fdat);
          out = fdat.inner()();
        }
      else
        {
          throw bad_decode(F("file '%s' does not exist in our database") % hitem);
        }
      break;

    case cert_item:
      if (app.db.revision_cert_exists(hitem))
        {
          revision<cert> c;
          app.db.get_revision_cert(hitem, c);
          string tmp;
          write_cert(c.inner(), out);
        }
      else
        {
          throw bad_decode(F("cert '%s' does not exist in our database") % hitem);
        }
      break;
    }
}


bool 
session::process_refine_cmd(merkle_node const & their_node)
{
  prefix pref;
  hexenc<prefix> hpref;
  their_node.get_raw_prefix(pref);
  their_node.get_hex_prefix(hpref);
  string typestr;

  netcmd_item_type_to_string(their_node.type, typestr);
  size_t lev = static_cast<size_t>(their_node.level);
  
  L(F("received 'refine' netcmd on %s node '%s', level %d\n") 
    % typestr % hpref % lev);
  
  if (!merkle_node_exists(their_node.type, their_node.level, pref))
    {
      L(F("no corresponding %s merkle node for prefix '%s', level %d\n")
        % typestr % hpref % lev);

      for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
        {
          switch (their_node.get_slot_state(slot))
            {
            case empty_state:
              {
                // we agree, this slot is empty
                L(boost::format("(#0) they have an empty slot %d (in a %s node '%s', level %d, we do not have)\n")
                  % slot % typestr % hpref % lev);
                continue;
              }
              break;
            case live_leaf_state:
              {
                // we want what *they* have
                id slotval;
                hexenc<id> hslotval;
                their_node.get_raw_slot(slot, slotval);
                their_node.get_hex_slot(slot, hslotval);
                L(boost::format("(#0) they have a live leaf at slot %d (in a %s node '%s', level %d, we do not have)\n")
                  % slot % typestr % hpref % lev);
                L(boost::format("(#0) requesting their %s leaf %s\n") % typestr % hslotval);
                queue_send_data_cmd(their_node.type, slotval);
              }
              break;
            case dead_leaf_state:
              {
                // we cannot ask for what they have, it is dead
                L(boost::format("(#0) they have a dead leaf at slot %d (in a %s node '%s', level %d, we do not have)\n")
                  % slot % typestr % hpref % lev);
                continue;
              }
              break;
            case subtree_state:
              {
                // they have a subtree; might as well ask for that
                L(boost::format("(#0) they have a subtree at slot %d (in a %s node '%s', level %d, we do not have)\n")
                  % slot % typestr % hpref % lev);
                merkle_node our_fake_subtree;
                their_node.extended_prefix(slot, our_fake_subtree.pref);
                our_fake_subtree.level = their_node.level + 1;
                our_fake_subtree.type = their_node.type;
                queue_refine_cmd(our_fake_subtree);
              }
              break;
            }
        }
    }
  else
    {
      // we have a corresponding merkle node. there are 16 branches
      // to the following switch condition. it is awful. sorry.
      L(F("found corresponding %s merkle node for prefix '%s', level %d\n")
        % typestr % hpref % lev);
      merkle_ptr our_node;
      load_merkle_node(their_node.type, their_node.level, pref, our_node);
      for (size_t slot = 0; slot < constants::merkle_num_slots; ++slot)
        {         
          switch (their_node.get_slot_state(slot))
            {
            case empty_state:
              switch (our_node->get_slot_state(slot))
                {

                case empty_state:
                  // 1: theirs == empty, ours == empty 
                  L(boost::format("(#1) they have an empty slot %d in %s node '%s', level %d, and so do we\n")
                    % slot % typestr % hpref % lev);
                  continue;
                  break;

                case live_leaf_state:
                  // 2: theirs == empty, ours == live 
                  L(boost::format("(#2) they have an empty slot %d in %s node '%s', level %d, we have a live leaf\n")
                    % slot % typestr % hpref % lev);
                  {
                    I(their_node.type == our_node->type);
                    string tmp;
                    id slotval;
                    our_node->get_raw_slot(slot, slotval);
                    load_data(their_node.type, slotval, this->app, tmp);
                    queue_data_cmd(their_node.type, slotval, tmp);
                  }
                  break;

                case dead_leaf_state:
                  // 3: theirs == empty, ours == dead 
                  L(boost::format("(#3) they have an empty slot %d in %s node '%s', level %d, we have a dead leaf\n")
                    % slot % typestr % hpref % lev);
                  continue;
                  break;

                case subtree_state:
                  // 4: theirs == empty, ours == subtree 
                  L(boost::format("(#4) they have an empty slot %d in %s node '%s', level %d, we have a subtree\n")
                    % slot % typestr % hpref % lev);
                  {
                    prefix subprefix;
                    our_node->extended_raw_prefix(slot, subprefix);
                    merkle_ptr our_subtree;
                    I(our_node->type == their_node.type);
                    load_merkle_node(their_node.type, our_node->level + 1,
                                     subprefix, our_subtree);
                    I(our_node->type == our_subtree->type);
                    // FIXME: it would be more efficient here, to instead of
                    // sending our subtree, just send the data for everything
                    // in the subtree.
                    queue_refine_cmd(*our_subtree);
                  }
                  break;

                }
              break;


            case live_leaf_state:
              switch (our_node->get_slot_state(slot))
                {

                case empty_state:
                  // 5: theirs == live, ours == empty 
                  L(boost::format("(#5) they have a live leaf at slot %d in %s node '%s', level %d, we have nothing\n")
                    % slot % typestr % hpref % lev);
                  {
                    id slotval;
                    their_node.get_raw_slot(slot, slotval);
                    queue_send_data_cmd(their_node.type, slotval);
                  }
                  break;

                case live_leaf_state:
                  // 6: theirs == live, ours == live 
                  L(boost::format("(#6) they have a live leaf at slot %d in %s node '%s', and so do we\n")
                    % slot % typestr % hpref);
                  {
                    id our_slotval, their_slotval;
                    their_node.get_raw_slot(slot, their_slotval);
                    our_node->get_raw_slot(slot, our_slotval);               
                    if (their_slotval == our_slotval)
                      {
                        hexenc<id> hslotval;
                        their_node.get_hex_slot(slot, hslotval);
                        L(boost::format("(#6) we both have live %s leaf '%s'\n") % typestr % hslotval);
                        continue;
                      }
                    else
                      {
                        I(their_node.type == our_node->type);
                        string tmp;
                        load_data(our_node->type, our_slotval, this->app, tmp);
                        queue_send_data_cmd(their_node.type, their_slotval);
                        queue_data_cmd(our_node->type, our_slotval, tmp);
                      }
                  }
                  break;

                case dead_leaf_state:
                  // 7: theirs == live, ours == dead 
                  L(boost::format("(#7) they have a live leaf at slot %d in %s node %s, level %d, we have a dead one\n")
                    % slot % typestr % hpref % lev);
                  {
                    id our_slotval, their_slotval;
                    our_node->get_raw_slot(slot, our_slotval);
                    their_node.get_raw_slot(slot, their_slotval);
                    if (their_slotval == our_slotval)
                      {
                        hexenc<id> hslotval;
                        their_node.get_hex_slot(slot, hslotval);
                        L(boost::format("(#7) it's the same %s leaf '%s', but ours is dead\n") 
                          % typestr % hslotval);
                        continue;
                      }
                    else
                      {
                        queue_send_data_cmd(their_node.type, their_slotval);
                      }
                  }
                  break;

                case subtree_state:
                  // 8: theirs == live, ours == subtree 
                  L(boost::format("(#8) they have a live leaf in slot %d of %s node '%s', level %d, we have a subtree\n")
                    % slot % typestr % hpref % lev);
                  {

                    id their_slotval;
                    hexenc<id> their_hval;
                    their_node.get_raw_slot(slot, their_slotval);
                    encode_hexenc(their_slotval, their_hval);
                    if (data_exists(their_node.type, their_slotval, app))
                      L(boost::format("(#8) we have a copy of their live leaf '%s' in slot %d of %s node '%s', level %d\n")
                        % their_hval % slot % typestr % hpref % lev);
                    else
                      {
                        L(boost::format("(#8) requesting a copy of their live leaf '%s' in slot %d of %s node '%s', level %d\n")
                          % their_hval % slot % typestr % hpref % lev);
                        queue_send_data_cmd(their_node.type, their_slotval);
                      }
                    
                    L(boost::format("(#8) sending our subtree for refinement, in slot %d of %s node '%s', level %d\n")
                      % slot % typestr % hpref % lev);
                    prefix subprefix;
                    our_node->extended_raw_prefix(slot, subprefix);
                    merkle_ptr our_subtree;
                    load_merkle_node(our_node->type, our_node->level + 1,
                                     subprefix, our_subtree);
                    // FIXME: it would be more efficient here, to instead of
                    // sending our subtree, just send the data for everything
                    // in the subtree (except, possibly, the item they already
                    // have).
                    queue_refine_cmd(*our_subtree);
                  }
                  break;
                }
              break;


            case dead_leaf_state:
              switch (our_node->get_slot_state(slot))
                {
                case empty_state:
                  // 9: theirs == dead, ours == empty 
                  L(boost::format("(#9) they have a dead leaf at slot %d in %s node '%s', level %d, we have nothing\n")
                    % slot % typestr % hpref % lev);
                  continue;
                  break;

                case live_leaf_state:
                  // 10: theirs == dead, ours == live 
                  L(boost::format("(#10) they have a dead leaf at slot %d in %s node '%s', level %d, we have a live one\n")
                    % slot % typestr % hpref % lev);
                  {
                    id our_slotval, their_slotval;
                    their_node.get_raw_slot(slot, their_slotval);
                    our_node->get_raw_slot(slot, our_slotval);
                    hexenc<id> hslotval;
                    our_node->get_hex_slot(slot, hslotval);
                    if (their_slotval == our_slotval)
                      {
                        L(boost::format("(#10) we both have %s leaf %s, theirs is dead\n") 
                          % typestr % hslotval);
                        continue;
                      }
                    else
                      {
                        I(their_node.type == our_node->type);
                        string tmp;
                        load_data(our_node->type, our_slotval, this->app, tmp);
                        queue_data_cmd(our_node->type, our_slotval, tmp);
                      }
                  }
                  break;

                case dead_leaf_state:
                  // 11: theirs == dead, ours == dead 
                  L(boost::format("(#11) they have a dead leaf at slot %d in %s node '%s', level %d, so do we\n")
                    % slot % typestr % hpref % lev);
                  continue;
                  break;

                case subtree_state:
                  // theirs == dead, ours == subtree 
                  L(boost::format("(#12) they have a dead leaf in slot %d of %s node '%s', we have a subtree\n")
                    % slot % typestr % hpref % lev);
                  {
                    prefix subprefix;
                    our_node->extended_raw_prefix(slot, subprefix);
                    merkle_ptr our_subtree;
                    load_merkle_node(our_node->type, our_node->level + 1,
                                     subprefix, our_subtree);
                    // FIXME: it would be more efficient here, to instead of
                    // sending our subtree, just send the data for everything
                    // in the subtree (except, possibly, the dead thing).
                    queue_refine_cmd(*our_subtree);
                  }
                  break;
                }
              break;


            case subtree_state:
              switch (our_node->get_slot_state(slot))
                {
                case empty_state:
                  // 13: theirs == subtree, ours == empty 
                  L(boost::format("(#13) they have a subtree at slot %d in %s node '%s', level %d, we have nothing\n")
                    % slot % typestr % hpref % lev);
                  {
                    merkle_node our_fake_subtree;
                    their_node.extended_prefix(slot, our_fake_subtree.pref);
                    our_fake_subtree.level = their_node.level + 1;
                    our_fake_subtree.type = their_node.type;
                    queue_refine_cmd(our_fake_subtree);
                  }
                  break;

                case live_leaf_state:
                  // 14: theirs == subtree, ours == live 
                  L(boost::format("(#14) they have a subtree at slot %d in %s node '%s', level %d, we have a live leaf\n")
                    % slot % typestr % hpref % lev);
                  {
                    size_t subslot;
                    id our_slotval;
                    merkle_node our_fake_subtree;
                    our_node->get_raw_slot(slot, our_slotval);
                    hexenc<id> hslotval;
                    encode_hexenc(our_slotval, hslotval);
                    
                    pick_slot_and_prefix_for_value(our_slotval, our_node->level + 1, subslot, 
                                                   our_fake_subtree.pref);
                    L(boost::format("(#14) pushed our leaf '%s' into fake subtree slot %d, level %d\n")
                      % hslotval % subslot % (lev + 1));
                    our_fake_subtree.type = their_node.type;
                    our_fake_subtree.level = our_node->level + 1;
                    our_fake_subtree.set_raw_slot(subslot, our_slotval);
                    our_fake_subtree.set_slot_state(subslot, our_node->get_slot_state(slot));
                    queue_refine_cmd(our_fake_subtree);
                  }
                  break;

                case dead_leaf_state:
                  // 15: theirs == subtree, ours == dead 
                  L(boost::format("(#15) they have a subtree at slot %d in %s node '%s', level %d, we have a dead leaf\n")
                    % slot % typestr % hpref % lev);
                  {
                    size_t subslot;
                    id our_slotval;
                    merkle_node our_fake_subtree;
                    our_node->get_raw_slot(slot, our_slotval);
                    pick_slot_and_prefix_for_value(our_slotval, our_node->level + 1, subslot, 
                                                   our_fake_subtree.pref);
                    our_fake_subtree.type = their_node.type;
                    our_fake_subtree.level = our_node->level + 1;
                    our_fake_subtree.set_raw_slot(subslot, our_slotval);
                    our_fake_subtree.set_slot_state(subslot, our_node->get_slot_state(slot));
                    queue_refine_cmd(our_fake_subtree);    
                  }
                  break;

                case subtree_state:
                  // 16: theirs == subtree, ours == subtree 
                  L(boost::format("(#16) they have a subtree at slot %d in %s node '%s', level %d, and so do we\n")
                    % slot % typestr % hpref % lev);
                  {
                    id our_slotval, their_slotval;
                    hexenc<id> hslotval;
                    their_node.get_raw_slot(slot, their_slotval);
                    our_node->get_raw_slot(slot, our_slotval);
                    our_node->get_hex_slot(slot, hslotval);
                    if (their_slotval == our_slotval)
                      {
                        L(boost::format("(#16) we both have %s subtree '%s'\n") % typestr % hslotval);
                        continue;
                      }
                    else
                      {
                        L(boost::format("(#16) %s subtrees at slot %d differ, refining ours\n") % typestr % slot);
                        prefix subprefix;
                        our_node->extended_raw_prefix(slot, subprefix);
                        merkle_ptr our_subtree;
                        load_merkle_node(our_node->type, our_node->level + 1,
                                         subprefix, our_subtree);
                        queue_refine_cmd(*our_subtree);
                      }
                  }
                  break;
                }
              break;
            }
        }
    }
  return true;
}


bool 
session::process_send_data_cmd(netcmd_item_type type,
                               id const & item)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hitem;
  encode_hexenc(item, hitem);
  L(F("received 'send_data' netcmd requesting %s '%s'\n") 
    % typestr % hitem);
  if (data_exists(type, item, this->app))
    {
      string out;
      load_data(type, item, this->app, out);
      queue_data_cmd(type, item, out);
    }
  else
    {
      queue_nonexistant_cmd(type, item);
    }
  return true;
}

bool 
session::process_send_delta_cmd(netcmd_item_type type,
                                id const & base,
                                id const & ident)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  delta del;

  hexenc<id> hbase, hident;
  encode_hexenc(base, hbase);
  encode_hexenc(ident, hident);

  L(F("received 'send_delta' netcmd requesting %s edge '%s' -> '%s'\n") 
    % typestr % hbase % hident);

  switch (type)
    {
    case file_item:
      {
        file_id fbase(hbase), fident(hident);
        file_delta fdel;
        if (this->app.db.file_version_exists(fbase) 
            && this->app.db.file_version_exists(fident))
          {
            file_data base_fdat, ident_fdat;
            data base_dat, ident_dat;
            this->app.db.get_file_version(fbase, base_fdat);
            this->app.db.get_file_version(fident, ident_fdat);      
            string tmp;     
            base_dat = base_fdat.inner();
            ident_dat = ident_fdat.inner();
            compute_delta(base_dat(), ident_dat(), tmp);
            del = delta(tmp);
          }
        else
          {
            return process_send_data_cmd(type, ident);
          }
      }
      break;

    case manifest_item:
      {
        manifest_id mbase(hbase), mident(hident);
        manifest_delta mdel;
        if (this->app.db.manifest_version_exists(mbase) 
            && this->app.db.manifest_version_exists(mident))
          {
            manifest_data base_mdat, ident_mdat;
            data base_dat, ident_dat;
            this->app.db.get_manifest_version(mbase, base_mdat);
            this->app.db.get_manifest_version(mident, ident_mdat);
            string tmp;
            base_dat = base_mdat.inner();
            ident_dat = ident_mdat.inner();
            compute_delta(base_dat(), ident_dat(), tmp);
            del = delta(tmp);
          }
        else
          {
            return process_send_data_cmd(type, ident);
          }
      }
      break;
      
    default:
      throw bad_decode(F("delta requested for item type %s\n") % typestr);
    }
  queue_delta_cmd(type, base, ident, del);
  return true;
}

bool 
session::process_data_cmd(netcmd_item_type type,
                          id const & item, 
                          string const & dat)
{  
  hexenc<id> hitem;
  encode_hexenc(item, hitem);

  // it's ok if we received something we didn't ask for; it might
  // be a spontaneous transmission from refinement
  note_item_arrived(type, item);

  switch (type)
    {
    case epoch_item:
      if (this->app.db.epoch_exists(epoch_id(hitem)))
        {
          L(F("epoch '%s' already exists in our database\n") % hitem);
        }
      else
        {
          cert_value branch;
          epoch_data epoch;
          read_epoch(dat, branch, epoch);
          L(F("received epoch %s for branch %s\n") % epoch % branch);
          std::map<cert_value, epoch_data> epochs;
          app.db.get_epochs(epochs);
          std::map<cert_value, epoch_data>::const_iterator i;
          i = epochs.find(branch);
          if (i == epochs.end())
            {
              L(F("branch %s has no epoch; setting epoch to %s\n") % branch % epoch);
              app.db.set_epoch(branch, epoch);
              maybe_note_epochs_finished();
            }
          else
            {
              L(F("branch %s already has an epoch; checking\n") % branch);
              // if we get here, then we know that the epoch must be
              // different, because if it were the same then the
              // if(epoch_exists()) branch up above would have been taken.  if
              // somehow this is wrong, then we have broken epoch hashing or
              // something, which is very dangerous, so play it safe...
              I(!(i->second == epoch));

              // It is safe to call 'error' here, because if we get here,
              // then the current netcmd packet cannot possibly have
              // written anything to the database.
              error((F("Mismatched epoch on branch %s."
                       " Server has '%s', client has '%s'.")
                     % branch
                     % (voice == server_voice ? i->second : epoch)
                     % (voice == server_voice ? epoch : i->second)).str());
            }
        }
      break;
      
    case key_item:
      if (this->app.db.public_key_exists(hitem))
        L(F("public key '%s' already exists in our database\n")  % hitem);
      else
        {
          rsa_keypair_id keyid;
          base64<rsa_pub_key> pub;
          read_pubkey(dat, keyid, pub);
          hexenc<id> tmp;
          key_hash_code(keyid, pub, tmp);
          if (! (tmp == hitem))
            throw bad_decode(F("hash check failed for public key '%s' (%s);"
                               " wanted '%s' got '%s'")  
                             % hitem % keyid % hitem % tmp);
          this->dbw.consume_public_key(keyid, pub);
        }
      break;

    case cert_item:
      if (this->app.db.revision_cert_exists(hitem))
        L(F("cert '%s' already exists in our database\n")  % hitem);
      else
        {
          cert c;
          read_cert(dat, c);
          hexenc<id> tmp;
          cert_hash_code(c, tmp);
          if (! (tmp == hitem))
            throw bad_decode(F("hash check failed for revision cert '%s'")  % hitem);
//          this->dbw.consume_revision_cert(revision<cert>(c));
          received_certs[revision_id(c.ident)][c.name].push_back(c);
          if (!app.db.revision_exists(revision_id(c.ident)))
            {
              id rid;
              decode_hexenc(c.ident, rid);
              queue_send_data_cmd(revision_item, rid);
            }
        }
      break;

    case revision_item:
      {
        revision_id rid(hitem);
        if (this->app.db.revision_exists(rid))
          L(F("revision '%s' already exists in our database\n") % hitem);
        else
          {
	    L(F("received revision '%s'\n") % hitem);
            boost::shared_ptr< pair<revision_data, revision_set > > 
              rp(new pair<revision_data, revision_set>());
            
            rp->first = revision_data(dat);
            read_revision_set(dat, rp->second);
            ancestry.insert(std::make_pair(rid, rp));
            if (cert_refinement_done())
              {
                analyze_ancestry_graph();
              }
          }
      }
      break;

    case manifest_item:
      {
        manifest_id mid(hitem);
        if (this->app.db.manifest_version_exists(mid))
          L(F("manifest version '%s' already exists in our database\n") % hitem);
        else
          {
            this->dbw.consume_manifest_data(mid, manifest_data(dat));
            manifest_map man;
            read_manifest_map(data(dat), man);
            analyze_manifest(man);
          }
      }
      break;

    case file_item:
      {
        file_id fid(hitem);
        if (this->app.db.file_version_exists(fid))
          L(F("file version '%s' already exists in our database\n") % hitem);
        else
          {
            this->dbw.consume_file_data(fid, file_data(dat));
          }
      }
      break;

    }
  return true;
}

bool 
session::process_delta_cmd(netcmd_item_type type,
                           id const & base, 
                           id const & ident, 
                           delta const & del)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hbase, hident;
  encode_hexenc(base, hbase);
  encode_hexenc(ident, hident);

  pair<id,id> id_pair = make_pair(base, ident);

  // it's ok if we received something we didn't ask for; it might
  // be a spontaneous transmission from refinement
  note_item_arrived(type, ident);

  switch (type)
    {
    case manifest_item:
      {
        manifest_id src_manifest(hbase), dst_manifest(hident);
        if (full_delta_items[manifest_item]->find(ident)
            != full_delta_items[manifest_item]->end())
          {
            this->dbw.consume_manifest_delta(src_manifest, 
                                             dst_manifest,
                                             manifest_delta(del),
                                             true);
          }
        else if (reverse_delta_requests.find(id_pair)
            != reverse_delta_requests.end())
          {
            reverse_delta_requests.erase(id_pair);
            this->dbw.consume_manifest_reverse_delta(src_manifest, 
                                                     dst_manifest,
                                                     manifest_delta(del));
          }
        else
          this->dbw.consume_manifest_delta(src_manifest, 
                                           dst_manifest,
                                           manifest_delta(del));
        
      }
      break;

    case file_item:
      {
        file_id src_file(hbase), dst_file(hident);
        if (full_delta_items[file_item]->find(ident) 
            != full_delta_items[file_item]->end())
          {
            this->dbw.consume_file_delta(src_file, 
                                             dst_file,
                                             file_delta(del),
                                             true);
          }
        else if (reverse_delta_requests.find(id_pair)
            != reverse_delta_requests.end())
          {
            reverse_delta_requests.erase(id_pair);
            this->dbw.consume_file_reverse_delta(src_file, 
                                                 dst_file,
                                                 file_delta(del));
          }
        else
          this->dbw.consume_file_delta(src_file, 
                                       dst_file,
                                       file_delta(del));
      }
      break;
      
    default:
      L(F("ignoring delta received for item type %s\n") % typestr);
      break;
    }
  return true;
}

bool 
session::process_nonexistant_cmd(netcmd_item_type type,
                                 id const & item)
{
  string typestr;
  netcmd_item_type_to_string(type, typestr);
  hexenc<id> hitem;
  encode_hexenc(item, hitem);
  L(F("received 'nonexistant' netcmd for %s '%s'\n") 
    % typestr % hitem);
  note_item_arrived(type, item);
  return true;
}

bool
session::process_usher_cmd(utf8 const & msg)
{
  if (msg().size())
    {
      if (msg()[0] == '!')
        P(F("Received warning from usher: %s") % msg().substr(1));
      else
        L(F("Received greeting from usher: %s") % msg().substr(1));
    }
  netcmd cmdout;
  cmdout.write_usher_reply_cmd(peer_id, our_include_pattern);
  write_netcmd_and_try_flush(cmdout);
  L(F("Sent reply."));
  return true;
}

bool
session::merkle_node_exists(netcmd_item_type type,
                            size_t level,
                            prefix const & pref)
{
  map<netcmd_item_type, boost::shared_ptr<merkle_table> >::const_iterator i = 
    merkle_tables.find(type);
  
  I(i != merkle_tables.end());
  merkle_table::const_iterator j = i->second->find(std::make_pair(pref, level));
  return (j != i->second->end());
}

void 
session::load_merkle_node(netcmd_item_type type,
                          size_t level,
                          prefix const & pref,
                          merkle_ptr & node)
{
  map<netcmd_item_type, boost::shared_ptr<merkle_table> >::const_iterator i = 
    merkle_tables.find(type);

  I(i != merkle_tables.end());
  merkle_table::const_iterator j = i->second->find(std::make_pair(pref, level));
  I(j != i->second->end());
  node = j->second;
}


bool 
session::dispatch_payload(netcmd const & cmd)
{
  
  switch (cmd.get_cmd_code())
    {
      
    case bye_cmd:
      return process_bye_cmd();
      break;

    case error_cmd:
      {
        string errmsg;
        cmd.read_error_cmd(errmsg);
        return process_error_cmd(errmsg);
      }
      break;

    case hello_cmd:
      require(! authenticated, "hello netcmd received when not authenticated");
      require(voice == client_voice, "hello netcmd received in client voice");
      {
        rsa_keypair_id server_keyname;
        rsa_pub_key server_key;
        id nonce;
        cmd.read_hello_cmd(server_keyname, server_key, nonce);
        return process_hello_cmd(server_keyname, server_key, nonce);
      }
      break;

    case anonymous_cmd:
      require(! authenticated, "anonymous netcmd received when not authenticated");
      require(voice == server_voice, "anonymous netcmd received in server voice");
      require(role == source_role ||
              role == source_and_sink_role, 
              "anonymous netcmd received in source or source/sink role");
      {
        protocol_role role;
        utf8 their_include_pattern, their_exclude_pattern;
        rsa_oaep_sha_data hmac_key_encrypted;
        cmd.read_anonymous_cmd(role, their_include_pattern, their_exclude_pattern, hmac_key_encrypted);
        L(F("received 'anonymous' netcmd from client for pattern '%s' excluding '%s' "
            "in %s mode\n")
          % their_include_pattern % their_exclude_pattern
          % (role == source_and_sink_role ? _("source and sink") :
             (role == source_role ? _("source") : _("sink"))));

        set_session_key(hmac_key_encrypted);
        if (!process_anonymous_cmd(role, their_include_pattern, their_exclude_pattern))
            return false;
        queue_confirm_cmd();
        return true;
      }
      break;

    case auth_cmd:
      require(! authenticated, "auth netcmd received when not authenticated");
      require(voice == server_voice, "auth netcmd received in server voice");
      {
        protocol_role role;
        string signature;
        utf8 their_include_pattern, their_exclude_pattern;
        id client, nonce1, nonce2;
        rsa_oaep_sha_data hmac_key_encrypted;
        cmd.read_auth_cmd(role, their_include_pattern, their_exclude_pattern,
                          client, nonce1, hmac_key_encrypted, signature);

        hexenc<id> their_key_hash;
        encode_hexenc(client, their_key_hash);
        hexenc<id> hnonce1;
        encode_hexenc(nonce1, hnonce1);

        L(F("received 'auth(hmac)' netcmd from client '%s' for pattern '%s' "
            "exclude '%s' in %s mode with nonce1 '%s'\n")
          % their_key_hash % their_include_pattern % their_exclude_pattern
          % (role == source_and_sink_role ? _("source and sink") :
             (role == source_role ? _("source") : _("sink")))
          % hnonce1);

        set_session_key(hmac_key_encrypted);
        if (!process_auth_cmd(role, their_include_pattern, their_exclude_pattern,
                              client, nonce1, signature))
            return false;
        queue_confirm_cmd();
        return true;
      }
      break;

    case confirm_cmd:
      require(! authenticated, "confirm netcmd received when not authenticated");
      require(voice == client_voice, "confirm netcmd received in client voice");
      {
        string signature;
        cmd.read_confirm_cmd();
        this->authenticated = true;
        respond_to_confirm_cmd();
        return true;
      }
      break;

    case refine_cmd:
      require(authenticated, "refine netcmd received when authenticated");
      {
        merkle_node node;
        cmd.read_refine_cmd(node);
        map< netcmd_item_type, done_marker>::iterator i = done_refinements.find(node.type);
        require(i != done_refinements.end(), "refinement netcmd refers to valid type");
        require(i->second.tree_is_done == false, "refinement netcmd received when tree is live");
        i->second.current_level_had_refinements = true;
        return process_refine_cmd(node);
      }
      break;

    case done_cmd:
      require(authenticated, "done netcmd received when authenticated");
      {
        size_t level;
        netcmd_item_type type;
        cmd.read_done_cmd(level, type);
        return process_done_cmd(level, type);
      }
      break;

    case send_data_cmd:
      require(authenticated, "send_data netcmd received when authenticated");
      require(role == source_role ||
              role == source_and_sink_role, 
              "send_data netcmd received in source or source/sink role");
      {
        netcmd_item_type type;
        id item;
        cmd.read_send_data_cmd(type, item);
        return process_send_data_cmd(type, item);
      }
      break;

    case send_delta_cmd:
      require(authenticated, "send_delta netcmd received when authenticated");
      require(role == source_role ||
              role == source_and_sink_role, 
              "send_delta netcmd received in source or source/sink role");
      {
        netcmd_item_type type;
        id base, ident;
        cmd.read_send_delta_cmd(type, base, ident);
        return process_send_delta_cmd(type, base, ident);
      }

    case data_cmd:
      require(authenticated, "data netcmd received when authenticated");
      require(role == sink_role ||
              role == source_and_sink_role, 
              "data netcmd received in source or source/sink role");
      {
        netcmd_item_type type;
        id item;
        string dat;
        cmd.read_data_cmd(type, item, dat);
        return process_data_cmd(type, item, dat);
      }
      break;

    case delta_cmd:
      require(authenticated, "delta netcmd received when authenticated");
      require(role == sink_role ||
              role == source_and_sink_role, 
              "delta netcmd received in source or source/sink role");
      {
        netcmd_item_type type;
        id base, ident;
        delta del;
        cmd.read_delta_cmd(type, base, ident, del);
        return process_delta_cmd(type, base, ident, del);
      }
      break;      

    case nonexistant_cmd:
      require(authenticated, "nonexistant netcmd received when authenticated");
      require(role == sink_role ||
              role == source_and_sink_role, 
              "nonexistant netcmd received in sink or source/sink role");
      {
        netcmd_item_type type;
        id item;
        cmd.read_nonexistant_cmd(type, item);
        return process_nonexistant_cmd(type, item);
      }
      break;
    case usher_cmd:
      {
        utf8 greeting;
        cmd.read_usher_cmd(greeting);
        return process_usher_cmd(greeting);
      }
      break;
    case usher_reply_cmd:
      return false;// should not happen
      break;
    }
  return false;
}

// this kicks off the whole cascade starting from "hello"
void 
session::begin_service()
{
  keypair kp;
  app.keys.get_key_pair(app.signing_key, kp);
  queue_hello_cmd(app.signing_key, kp.pub, mk_nonce());
}

void 
session::maybe_say_goodbye()
{
  if (done_all_refinements() &&
      got_all_data() && !sent_goodbye)
    queue_bye_cmd();
}

bool 
session::arm()
{
  if (!armed)
    {
      if (outbuf_size > constants::bufsz * 10)
        return false; // don't pack the buffer unnecessarily

      if (cmd.read(inbuf, read_hmac))
        {
          armed = true;
        }
    }
  return armed;
}      

bool session::process()
{
  try 
    {      
      if (!arm())
        return true;
      
      transaction_guard guard(app.db);
      armed = false;
      L(F("processing %d byte input buffer from peer %s\n") % inbuf.size() % peer_id);
      bool ret = dispatch_payload(cmd);
      if (inbuf.size() >= constants::netcmd_maxsz)
        W(F("input buffer for peer %s is overfull after netcmd dispatch\n") % peer_id);
      guard.commit();
      maybe_say_goodbye();
      if (!ret)
        P(F("failed to process '%s' packet") % cmd.get_cmd_code());
      return ret;
    }
  catch (bad_decode & bd)
    {
      W(F("protocol error while processing peer %s: '%s'\n") % peer_id % bd.what);
      return false;
    }
}


static void 
call_server(protocol_role role,
            utf8 const & include_pattern,
            utf8 const & exclude_pattern,
            app_state & app,
            utf8 const & address,
            Netxx::port_type default_port,
            unsigned long timeout_seconds)
{
  Netxx::Probe probe;
  Netxx::Timeout timeout(static_cast<long>(timeout_seconds)), instant(0,1);
#ifdef USE_IPV6
  bool use_ipv6=true;
#else
  bool use_ipv6=false;
#endif

  // FIXME: split into labels and convert to ace here.

  P(F("connecting to %s\n") % address());
  Netxx::Address addr(address().c_str(), default_port, use_ipv6);
  Netxx::Stream server(addr, timeout);
  session sess(role, client_voice, include_pattern, exclude_pattern,
               app, address(), server.get_socketfd(), timeout);
  
  while (true)
    {       
      bool armed = false;
      try 
        {
          armed = sess.arm();
        }
      catch (bad_decode & bd)
        {
          W(F("protocol error while processing peer %s: '%s'\n") 
            % sess.peer_id % bd.what);
          return;         
        }

      probe.clear();
      probe.add(sess.str, sess.which_events());
      Netxx::Probe::result_type res = probe.ready(armed ? instant : timeout);
      Netxx::Probe::ready_type event = res.second;
      Netxx::socket_type fd = res.first;
      
      if (fd == -1 && !armed) 
        {
          P(F("timed out waiting for I/O with peer %s, disconnecting\n") % sess.peer_id);
          return;
        }
      
      if (event & Netxx::Probe::ready_read)
        {
          if (sess.read_some())
            {
              try 
                {
                  armed = sess.arm();
                }
              catch (bad_decode & bd)
                {
                  W(F("protocol error while processing peer %s: '%s'\n") 
                    % sess.peer_id % bd.what);
                  return;         
                }
            }
          else
            {         
              if (sess.sent_goodbye)
                P(F("read from fd %d (peer %s) closed OK after goodbye\n") % fd % sess.peer_id);
              else
                P(F("read from fd %d (peer %s) failed, disconnecting\n") % fd % sess.peer_id);
              return;
            }
        }
      
      if (event & Netxx::Probe::ready_write)
        {
          if (! sess.write_some())
            {
              if (sess.sent_goodbye)
                P(F("write on fd %d (peer %s) closed OK after goodbye\n") % fd % sess.peer_id);
              else
                P(F("write on fd %d (peer %s) failed, disconnecting\n") % fd % sess.peer_id);
              return;
            }
        }
      
      if (event & Netxx::Probe::ready_oobd)
        {
          P(F("got OOB data on fd %d (peer %s), disconnecting\n") 
            % fd % sess.peer_id);
          return;
        }

      if (armed)
        {
          if (!sess.process())
            {
              P(F("terminated exchange with %s\n") 
                % sess.peer_id);
              return;
            }
        }

      if (sess.sent_goodbye && sess.outbuf.empty() && sess.received_goodbye)
        {
          P(F("successful exchange with %s\n") 
            % sess.peer_id);
          return;
        }
    }
}

static void 
arm_sessions_and_calculate_probe(Netxx::Probe & probe,
                                 map<Netxx::socket_type, shared_ptr<session> > & sessions,
                                 set<Netxx::socket_type> & armed_sessions)
{
  set<Netxx::socket_type> arm_failed;
  for (map<Netxx::socket_type, 
         shared_ptr<session> >::const_iterator i = sessions.begin();
       i != sessions.end(); ++i)
    {
      try 
        {
          if (i->second->arm())
            {
              L(F("fd %d is armed\n") % i->first);
              armed_sessions.insert(i->first);
            }
          probe.add(i->second->str, i->second->which_events());
        }
      catch (bad_decode & bd)
        {
          W(F("protocol error while processing peer %s: '%s', marking as bad\n") 
            % i->second->peer_id % bd.what);
          arm_failed.insert(i->first);
        }         
    }
  for (set<Netxx::socket_type>::const_iterator i = arm_failed.begin();
       i != arm_failed.end(); ++i)
    {
      sessions.erase(*i);
    }
}

static void
handle_new_connection(Netxx::Address & addr,
                      Netxx::StreamServer & server,
                      Netxx::Timeout & timeout,
                      protocol_role role,
                      utf8 const & include_pattern,
                      utf8 const & exclude_pattern,
                      map<Netxx::socket_type, shared_ptr<session> > & sessions,
                      app_state & app)
{
  L(F("accepting new connection on %s : %s\n") 
    % addr.get_name() % lexical_cast<string>(addr.get_port()));
  Netxx::Peer client = server.accept_connection();
  
  if (!client) 
    {
      L(F("accept() returned a dead client\n"));
    }
  else
    {
      P(F("accepted new client connection from %s : %s\n")
        % client.get_address() % lexical_cast<string>(client.get_port()));
      shared_ptr<session> sess(new session(role, server_voice,
                                           include_pattern, exclude_pattern,
                                           app,
                                           lexical_cast<string>(client), 
                                           client.get_socketfd(), timeout));
      sess->begin_service();
      sessions.insert(make_pair(client.get_socketfd(), sess));
    }
}

static void 
handle_read_available(Netxx::socket_type fd,
                      shared_ptr<session> sess,
                      map<Netxx::socket_type, shared_ptr<session> > & sessions,
                      set<Netxx::socket_type> & armed_sessions,
                      bool & live_p)
{
  if (sess->read_some())
    {
      try
        {
          if (sess->arm())
            armed_sessions.insert(fd);
        }
      catch (bad_decode & bd)
        {
          W(F("protocol error while processing peer %s: '%s', disconnecting\n") 
            % sess->peer_id % bd.what);
          sessions.erase(fd);
          live_p = false;
        }
    }
  else
    {
      P(F("fd %d (peer %s) read failed, disconnecting\n") 
        % fd % sess->peer_id);
      sessions.erase(fd);
      live_p = false;
    }
}


static void 
handle_write_available(Netxx::socket_type fd,
                       shared_ptr<session> sess,
                       map<Netxx::socket_type, shared_ptr<session> > & sessions,
                       bool & live_p)
{
  if (! sess->write_some())
    {
      P(F("fd %d (peer %s) write failed, disconnecting\n") 
        % fd % sess->peer_id);
      sessions.erase(fd);
      live_p = false;
    }
}

static void
process_armed_sessions(map<Netxx::socket_type, shared_ptr<session> > & sessions,
                       set<Netxx::socket_type> & armed_sessions)
{
  for (set<Netxx::socket_type>::const_iterator i = armed_sessions.begin();
       i != armed_sessions.end(); ++i)
    {
      map<Netxx::socket_type, shared_ptr<session> >::iterator j;
      j = sessions.find(*i);
      if (j == sessions.end())
        continue;
      else
        {
          Netxx::socket_type fd = j->first;
          shared_ptr<session> sess = j->second;
          if (!sess->process())
            {
              P(F("fd %d (peer %s) processing finished, disconnecting\n") 
                % fd % sess->peer_id);
              sessions.erase(j);
            }
        }
    }
}

static void
reap_dead_sessions(map<Netxx::socket_type, shared_ptr<session> > & sessions,
                   unsigned long timeout_seconds)
{
  // kill any clients which haven't done any i/o inside the timeout period
  // or who have said goodbye and flushed their output buffers
  set<Netxx::socket_type> dead_clients;
  time_t now = ::time(NULL);
  for (map<Netxx::socket_type, shared_ptr<session> >::const_iterator i = sessions.begin();
       i != sessions.end(); ++i)
    {
      if (static_cast<unsigned long>(i->second->last_io_time + timeout_seconds) 
          < static_cast<unsigned long>(now))
        {
          P(F("fd %d (peer %s) has been idle too long, disconnecting\n") 
            % i->first % i->second->peer_id);
          dead_clients.insert(i->first);
        }
      if (i->second->sent_goodbye && i->second->outbuf.empty() && i->second->received_goodbye)
        {
          P(F("fd %d (peer %s) exchanged goodbyes and flushed output, disconnecting\n") 
            % i->first % i->second->peer_id);
          dead_clients.insert(i->first);
        }
    }
  for (set<Netxx::socket_type>::const_iterator i = dead_clients.begin();
       i != dead_clients.end(); ++i)
    {
      sessions.erase(*i);
    }
}

static void 
serve_connections(protocol_role role,
                  utf8 const & include_pattern,
                  utf8 const & exclude_pattern,
                  app_state & app,
                  utf8 const & address,
                  Netxx::port_type default_port,
                  unsigned long timeout_seconds,
                  unsigned long session_limit)
{
  Netxx::Probe probe;  

  Netxx::Timeout 
    forever, 
    timeout(static_cast<long>(timeout_seconds)), 
    instant(0,1);

  if (!app.bind_port().empty())
    default_port = ::atoi(app.bind_port().c_str());
#ifdef USE_IPV6
  bool use_ipv6=true;
#else
  bool use_ipv6=false;
#endif
  Netxx::Address addr(use_ipv6);

  if (!app.bind_address().empty()) 
      addr.add_address(app.bind_address().c_str(), default_port);
  else
      addr.add_all_addresses (default_port);


  Netxx::StreamServer server(addr, timeout);
  const char *name = addr.get_name();
  P(F("beginning service on %s : %s\n") 
    % (name != NULL ? name : "all interfaces") % lexical_cast<string>(addr.get_port()));
  
  map<Netxx::socket_type, shared_ptr<session> > sessions;
  set<Netxx::socket_type> armed_sessions;
  
  while (true)
    {      
      probe.clear();
      armed_sessions.clear();

      if (sessions.size() >= session_limit)
        W(F("session limit %d reached, some connections will be refused\n") % session_limit);
      else
        probe.add(server);

      arm_sessions_and_calculate_probe(probe, sessions, armed_sessions);

      L(F("i/o probe with %d armed\n") % armed_sessions.size());      
      Netxx::Probe::result_type res = probe.ready(sessions.empty() ? forever 
                                           : (armed_sessions.empty() ? timeout 
                                              : instant));
      Netxx::Probe::ready_type event = res.second;
      Netxx::socket_type fd = res.first;
      
      if (fd == -1)
        {
          if (armed_sessions.empty()) 
            L(F("timed out waiting for I/O (listening on %s : %s)\n") 
              % addr.get_name() % lexical_cast<string>(addr.get_port()));
        }
      
      // we either got a new connection
      else if (fd == server)
        handle_new_connection(addr, server, timeout, role, 
                              include_pattern, exclude_pattern, sessions, app);
      
      // or an existing session woke up
      else
        {
          map<Netxx::socket_type, shared_ptr<session> >::iterator i;
          i = sessions.find(fd);
          if (i == sessions.end())
            {
              L(F("got woken up for action on unknown fd %d\n") % fd);
            }
          else
            {
              shared_ptr<session> sess = i->second;
              bool live_p = true;

              if (event & Netxx::Probe::ready_read)
                handle_read_available(fd, sess, sessions, armed_sessions, live_p);
                
              if (live_p && (event & Netxx::Probe::ready_write))
                handle_write_available(fd, sess, sessions, live_p);
                
              if (live_p && (event & Netxx::Probe::ready_oobd))
                {
                  P(F("got some OOB data on fd %d (peer %s), disconnecting\n") 
                    % fd % sess->peer_id);
                  sessions.erase(i);
                }
            }
        }
      process_armed_sessions(sessions, armed_sessions);
      reap_dead_sessions(sessions, timeout_seconds);
    }
}


/////////////////////////////////////////////////
//
// layer 4: monotone interface layer
//
/////////////////////////////////////////////////

static boost::shared_ptr<merkle_table>
make_root_node(session & sess,
               netcmd_item_type ty)
{
  boost::shared_ptr<merkle_table> tab = 
    boost::shared_ptr<merkle_table>(new merkle_table());
  
  merkle_ptr tmp = merkle_ptr(new merkle_node());
  tmp->type = ty;

  tab->insert(std::make_pair(std::make_pair(get_root_prefix().val, 0), tmp));

  sess.merkle_tables[ty] = tab;
  return tab;
}

void
insert_with_parents(revision_id rev, set<revision_id> & col, app_state & app, ticker & revisions_ticker)
{
  vector<revision_id> frontier;
  frontier.push_back(rev);
  while (!frontier.empty())
    {
      revision_id rid = frontier.back();
      frontier.pop_back();
      if (!null_id(rid) && col.find(rid) == col.end())
        {
          ++revisions_ticker;
          col.insert(rid);
          std::set<revision_id> parents;
          app.db.get_revision_parents(rid, parents);
          for (std::set<revision_id>::const_iterator i = parents.begin();
               i != parents.end(); ++i)
            {
              frontier.push_back(*i);
            }
        }
    }
}

void 
session::rebuild_merkle_trees(app_state & app,
                              set<utf8> const & branchnames)
{
  P(F("finding items to synchronize:\n"));
  for (set<utf8>::const_iterator i = branchnames.begin();
      i != branchnames.end(); ++i)
    L(F("including branch %s") % *i);

  boost::shared_ptr<merkle_table> ctab = make_root_node(*this, cert_item);
  boost::shared_ptr<merkle_table> ktab = make_root_node(*this, key_item);
  boost::shared_ptr<merkle_table> etab = make_root_node(*this, epoch_item);

  // xgettext: please use short message and try to avoid multibytes chars
  ticker revisions_ticker(_("revisions"), "r", 64);
  // xgettext: please use short message and try to avoid multibytes chars
  ticker certs_ticker(_("certs"), "c", 256);
  // xgettext: please use short message and try to avoid multibytes chars
  ticker keys_ticker(_("keys"), "k", 1);

  set<revision_id> revision_ids;
  set<rsa_keypair_id> inserted_keys;
  
  {
    // Get our branches
    vector<string> names;
    get_branches(app, names);
    for (size_t i = 0; i < names.size(); ++i)
      {
        if(branchnames.find(names[i]) != branchnames.end())
          {
            // branch matches, get its certs
            vector< revision<cert> > certs;
            base64<cert_value> encoded_name;
            encode_base64(cert_value(names[i]),encoded_name);
            app.db.get_revision_certs(branch_cert_name, encoded_name, certs);
            for (vector< revision<cert> >::const_iterator j = certs.begin();
                 j != certs.end(); j++)
              {
                insert_with_parents(revision_id(j->inner().ident),
                                    revision_ids, app, revisions_ticker);
                // branch certs go in here, others later on
                hexenc<id> hash;
                cert_hash_code(j->inner(), hash);
                insert_into_merkle_tree(*ctab, cert_item, true, hash, 0);
                if (inserted_keys.find(j->inner().key) == inserted_keys.end())
                    inserted_keys.insert(j->inner().key);
              }
          }
      }
  }
    
  {
    map<cert_value, epoch_data> epochs;
    app.db.get_epochs(epochs);
    
    epoch_data epoch_zero(std::string(constants::epochlen, '0'));
    for (std::set<utf8>::const_iterator i = branchnames.begin();
         i != branchnames.end(); ++i)
      {
        cert_value branch((*i)());
        std::map<cert_value, epoch_data>::const_iterator j;
        j = epochs.find(branch);
        // set to zero any epoch which is not yet set    
        if (j == epochs.end())
          {
            L(F("setting epoch on %s to zero\n") % branch);
            epochs.insert(std::make_pair(branch, epoch_zero));
            app.db.set_epoch(branch, epoch_zero);
          }
        // then insert all epochs into merkle tree
        j = epochs.find(branch);
        I(j != epochs.end());
        epoch_id eid;
        epoch_hash_code(j->first, j->second, eid);
        insert_into_merkle_tree(*etab, epoch_item, true, eid.inner(), 0);
      }
  }
  
  {
    typedef std::vector< std::pair<hexenc<id>,
      std::pair<revision_id, rsa_keypair_id> > > cert_idx;
    
    cert_idx idx;
    app.db.get_revision_cert_nobranch_index(idx);
    
    // insert all non-branch certs reachable via these revisions
    // (branch certs were inserted earlier)
    for (cert_idx::const_iterator i = idx.begin(); i != idx.end(); ++i)
      {
        hexenc<id> const & hash = i->first;
        revision_id const & ident = i->second.first;
        rsa_keypair_id const & key = i->second.second;
        
        if (revision_ids.find(ident) == revision_ids.end())
          continue;
        
        insert_into_merkle_tree(*ctab, cert_item, true, hash, 0);
        ++certs_ticker;
        if (inserted_keys.find(key) == inserted_keys.end())
            inserted_keys.insert(key);
      }
  }

  // add any keys specified on the command line
  for (vector<rsa_keypair_id>::const_iterator key = app.keys_to_push.begin();
       key != app.keys_to_push.end(); ++key)
    {
      if (inserted_keys.find(*key) == inserted_keys.end())
        {
          if (!app.db.public_key_exists(*key))
            {
              if (app.keys.key_pair_exists(*key))
                app.keys.ensure_in_database(*key);
              else
                W(F("Cannot find key '%s'") % *key);
            }
          inserted_keys.insert(*key);
        }
    }
  // insert all the keys
  for (set<rsa_keypair_id>::const_iterator key = inserted_keys.begin();
       key != inserted_keys.end(); key++)
    {
      if (app.db.public_key_exists(*key))
        {
          base64<rsa_pub_key> pub_encoded;
          app.db.get_key(*key, pub_encoded);
          hexenc<id> keyhash;
          key_hash_code(*key, pub_encoded, keyhash);
          insert_into_merkle_tree(*ktab, key_item, true, keyhash, 0);
          ++keys_ticker;
        }
    }

  recalculate_merkle_codes(*etab, get_root_prefix().val, 0);
  recalculate_merkle_codes(*ktab, get_root_prefix().val, 0);
  recalculate_merkle_codes(*ctab, get_root_prefix().val, 0);
}

void 
run_netsync_protocol(protocol_voice voice, 
                     protocol_role role, 
                     utf8 const & addr, 
                     utf8 const & include_pattern,
                     utf8 const & exclude_pattern,
                     app_state & app)
{
  try 
    {
      if (voice == server_voice)
        {
          serve_connections(role, include_pattern, exclude_pattern, app,
                            addr, static_cast<Netxx::port_type>(constants::netsync_default_port), 
                            static_cast<unsigned long>(constants::netsync_timeout_seconds), 
                            static_cast<unsigned long>(constants::netsync_connection_limit));
        }
      else    
        {
          I(voice == client_voice);
          transaction_guard guard(app.db);
          call_server(role, include_pattern, exclude_pattern, app,
                      addr, static_cast<Netxx::port_type>(constants::netsync_default_port), 
                      static_cast<unsigned long>(constants::netsync_timeout_seconds));
          guard.commit();
        }
    }
  catch (Netxx::NetworkException & e)
    {      
      throw informative_failure((F("network error: %s") % e.what()).str());
    }
  catch (Netxx::Exception & e)
    {      
      throw oops((F("network error: %s") % e.what()).str());;
    }
}


// Steps for determining files/manifests to request, from 
// a given revision ancestry:
//
// 1) find the new heads, consume valid branch certs etc.
//
// 2) foreach new head, traverse up the revision ancestry, building
// a set of reverse file/manifest deltas (we stop when we hit an
// already-seen or existing-in-db rev). 
//
// at the same time, build a (smaller) set of forward deltas (files and
// manifests). these have a file/manifest in the new head as the
// destination, and end up having an item already existing in the
// database as the source (or null, in which case full data is
// requested).
//
// 3) For each file/manifest in head, first request the forward delta
// (or full data if there is no path back to existing data). Then
// traverse up the set of reverse deltas, daisychaining our way until
// we get to existing revisions.
//
// Notes:
//
// - The database stores reverse deltas, so preferring these allows
// a server to send pre-computed deltas straight from the database
// (this isn't done yet). In order to bootstrap the tip-most data,
// forward deltas from a close(est?)-ancestor are used, or full data
// is requested if there is no existing ancestor.
//
// eg, if we have the (manifest) ancestry
// A -> B -> C -> D
// where A is existing, {B,C,D} are new, then we will request deltas
// A->D (fwd)
// D->C (rev)
// C->B (rev)
// This may result in slightly larger deltas than using all forward
// deltas, however it should be more efficient.
//
// - in order to keep a good hit ratio with the reconstructed version
// cache in database, we'll request deltas for a single file/manifest
// all at once, rather than requesting deltas per-revision. This
// requires a bit more memory usage, though it will be less memory
// than would be required to store all the outgoing delta requests
// anyway.
ancestry_fetcher::ancestry_fetcher(session & s)
    : sess(s)
{
  set<revision_id> new_heads;
  sess.get_heads_and_consume_certs( new_heads );

  L(F("ancestry_fetcher: got %d heads") % new_heads.size());
 
  traverse_ancestry(new_heads);

  request_files();
  request_manifests();
}

// adds file deltas from the given changeset into the sets of forward
// and reverse deltas
void
ancestry_fetcher::traverse_files(change_set const & cset)
{
  for (change_set::delta_map::const_iterator d = cset.deltas.begin(); 
       d != cset.deltas.end(); ++d)
    {
      file_id parent_file (delta_entry_src(d));
      file_id child_file (delta_entry_dst(d));
      MM(parent_file);
      MM(child_file);

      I(!(parent_file == child_file));
      // when changeset format is altered to have [...]->[] deltas on deletion,
      // this assertion needs revisiting
      I(!null_id(child_file));

      // request the reverse delta
      if (!null_id(parent_file))
        {
          rev_file_deltas.insert(make_pair(child_file, parent_file));
        }

      // add any new forward deltas
      if (seen_files.find(child_file) == seen_files.end())
        {
          fwd_file_deltas.insert( make_pair( parent_file, child_file ) );
        }

      // update any forward deltas. no point updating if it already
      // points to something we have.
      if (!null_id(parent_file)
          && fwd_file_deltas.find(child_file) != fwd_file_deltas.end())
        {
          // We're traversing with child->parent of A->B.
          // Update any forward deltas with a parent of B to 
          // have A as a parent, ie B->C becomes A->C.
          for (multimap<file_id,file_id>::iterator d = 
               fwd_file_deltas.lower_bound(child_file);
               d != fwd_file_deltas.upper_bound(child_file);
               d++)
            {
              fwd_file_deltas.insert(make_pair(parent_file, d->second));
            }

            fwd_file_deltas.erase(fwd_file_deltas.lower_bound(child_file),
                                  fwd_file_deltas.upper_bound(child_file));
        }

      seen_files.insert(child_file);
      seen_files.insert(parent_file);
    }
}

// adds the given manifest deltas to the sets of forward and reverse deltas
void
ancestry_fetcher::traverse_manifest(manifest_id const & child_man,
                                    manifest_id const & parent_man)
{
  MM(child_man);
  MM(parent_man);
  I(!null_id(child_man));
  // add reverse deltas
  if (!null_id(parent_man))
    {
      rev_manifest_deltas.insert(make_pair(child_man, parent_man));
    }
  
  // handle the manifest forward-deltas
  if (!null_id(parent_man)
      // don't update child to itself, it makes the loop iterate infinitely.
      && !(parent_man == child_man)
      && fwd_manifest_deltas.find(child_man) != fwd_manifest_deltas.end())
    {
      // We're traversing with child->parent of A->B.
      // Update any forward deltas with a parent of B to 
      // have A as a parent, ie B->C becomes A->C.
      for (multimap<manifest_id,manifest_id>::iterator d = 
           fwd_manifest_deltas.lower_bound(child_man);
           d != fwd_manifest_deltas.upper_bound(child_man);
           d++)
        {
          fwd_manifest_deltas.insert(make_pair(parent_man, d->second));
        }

      fwd_manifest_deltas.erase(fwd_manifest_deltas.lower_bound(child_man),
                                fwd_manifest_deltas.upper_bound(child_man));
    }
}

// traverse up the ancestry for each of the given new head revisions,
// storing sets of file and manifest deltas
void
ancestry_fetcher::traverse_ancestry(set<revision_id> const & heads)
{
  deque<revision_id> frontier;
  set<revision_id> seen_revs;

  for (set<revision_id>::const_iterator h = heads.begin(); 
       h != heads.end(); h++)
    {
      L(F("traversing head %s") % *h);
      frontier.push_back(*h);
      seen_revs.insert(*h);
      manifest_id const & m = sess.ancestry[*h]->second.new_manifest;
      fwd_manifest_deltas.insert(make_pair(m,m));
    }

  // breadth first up the ancestry
  while (!frontier.empty())
    {
      revision_id const & rev = frontier.front();
      MM(rev);

      I(sess.ancestry.find(rev) != sess.ancestry.end());

      for (edge_map::const_iterator e = sess.ancestry[rev]->second.edges.begin();
           e != sess.ancestry[rev]->second.edges.end(); e++)
        {
          revision_id const & par = edge_old_revision(e);
          MM(par);
          if (seen_revs.find(par) == seen_revs.end())
            {
              if (sess.ancestry.find(par) != sess.ancestry.end())
                {
                  frontier.push_back(par);
                }
              seen_revs.insert(par);
            }

          traverse_manifest(sess.ancestry[rev]->second.new_manifest,
                            edge_old_manifest(e));
          traverse_files(edge_changes(e));

        }

      sess.dbw.consume_revision_data(rev, sess.ancestry[rev]->first);
      frontier.pop_front();
    }
}

void
ancestry_fetcher::request_rev_file_deltas(file_id const & start, 
                                          set<file_id> & done_files)
{
  stack< file_id > frontier;
  frontier.push(start);

  while (!frontier.empty())
    {
      file_id const child = frontier.top();
      MM(child);
      I(!null_id(child));
      frontier.pop();

      for (multimap< file_id, file_id>::const_iterator
           d = rev_file_deltas.lower_bound(child);
           d != rev_file_deltas.upper_bound(child);
           d++)
        {
          file_id const & parent = d->second;
          MM(parent);
          I(!null_id(parent));
          if (done_files.find(parent) == done_files.end())
            {
              done_files.insert(parent);
              if (!sess.app.db.file_version_exists(parent))
                {
                  sess.queue_send_delta_cmd(file_item,
                                            plain_id(child), plain_id(parent));
                  sess.reverse_delta_requests.insert(make_pair(plain_id(child),
                                                               plain_id(parent)));
                }
              frontier.push(parent);
            }
        }
    }
}

void
ancestry_fetcher::request_files()
{
  // just a cache to avoid checking db.foo_version_exists() too much
  set<file_id> done_files;

  for (multimap<file_id,file_id>::const_iterator d = fwd_file_deltas.begin();
       d != fwd_file_deltas.end(); d++)
    {
      file_id const & anc = d->first;
      file_id const & child = d->second;
      MM(anc);
      MM(child);
      if (!sess.app.db.file_version_exists(child))
        {
          if (null_id(anc)
              || !sess.app.db.file_version_exists(anc))
            {
              sess.queue_send_data_cmd(file_item, plain_id(child));
            }
          else
            {
              sess.queue_send_delta_cmd(file_item, 
                                        plain_id(anc), plain_id(child));
              sess.note_item_full_delta(file_item, plain_id(child));
            }
        }

      // traverse up the reverse deltas
      request_rev_file_deltas(child, done_files);
    }
}

void
ancestry_fetcher::request_rev_manifest_deltas(manifest_id const & start,
                                              set<manifest_id> & done_manifests)
{
  stack< manifest_id > frontier;
  frontier.push(start);

  while (!frontier.empty())
    {
      manifest_id const child = frontier.top();
      MM(child);
      I(!null_id(child));
      frontier.pop();

      for (multimap< manifest_id, manifest_id>::const_iterator
           d = rev_manifest_deltas.lower_bound(child);
           d != rev_manifest_deltas.upper_bound(child);
           d++)
        {
          manifest_id const & parent = d->second;
          MM(parent);
          I(!null_id(parent));
          if (done_manifests.find(parent) == done_manifests.end())
            {
              done_manifests.insert(parent);
              if (!sess.app.db.manifest_version_exists(parent))
                {
                  sess.queue_send_delta_cmd(manifest_item,
                                            plain_id(child), plain_id(parent));
                  sess.reverse_delta_requests.insert(make_pair(plain_id(child),
                                                               plain_id(parent)));
                }
              frontier.push(parent);
            }
        }
    }
}

// could try and make this a template function, is the same as request_files(),
// though it calls non-template functions
void
ancestry_fetcher::request_manifests()
{
  // just a cache to avoid checking db.foo_version_exists() too much
  set<manifest_id> done_manifests;

  for (multimap<manifest_id,manifest_id>::const_iterator d = fwd_manifest_deltas.begin();
       d != fwd_manifest_deltas.end(); d++)
    {
      manifest_id const & anc = d->first;
      manifest_id const & child = d->second;
      MM(anc);
      MM(child);
      if (!sess.app.db.manifest_version_exists(child))
        {
          if (null_id(anc)
              || !sess.app.db.manifest_version_exists(anc))
            {
              sess.queue_send_data_cmd(manifest_item, plain_id(child));
            }
          else
            {
              sess.queue_send_delta_cmd(manifest_item, 
                                        plain_id(anc), plain_id(child));
              sess.note_item_full_delta(manifest_item, plain_id(child));
            }
        }

      // traverse up the reverse deltas
      request_rev_manifest_deltas(child, done_manifests);
    }
}
