// SPDX-License-Identifier: MIT
// Actual-engine integration tests. Every radio packet uses the real codec and
// production crypto adapter. Only storage, entropy and allocation are fake.
#include "messaging.h"
#include "meshcore_message.h"
#include "meshtastic_message.h"
#include <RNG.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace chat=tunnel::messaging;
namespace mc=tunnel::meshcore_message;
namespace mt=tunnel::meshtastic;
using Protocol=tunnel::mesh::Protocol;
static unsigned checks=0,failures=0;static const char* scenario="initial";
#define CHECK(x) do{++checks;if(!(x)){++failures;std::fprintf(stderr,"FAIL %s:%d: %s\n",scenario,__LINE__,#x);}}while(0)
#define REQUIRE(x) do{const bool required_result=bool(x);CHECK(required_result);if(!required_result){chat::end();return;}}while(0)
RNGClass::RNGClass(){}RNGClass::~RNGClass(){}void RNGClass::rand(std::uint8_t*,std::size_t){std::abort();}RNGClass RNG;
static std::map<std::string,std::vector<std::uint8_t>> blobs;
static std::string failSave,invalidLoad;
static bool failAllocation=false;
static std::uint32_t randomState=0x1234abcd;
static unsigned released=0,saveCalls=0;
static chat::Load load(const char* key,void* bytes,std::size_t n){
  if(invalidLoad==key)return chat::Load::Invalid;
  const auto it=blobs.find(key);if(it==blobs.end())return chat::Load::Missing;
  if(it->second.size()!=n)return chat::Load::Invalid;std::memcpy(bytes,it->second.data(),n);return chat::Load::Ok;
}
static bool save(const char* key,const void* bytes,std::size_t n){
  ++saveCalls;CHECK(std::string(key)=="identity-v1" || std::string(key)=="contacts-v1");
  if(failSave==key)return false;
  auto p=static_cast<const std::uint8_t*>(bytes);blobs[key]=std::vector<std::uint8_t>(p,p+n);return true;
}
static std::uint32_t randomValue(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
static void* allocate(std::size_t n){CHECK(n==chat::workspaceBytes());return failAllocation?nullptr:std::malloc(n);}
static void release(void* p){auto b=static_cast<const std::uint8_t*>(p);bool zero=true;for(std::size_t i=0;i<chat::workspaceBytes();++i)zero&=b[i]==0;CHECK(zero);++released;std::free(p);}
static chat::Storage storage{load,save,randomValue,allocate,release};
static chat::Settings defaults;
static constexpr std::uint32_t Epoch=1800000000U;
static void reset(){chat::end();blobs.clear();failSave.clear();invalidLoad.clear();failAllocation=false;randomState=0x1234abcd;saveCalls=0;defaults=chat::Settings{};}
static chat::View view(){chat::View v;chat::view(v);return v;}
static unsigned nibble(char ch){return ch>='a'?unsigned(ch-'a'+10):unsigned(ch-'0');}
static void localPublic(std::uint8_t out[32]){const auto v=view();for(unsigned i=0;i<32;++i)out[i]=std::uint8_t(nibble(v.identity[2*i])*16+nibble(v.identity[2*i+1]));}
static std::uint32_t le32(const std::uint8_t* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
static void put32(std::uint8_t* p,std::uint32_t v){for(unsigned i=0;i<4;++i)p[i]=std::uint8_t(v>>(8*i));}
static std::uint32_t fnv(const std::uint8_t* p,std::size_t n){std::uint32_t h=2166136261U;while(n--){h^=*p++;h*=16777619U;}return h;}
static void updateChecksum(std::vector<std::uint8_t>& b){CHECK(b.size()>=4);std::fill(b.end()-4,b.end(),0);put32(b.data()+b.size()-4,fnv(b.data(),b.size()));}
static bool start(Protocol p){const bool ok=chat::begin(p,defaults,"LongFast",storage);CHECK(ok);CHECK(chat::active());return ok&&view().identity_ready;}
static bool take(std::uint32_t now,mt::Frame& f,bool success=true){
  const std::uint8_t* bytes=nullptr;std::size_t n=0;const bool ok=chat::nextFrame(now,bytes,n);
  if(!ok)return false;CHECK(bytes!=nullptr&&n>0&&n<=255);f=mt::Frame{};f.length=n;std::memcpy(f.bytes,bytes,n);chat::transmitted(success,now);return true;
}
static mc::Identity mcPeer(unsigned number=1){std::uint8_t seed[32];for(unsigned i=0;i<32;++i)seed[i]=std::uint8_t(i+number*17);mc::Identity peer;CHECK(mc::identity(seed,mc::platformCrypto(),peer));return peer;}
static mc::Contact mcLocal(){mc::Contact c;localPublic(c.public_key);std::strcpy(c.name,"LORA Tunnel");return c;}
static unsigned discoverMc(const mc::Identity& peer,const char* name="Bob",bool trust=true){
  mc::Frame ad;CHECK(mc::advert(peer,name,Epoch,mc::platformCrypto(),ad));chat::receive(ad.bytes,ad.length,1000,Epoch);
  auto v=view();unsigned index=unsigned(v.contact_count);for(unsigned i=0;i<v.contact_count;++i)if(!std::memcmp(v.contacts[i].public_key,peer.public_key,32)){index=i;break;}
  CHECK(index<v.contact_count);if(trust&&index<v.contact_count)CHECK(chat::trustContact(index,peer.public_key));return index;
}
static mt::Identity mtPeer(unsigned number=1){mt::Identity peer;peer.node=0x55660000+number;for(unsigned i=0;i<32;++i)peer.private_key[i]=std::uint8_t(i+number*21);CHECK(mt::platformCrypto().derivePublic(peer.private_key,peer.public_key));std::strcpy(peer.name,"Bob");std::strcpy(peer.short_name,"BOB");return peer;}
static mt::Channel channel(){mt::Channel c;const std::uint8_t key[16]={0xd4,0xf1,0xbb,0x3a,0x20,0x29,0x07,0x59,0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69,0x01};std::memcpy(c.key,key,16);c.key_length=16;for(char ch:std::string("LongFast"))c.hash^=std::uint8_t(ch);for(auto k:key)c.hash^=k;return c;}
static mt::Contact mtLocal(){mt::Contact c;localPublic(c.public_key);c.has_public_key=true;const auto& b=blobs["identity-v1"];CHECK(b.size()>=8);c.node=le32(b.data()+4);return c;}
static unsigned discoverMt(const mt::Identity& peer,bool trust=true,std::uint32_t id=100){
  mt::Tx tx;tx.packet_id=id;mt::Frame f;CHECK(mt::encodeNodeInfo(channel(),peer,tx,f,mt::platformCrypto(),false));chat::receive(f.bytes,f.length,1000,Epoch);
  auto v=view();unsigned index=unsigned(v.contact_count);for(unsigned i=0;i<v.contact_count;++i)if(v.contacts[i].node==peer.node){index=i;break;}
  CHECK(index<v.contact_count);if(trust&&index<v.contact_count)CHECK(chat::trustContact(index,peer.public_key));return index;
}
static void identityLifecycle(){
  scenario="identity lifecycle";reset();failAllocation=true;CHECK(!chat::begin(Protocol::MeshCore,defaults,"LongFast",storage));CHECK(!chat::active());failAllocation=false;
  REQUIRE(start(Protocol::MeshCore));const auto first=view();CHECK(!first.tx_enabled);CHECK(chat::workspaceBytes()<=16384);CHECK(!chat::begin(Protocol::MeshCore,defaults,"LongFast",storage));
  CHECK(!chat::send(-1,nullptr,"OFF",1,Epoch));CHECK(!chat::announce(1,Epoch));mt::Frame f;CHECK(!take(1,f));
  const auto identity=blobs["identity-v1"];CHECK(chat::setTransmit(true));CHECK(chat::send(-1,nullptr,"public hello",1000,Epoch));CHECK(take(1000,f));CHECK(view().history[0].delivery==chat::Delivery::Sent);CHECK(!chat::busy());
  chat::end();CHECK(!chat::active());CHECK(view().history==nullptr);REQUIRE(start(Protocol::Meshtastic));CHECK(std::memcmp(blobs["identity-v1"].data()+16,identity.data()+16,64)==0);CHECK(le32(blobs["identity-v1"].data()+8)>le32(identity.data()+8));CHECK(!view().tx_enabled);CHECK(view().history_count==0);
  chat::end();blobs["identity-v1"][20]^=1;CHECK(chat::begin(Protocol::MeshCore,defaults,"LongFast",storage));CHECK(!view().identity_ready);CHECK(!chat::setTransmit(true));chat::end();
  reset();failSave="identity-v1";CHECK(chat::begin(Protocol::MeshCore,defaults,"LongFast",storage));CHECK(!view().identity_ready);CHECK(!chat::setTransmit(true));chat::end();
  reset();REQUIRE(start(Protocol::MeshCore));chat::end();auto& saved=blobs["identity-v1"];put32(saved.data()+8,0xffffffffU);updateChecksum(saved);CHECK(chat::begin(Protocol::MeshCore,defaults,"LongFast",storage));CHECK(!view().identity_ready);CHECK(!chat::setTransmit(true));chat::end();
  reset();invalidLoad="contacts-v1";CHECK(chat::begin(Protocol::MeshCore,defaults,"LongFast",storage));CHECK(!view().identity_ready);CHECK(!chat::setTransmit(true));chat::end();
}
static void configBoundary(){
  scenario="settings boundary";reset();REQUIRE(start(Protocol::Meshtastic));chat::Settings bad=defaults;bad.meshtastic_key_length=33;CHECK(!chat::updateSettings(bad,"LongFast"));
  bad=defaults;std::memset(bad.name,'x',sizeof(bad.name));CHECK(!chat::updateSettings(bad,"LongFast"));bad=defaults;bad.meshcore_key_length=17;CHECK(!chat::updateSettings(bad,"LongFast"));
  bad=defaults;bad.hop_limit=8;CHECK(!chat::updateSettings(bad,"LongFast"));chat::end();CHECK(!chat::begin(Protocol::MeshCore,bad,"LongFast",storage));CHECK(!chat::active());
  DynamicJsonDocument document(1024);CHECK(!deserializeJson(document,"{\"messaging\":{\"name\":\"Alice\",\"tx_enabled\":true,\"enabled\":true}}"));chat::Settings parsed;CHECK(chat::parseSettings(document.as<JsonVariantConst>(),parsed));CHECK(std::strcmp(parsed.name,"Alice")==0);CHECK(chat::begin(Protocol::MeshCore,parsed,"LongFast",storage));CHECK(!view().tx_enabled);chat::end();
  document["messaging"]["meshcore_key_hex"]="1234";CHECK(!chat::parseSettings(document.as<JsonVariantConst>(),parsed));document["messaging"].remove("meshcore_key_hex");document["messaging"]["name"]="Claimed: sender";CHECK(!chat::parseSettings(document.as<JsonVariantConst>(),parsed));
}
static void meshcoreTrustAndHistory(){
  scenario="MC trust privacy persistence";reset();REQUIRE(start(Protocol::MeshCore));const auto peer=mcPeer();const auto index=discoverMc(peer,"Bob",false);CHECK(!view().contacts[index].trusted);CHECK(chat::setTransmit(true));
  CHECK(!chat::send(index,peer.public_key,"PRIVATE_MARKER",5000,Epoch));std::uint8_t changed[32];std::memcpy(changed,peer.public_key,32);changed[0]^=1;CHECK(!chat::trustContact(index,changed));CHECK(chat::trustContact(index,peer.public_key));CHECK(!chat::send(index,changed,"PRIVATE_MARKER",5000,Epoch));
  mc::Frame incoming;std::uint8_t ack[4];const auto local=mcLocal();CHECK(mc::directText(peer,local,"PRIVATE_MARKER",Epoch,0,mc::platformCrypto(),incoming,ack));CHECK(chat::setTransmit(false));chat::receive(incoming.bytes,incoming.length,6000,Epoch);CHECK(view().history_count==1);CHECK(view().history[0].authenticated);mt::Frame f;CHECK(!take(7000,f));
  chat::receive(incoming.bytes,incoming.length,7000,Epoch);CHECK(view().history_count==1);
  for(const auto& item:blobs){const auto& value=item.second;const std::string marker="PRIVATE_MARKER";CHECK(std::search(value.begin(),value.end(),marker.begin(),marker.end())==value.end());}
  chat::end();REQUIRE(start(Protocol::MeshCore));CHECK(view().contacts[index].trusted);CHECK(view().history_count==0);chat::receive(incoming.bytes,incoming.length,8000,Epoch);CHECK(view().history_count==0);CHECK(!view().tx_enabled);
  CHECK(chat::setTransmit(true));chat::receive(incoming.bytes,incoming.length,9000,Epoch);CHECK(take(10000,f));mc::Message receipt;CHECK(mc::parseReceipt(f.bytes,f.length,peer,local,ack,mc::platformCrypto(),receipt));CHECK(view().history_count==0);
  // A similarly named new key is a new untrusted contact, never a trusted name.
  const auto impersonator=mcPeer(2);const auto newIndex=discoverMc(impersonator,"Bob",false);CHECK(newIndex!=index);CHECK(!view().contacts[newIndex].trusted);CHECK(view().contacts[index].trusted);
  chat::end();
}
static void meshcoreRetryReceipts(){
  scenario="MC retry receipt correlation";reset();REQUIRE(start(Protocol::MeshCore));const auto peer=mcPeer();const auto index=discoverMc(peer);const auto local=mcLocal();CHECK(chat::setTransmit(true));
  CHECK(chat::send(index,peer.public_key,"PRIVATE_MARKER",5000,Epoch));mt::Frame sent;REQUIRE(take(5000,sent));mc::Message decoded;CHECK(mc::parseDirect(sent.bytes,sent.length,peer,local,mc::platformCrypto(),decoded));mc::Frame receipt;CHECK(mc::reply(peer,local,decoded,mc::platformCrypto(),receipt));CHECK(view().history[0].delivery==chat::Delivery::AwaitingAck);
  // Receipt reaches us after timeout queues a retry but before radio takes it.
  chat::tick(125000);CHECK(view().history[0].delivery==chat::Delivery::Queued);chat::receive(receipt.bytes,receipt.length,125001,Epoch+121);CHECK(view().history[0].delivery==chat::Delivery::Delivered);CHECK(!chat::busy());CHECK(!take(125002,sent));
  CHECK(chat::send(index,peer.public_key,"second private",130000,Epoch+130));REQUIRE(take(130000,sent));CHECK(mc::parseDirect(sent.bytes,sent.length,peer,local,mc::platformCrypto(),decoded));CHECK(mc::reply(peer,local,decoded,mc::platformCrypto(),receipt));
  chat::tick(250000);REQUIRE(take(250000,sent));chat::receive(receipt.bytes,receipt.length,250001,Epoch+251);CHECK(view().history[0].delivery==chat::Delivery::Delivered);CHECK(!chat::busy());
  // Wrong-peer receipt must not settle the pending DM even with a guessed hash.
  CHECK(chat::send(index,peer.public_key,"third private",260000,Epoch+260));REQUIRE(take(260000,sent));CHECK(mc::parseDirect(sent.bytes,sent.length,peer,local,mc::platformCrypto(),decoded));CHECK(mc::reply(peer,local,decoded,mc::platformCrypto(),receipt));receipt.bytes[3]^=1;chat::receive(receipt.bytes,receipt.length,260001,Epoch+260);CHECK(chat::busy());CHECK(view().history[0].delivery==chat::Delivery::AwaitingAck);
  // Exactly three attempts, then failure. No private-to-channel fallback.
  chat::tick(380000);REQUIRE(take(380000,sent));CHECK((sent.bytes[0]>>2&15)==2);chat::tick(500000);REQUIRE(take(500000,sent));CHECK((sent.bytes[0]>>2&15)==2);chat::tick(620000);CHECK(!chat::busy());CHECK(view().history[0].delivery==chat::Delivery::Failed);CHECK(!take(620001,sent));chat::end();
}
static void storageFailureAndReplay(){
  scenario="MC storage failure latch and replay watermark";reset();REQUIRE(start(Protocol::MeshCore));const auto peer=mcPeer();const auto index=discoverMc(peer);const auto local=mcLocal();
  CHECK(chat::setTransmit(true));failSave="contacts-v1";mc::Frame packet;std::uint8_t ack[4];CHECK(mc::directText(peer,local,"save must persist",Epoch,0,mc::platformCrypto(),packet,ack));chat::receive(packet.bytes,packet.length,5000,Epoch);CHECK(!view().tx_enabled);CHECK(!view().identity_ready);CHECK(!chat::setTransmit(true));CHECK(view().history_count==0);failSave.clear();chat::end();
  REQUIRE(start(Protocol::MeshCore));CHECK(view().contacts[index].trusted);chat::receive(packet.bytes,packet.length,6000,Epoch);CHECK(view().history_count==1);chat::end();
  // Persisted watermark remains valid if the finite replay cache has evicted
  // this timestamp. Clear only the cache in a valid synthetic stored record.
  auto& contacts=blobs["contacts-v1"];CHECK(contacts.size()>200);const std::size_t replayOffset=4+sizeof(chat::Contact)*chat::ContactCount+sizeof(std::uint32_t)*chat::ContactCount;
  CHECK(replayOffset+16*12<contacts.size());std::fill(contacts.begin()+replayOffset,contacts.begin()+replayOffset+16*12,0);updateChecksum(contacts);
  REQUIRE(start(Protocol::MeshCore));chat::receive(packet.bytes,packet.length,7000,Epoch);CHECK(view().history_count==0);chat::end();
}
static void meshtasticRuntime(){
  scenario="MT discovery privacy receipts key change";reset();REQUIRE(start(Protocol::Meshtastic));auto peer=mtPeer();const auto index=discoverMt(peer);auto local=mtLocal();CHECK(chat::setTransmit(true));
  CHECK(chat::send(index,peer.public_key,"MT_PRIVATE_MARKER",5000,Epoch));mt::Frame sent;REQUIRE(take(5000,sent));mt::Parsed decoded;CHECK(mt::decodePrivate(sent.bytes,sent.length,peer,local,decoded,mt::platformCrypto()));CHECK(decoded.authenticated);const auto id=decoded.header.id;
  mt::Tx options;options.packet_id=1001;options.extra_nonce=0x11223344;mt::Frame receipt;CHECK(mt::encodeReceipt(channel(),peer,local.node,options,id+1,0,receipt,mt::platformCrypto(),&local));chat::receive(receipt.bytes,receipt.length,5001,Epoch);CHECK(chat::busy());
  CHECK(mt::encodeReceipt(channel(),peer,local.node,options,id,0,receipt,mt::platformCrypto(),&local));chat::receive(receipt.bytes,receipt.length,5002,Epoch);CHECK(!chat::busy());CHECK(view().history[0].delivery==chat::Delivery::Delivered);
  options.packet_id=1002;options.want_ack=true;CHECK(mt::encodePrivateText(peer,local,options,"MT_INCOMING_PRIVATE",sent,mt::platformCrypto()));CHECK(chat::setTransmit(false));chat::receive(sent.bytes,sent.length,6000,Epoch);const auto count=view().history_count;CHECK(count==2);CHECK(view().history[0].direct&&view().history[0].authenticated);CHECK(!take(7000,receipt));chat::receive(sent.bytes,sent.length,7000,Epoch);CHECK(view().history_count==count);
  mt::Frame tampered=sent;tampered.bytes[tampered.length-5]^=1;chat::receive(tampered.bytes,tampered.length,8000,Epoch);CHECK(view().history_count==count); // never decode as channel text after failed CCM
  for(const auto& item:blobs){const std::string marker="MT_INCOMING_PRIVATE";CHECK(std::search(item.second.begin(),item.second.end(),marker.begin(),marker.end())==item.second.end());}
  // Same MT node with a changed public key loses trust; stale UI fingerprint
  // cannot approve the replacement or accidentally send using its new key.
  auto replacement=mtPeer(2);replacement.node=peer.node;discoverMt(replacement,false,1003);CHECK(view().contacts[index].key_changed);CHECK(!view().contacts[index].trusted);CHECK(!chat::trustContact(index,peer.public_key));CHECK(chat::setTransmit(true));CHECK(!chat::send(index,peer.public_key,"no fallback",10000,Epoch));CHECK(!chat::send(index,replacement.public_key,"no fallback",10000,Epoch));CHECK(chat::trustContact(index,replacement.public_key));CHECK(chat::send(index,replacement.public_key,"new pinned peer",10000,Epoch));CHECK(chat::setTransmit(false));CHECK(!chat::busy());CHECK(view().history[0].delivery==chat::Delivery::Cancelled);chat::end();
}
static void clockAndCancellation(){
  scenario="timestamp persistence TX cancellation";reset();REQUIRE(start(Protocol::MeshCore));CHECK(chat::setTransmit(true));CHECK(!chat::send(-1,nullptr,"no clock",1000,0));CHECK(view().history_count==0);CHECK(chat::send(-1,nullptr,"first",5000,Epoch));mt::Frame first;REQUIRE(take(5000,first));mc::Message decoded;CHECK(mc::parseGroup(first.bytes,first.length,mc::publicGroupKey(),16,mc::platformCrypto(),decoded));const auto timestamp=decoded.timestamp;
  CHECK(chat::send(-1,nullptr,"second",10000,Epoch));REQUIRE(take(10000,first));CHECK(mc::parseGroup(first.bytes,first.length,mc::publicGroupKey(),16,mc::platformCrypto(),decoded));CHECK(decoded.timestamp>timestamp);chat::end();REQUIRE(start(Protocol::MeshCore));CHECK(chat::setTransmit(true));CHECK(chat::send(-1,nullptr,"after restart",15000,Epoch));REQUIRE(take(15000,first));CHECK(mc::parseGroup(first.bytes,first.length,mc::publicGroupKey(),16,mc::platformCrypto(),decoded));CHECK(decoded.timestamp>timestamp+1);
  CHECK(chat::send(-1,nullptr,"cancel",20000,Epoch));CHECK(chat::setTransmit(false));CHECK(!chat::busy());CHECK(view().history[0].delivery==chat::Delivery::Cancelled);CHECK(!take(21000,first));CHECK(chat::setTransmit(true));CHECK(chat::send(-1,nullptr,"radio error",25000,Epoch));CHECK(take(25000,first,false));CHECK(view().history[0].delivery==chat::Delivery::Failed);CHECK(!chat::busy());chat::radioFailure("synthetic error");CHECK(!view().tx_enabled);chat::end();
}
static void meshtasticRetriesAndUnverifiedReceipts(){
  scenario="MT retry rollover and receipt trust";reset();REQUIRE(start(Protocol::Meshtastic));const auto peer=mtPeer();const auto index=discoverMt(peer);const auto local=mtLocal();CHECK(chat::setTransmit(true));
  constexpr std::uint32_t nearWrap=0xffff0000U;CHECK(chat::send(index,peer.public_key,"retry identical",nearWrap,Epoch));mt::Frame first,retry;REQUIRE(take(nearWrap,first));mt::Parsed decoded;CHECK(mt::decodePrivate(first.bytes,first.length,peer,local,decoded,mt::platformCrypto()));
  const std::uint32_t deadline=nearWrap+120000U;chat::tick(deadline-1);CHECK(!take(deadline-1,retry));chat::tick(deadline);REQUIRE(take(deadline,retry));CHECK(first.length==retry.length);CHECK(!std::memcmp(first.bytes,retry.bytes,first.length));
  // Plain channel ACK is correlation only, visibly unverified.
  mt::Tx options;options.packet_id=2001;mt::Frame receipt;CHECK(mt::encodeReceipt(channel(),peer,local.node,options,decoded.header.id,0,receipt,mt::platformCrypto(),nullptr));chat::receive(receipt.bytes,receipt.length,deadline+1,Epoch);CHECK(!chat::busy());CHECK(view().history[0].delivery==chat::Delivery::ReceiptUnverified);
  CHECK(chat::send(index,peer.public_key,"late queued retry",deadline+5000,Epoch));REQUIRE(take(deadline+5000,first));CHECK(mt::decodePrivate(first.bytes,first.length,peer,local,decoded,mt::platformCrypto()));options.packet_id=2002;CHECK(mt::encodeReceipt(channel(),peer,local.node,options,decoded.header.id,0,receipt,mt::platformCrypto(),&local));
  chat::tick(deadline+125000);CHECK(view().history[0].delivery==chat::Delivery::Queued);chat::receive(receipt.bytes,receipt.length,deadline+125001,Epoch);CHECK(!chat::busy());CHECK(view().history[0].delivery==chat::Delivery::Delivered);CHECK(!take(deadline+125002,retry));chat::end();
}
static void privateChannelsAndFailures(){
  scenario="configured channel isolation and identity save failure";reset();for(unsigned i=0;i<32;++i){defaults.meshcore_key[i]=std::uint8_t(i);defaults.meshtastic_key[i]=std::uint8_t(i+32);}defaults.meshcore_key_length=defaults.meshtastic_key_length=32;
  REQUIRE(start(Protocol::MeshCore));CHECK(chat::setTransmit(true));CHECK(chat::send(-1,nullptr,"custom group",5000,Epoch));mt::Frame frame;REQUIRE(take(5000,frame));mc::Message group;CHECK(mc::parseGroup(frame.bytes,frame.length,defaults.meshcore_key,32,mc::platformCrypto(),group));CHECK(!mc::parseGroup(frame.bytes,frame.length,mc::publicGroupKey(),16,mc::platformCrypto(),group));
  failSave="identity-v1";CHECK(!chat::send(-1,nullptr,"must not send",10000,Epoch));CHECK(!view().identity_ready);CHECK(!view().tx_enabled);CHECK(!chat::setTransmit(true));CHECK(!take(10000,frame));failSave.clear();chat::end();
  REQUIRE(start(Protocol::Meshtastic));CHECK(chat::setTransmit(true));CHECK(chat::send(-1,nullptr,"custom MT group",15000,Epoch));REQUIRE(take(15000,frame));mt::Parsed parsed;CHECK(!mt::decodeChannel(frame.bytes,frame.length,channel(),parsed,mt::platformCrypto()));mt::Channel configured;configured.key_length=32;std::memcpy(configured.key,defaults.meshtastic_key,32);for(char ch:std::string("LongFast"))configured.hash^=std::uint8_t(ch);for(auto k:configured.key)configured.hash^=k;CHECK(mt::decodeChannel(frame.bytes,frame.length,configured,parsed,mt::platformCrypto()));CHECK(std::strcmp(parsed.text,"custom MT group")==0);chat::end();
}
static void meshtasticZeroHashChannel(){
  scenario="MT valid zero-hash channel with no private text fallback";reset();defaults.meshtastic_key_length=32;
  for(char ch:std::string("LongFast"))defaults.meshtastic_key[0]^=std::uint8_t(ch);
  REQUIRE(start(Protocol::Meshtastic));const auto peer=mtPeer();const auto local=mtLocal();mt::Channel selected;selected.key_length=32;std::memcpy(selected.key,defaults.meshtastic_key,32);CHECK(selected.hash==0);
  mt::Tx options;options.packet_id=301;mt::Frame frame;
  // Valid unicast NodeInfo on hash zero is not a private-text downgrade.
  CHECK(mt::encodeNodeInfo(selected,peer,options,frame,mt::platformCrypto(),false,local.node));chat::receive(frame.bytes,frame.length,1000,Epoch);CHECK(view().contact_count==1);REQUIRE(view().contact_count==1);CHECK(chat::trustContact(0,peer.public_key));CHECK(chat::setTransmit(true));
  CHECK(chat::send(0,peer.public_key,"hash zero private",5000,Epoch));REQUIRE(take(5000,frame));mt::Parsed decoded;CHECK(mt::decodePrivate(frame.bytes,frame.length,peer,local,decoded,mt::platformCrypto()));options.packet_id=302;
  CHECK(mt::encodeReceipt(selected,peer,local.node,options,decoded.header.id,0,frame,mt::platformCrypto(),&local));chat::receive(frame.bytes,frame.length,5001,Epoch);CHECK(!chat::busy());CHECK(view().history[0].delivery==chat::Delivery::Delivered);
  const auto count=view().history_count;options.packet_id=303;CHECK(mt::encodeChannelText(selected,peer,options,"MUST_NOT_DOWNGRADE",frame,mt::platformCrypto()));put32(frame.bytes,local.node);chat::receive(frame.bytes,frame.length,6000,Epoch);CHECK(view().history_count==count);chat::end();
}
static void meshcoreClockGate(){
  scenario="MC missing UTC cannot poison persisted watermark";reset();REQUIRE(start(Protocol::MeshCore));const auto peer=mcPeer();mc::Frame frame;
  // Discovery and ordinary channel RX stay usable without a clock.
  CHECK(mc::advert(peer,"Bob",Epoch,mc::platformCrypto(),frame));chat::receive(frame.bytes,frame.length,1000,0);CHECK(view().contact_count==1);REQUIRE(view().contact_count==1);CHECK(chat::trustContact(0,peer.public_key));
  CHECK(mc::groupText(mc::publicGroupKey(),16,"Bob","group without UTC",Epoch,0,mc::platformCrypto(),frame));chat::receive(frame.bytes,frame.length,2000,0);CHECK(view().history_count==1);CHECK(!view().history[0].direct);
  const auto saved=blobs["contacts-v1"];const auto history=view().history_count;const auto local=mcLocal();std::uint8_t ack[4];CHECK(chat::setTransmit(true));mt::Frame response;
  const unsigned writes=saveCalls;
  CHECK(mc::directText(peer,local,"future timestamp",0xffffffffU,0,mc::platformCrypto(),frame,ack));chat::receive(frame.bytes,frame.length,3000,0);CHECK(view().history_count==history);CHECK(!take(4000,response));CHECK(blobs["contacts-v1"]==saved);CHECK(saveCalls==writes);
  CHECK(std::strstr(view().status,"UTC")!=nullptr);
  CHECK(mc::directText(peer,local,"normal but no clock",Epoch,0,mc::platformCrypto(),frame,ack));chat::receive(frame.bytes,frame.length,5000,0);CHECK(view().history_count==history);CHECK(!take(6000,response));CHECK(blobs["contacts-v1"]==saved);
  chat::receive(frame.bytes,frame.length,7000,1);CHECK(view().history_count==history);CHECK(!take(8000,response));CHECK(blobs["contacts-v1"]==saved);
  CHECK(mc::directText(peer,local,"pre-2024 packet",1704067199U,0,mc::platformCrypto(),frame,ack));chat::receive(frame.bytes,frame.length,9000,Epoch);CHECK(view().history_count==history);CHECK(!take(10000,response));CHECK(blobs["contacts-v1"]==saved);
  CHECK(mc::directText(peer,local,"valid after UTC",Epoch,0,mc::platformCrypto(),frame,ack));chat::receive(frame.bytes,frame.length,11000,Epoch);CHECK(view().history_count==history+1);CHECK(view().history[0].direct);CHECK(take(12000,response));CHECK(blobs["contacts-v1"]!=saved);
  chat::end();REQUIRE(start(Protocol::MeshCore));CHECK(view().history_count==0);chat::receive(frame.bytes,frame.length,13000,Epoch);CHECK(view().history_count==0);chat::end();
}
static void borrowedFrameFailureLifetime(){
  scenario="borrowed frame survives fatal persistence until scanner abort";reset();REQUIRE(start(Protocol::MeshCore));const auto peer=mcPeer();const auto index=discoverMc(peer);const auto local=mcLocal();CHECK(chat::setTransmit(true));
  CHECK(chat::send(index,peer.public_key,"borrowed outgoing",5000,Epoch));const std::uint8_t* borrowed=nullptr;std::size_t length=0;CHECK(chat::nextFrame(5000,borrowed,length));REQUIRE(borrowed!=nullptr);const std::vector<std::uint8_t> copy(borrowed,borrowed+length);
  mc::Frame incoming;std::uint8_t ack[4];CHECK(mc::directText(peer,local,"incoming save fails",Epoch,0,mc::platformCrypto(),incoming,ack));failSave="contacts-v1";chat::receive(incoming.bytes,incoming.length,5001,Epoch);
  CHECK(!view().identity_ready&&!view().tx_enabled);CHECK(std::memcmp(borrowed,copy.data(),length)==0);const std::uint8_t* another=nullptr;std::size_t otherLength=0;CHECK(!chat::nextFrame(5002,another,otherLength));CHECK(!chat::setTransmit(true));
  // Caller must abort CAD/radio ownership and release its pointer FIRST.
  borrowed=nullptr;length=0;chat::transmitted(false,5003);chat::radioFailure("aborted after persistence failure");CHECK(!chat::busy());CHECK(view().history[0].delivery==chat::Delivery::Failed);chat::end();failSave.clear();
  REQUIRE(start(Protocol::MeshCore));CHECK(chat::setTransmit(true));chat::receive(incoming.bytes,incoming.length,6000,Epoch);CHECK(chat::nextFrame(7000,borrowed,length));REQUIRE(borrowed!=nullptr);const std::vector<std::uint8_t> ackCopy(borrowed,borrowed+length);
  CHECK(mc::directText(peer,local,"second save fails",Epoch+1,0,mc::platformCrypto(),incoming,ack));failSave="contacts-v1";chat::receive(incoming.bytes,incoming.length,7001,Epoch+1);CHECK(!view().identity_ready&&!view().tx_enabled);CHECK(std::memcmp(borrowed,ackCopy.data(),length)==0);
  borrowed=nullptr;length=0;chat::transmitted(false,7002);chat::radioFailure("reply aborted after persistence failure");CHECK(!chat::busy());chat::end();failSave.clear();
}
static void diagnosticEvidenceSurvivesExit(){
  scenario="numeric diagnostics distinguish rejected/queued/completed work";reset();
  const auto before=chat::diagnostics();REQUIRE(start(Protocol::Meshtastic));
  CHECK(!chat::send(-1,nullptr,"blocked while TX off",1000,Epoch));
  CHECK(!chat::announce(1000,Epoch));
  CHECK(chat::setTransmit(true));
  CHECK(chat::send(-1,nullptr,"radio failure",2000,Epoch));mt::Frame frame;
  REQUIRE(take(2000,frame,false));
  CHECK(chat::send(-1,nullptr,"completed locally",8000,Epoch));REQUIRE(take(8000,frame));
  CHECK(chat::announce(14000,Epoch));REQUIRE(take(14000,frame));
  const auto peer=mtPeer();discoverMt(peer,false);
  mt::Tx options;options.packet_id=987;
  CHECK(mt::encodeChannelText(channel(),peer,options,"received once",frame,mt::platformCrypto()));
  chat::receive(frame.bytes,frame.length,15000,Epoch);
  chat::receive(frame.bytes,frame.length,15001,Epoch); // duplicate frame, not another inbox record
  const std::uint8_t malformed[]={1,2,3};chat::receive(malformed,sizeof(malformed),15002,Epoch);
  chat::radioFailure("synthetic radio failure");chat::end();
  const auto after=chat::diagnostics();
  CHECK(after.starts-before.starts==1);CHECK(after.stops-before.stops==1);
  CHECK(after.tx_enable_requests-before.tx_enable_requests==1);CHECK(after.tx_enabled-before.tx_enabled==1);
  CHECK(after.send_requests-before.send_requests==3);CHECK(after.send_queued-before.send_queued==2);
  CHECK(after.announce_requests-before.announce_requests==2);CHECK(after.announce_queued-before.announce_queued==1);
  CHECK(after.tx_ok-before.tx_ok==2);CHECK(after.tx_fail-before.tx_fail==1);
  CHECK(after.rx_frames-before.rx_frames==4);CHECK(after.rx_nodes-before.rx_nodes==1);CHECK(after.rx_text-before.rx_text==1);
  CHECK(after.radio_failures-before.radio_failures==1);CHECK(view().history_count==0);
  // A new UI session must not erase the evidence of a prior failed send.
  REQUIRE(start(Protocol::Meshtastic));CHECK(!view().tx_enabled);
  CHECK(chat::diagnostics().send_requests==after.send_requests);CHECK(chat::diagnostics().rx_text==after.rx_text);
  CHECK(chat::diagnostics().tx_fail==after.tx_fail);chat::end();
}
static mc::Frame groupRoute(const std::uint8_t* bytes,std::size_t length){
  mc::Frame routed;routed.bytes[0]=(5<<2)|0; // flood with mutable transport codes
  routed.bytes[1]=0x12;routed.bytes[2]=0x34;routed.bytes[3]=0x56;routed.bytes[4]=0x78;
  routed.bytes[5]=3;routed.bytes[6]=0xaa;routed.bytes[7]=0xbb;routed.bytes[8]=0xcc;
  CHECK(length>=2&&bytes[1]==0&&length+7<=sizeof(routed.bytes));
  std::memcpy(routed.bytes+9,bytes+2,length-2);routed.length=length+7;return routed;
}
static void meshcoreDistinctGroupMessages(){
  scenario="MC same claimed name and timestamp are not a sender ID";reset();REQUIRE(start(Protocol::MeshCore));
  CHECK(chat::workspaceBytes()==3464);const auto initialWrites=saveCalls;
  mc::Frame first,second;CHECK(mc::groupText(mc::publicGroupKey(),16,"LORA Tunnel","peer one",Epoch,0,mc::platformCrypto(),first));
  CHECK(mc::groupText(mc::publicGroupKey(),16,"LORA Tunnel","peer two",Epoch,0,mc::platformCrypto(),second));
  chat::receive(first.bytes,first.length,1000,Epoch);CHECK(view().history_count==1);
  chat::receive(second.bytes,second.length,2000,Epoch);CHECK(view().history_count==2);CHECK(!std::strcmp(view().history[0].text,"peer two"));
  CHECK(!view().history[0].authenticated&&!view().history[0].direct);CHECK(view().contact_count==0);
  const auto received=chat::diagnostics().rx_text;
  chat::receive(first.bytes,first.length,3000,Epoch);auto routed=groupRoute(first.bytes,first.length);
  chat::receive(routed.bytes,routed.length,4000,Epoch);CHECK(view().history_count==2);CHECK(chat::diagnostics().rx_text==received);
  CHECK(saveCalls==initialWrites); // group dedupe is bounded RAM state, never private NVS history
  // Invalid routing and bad ciphertext must not poison the validated cache.
  mc::Frame fresh;CHECK(mc::groupText(mc::publicGroupKey(),16,"LORA Tunnel","valid after malformed",Epoch,0,mc::platformCrypto(),fresh));
  auto malformed=fresh;malformed.bytes[1]=0xc0;chat::receive(malformed.bytes,malformed.length,5000,Epoch);
  malformed=fresh;malformed.bytes[malformed.length-1]^=1;chat::receive(malformed.bytes,malformed.length,6000,Epoch);
  CHECK(view().history_count==2);chat::receive(fresh.bytes,fresh.length,7000,Epoch);CHECK(view().history_count==3);
  CHECK(!view().history[0].authenticated);CHECK(saveCalls==initialWrites);chat::end();
}
static void meshcoreOwnGroupEchoes(){
  scenario="MC own echo matches exact successful payload only";reset();REQUIRE(start(Protocol::MeshCore));CHECK(chat::setTransmit(true));
  CHECK(chat::send(-1,nullptr,"first local text",5000,Epoch));mt::Frame first;REQUIRE(take(5000,first));CHECK(view().history_count==1);
  CHECK(chat::send(-1,nullptr,"second local text",10000,Epoch+1));mt::Frame second;REQUIRE(take(10000,second));CHECK(view().history_count==2);
  // A same-named peer is independent even when its clock is equal or behind.
  mc::Frame incoming;CHECK(mc::groupText(mc::publicGroupKey(),16,"LORA Tunnel","other same second",Epoch,0,mc::platformCrypto(),incoming));
  chat::receive(incoming.bytes,incoming.length,11000,Epoch+1);CHECK(view().history_count==3);
  CHECK(mc::groupText(mc::publicGroupKey(),16,"LORA Tunnel","other earlier clock",Epoch-1,0,mc::platformCrypto(),incoming));
  chat::receive(incoming.bytes,incoming.length,12000,Epoch+1);CHECK(view().history_count==4);CHECK(!view().history[0].authenticated);
  const auto received=chat::diagnostics().rx_text;auto routed=groupRoute(first.bytes,first.length);
  chat::receive(routed.bytes,routed.length,13000,Epoch+1);chat::receive(second.bytes,second.length,14000,Epoch+1);
  CHECK(view().history_count==4);CHECK(chat::diagnostics().rx_text==received);
  // A successfully sent advert must not suppress later same-name group traffic.
  CHECK(chat::announce(15000,Epoch+2));mt::Frame advert;REQUIRE(take(15000,advert));
  CHECK(mc::groupText(mc::publicGroupKey(),16,"LORA Tunnel","peer after my advert",Epoch+2,0,mc::platformCrypto(),incoming));
  chat::receive(incoming.bytes,incoming.length,16000,Epoch+2);CHECK(view().history_count==5);
  // Echo suppression outlives the six visible history records, while still
  // using only the fixed replay ring. Evict the first sent record from history.
  for(unsigned i=0;i<3;++i){
    char text[32];std::snprintf(text,sizeof(text),"later peer message %u",i);
    CHECK(mc::groupText(mc::publicGroupKey(),16,"LORA Tunnel",text,Epoch+3+i,0,mc::platformCrypto(),incoming));
    chat::receive(incoming.bytes,incoming.length,17000+i,Epoch+5);
  }
  CHECK(view().history_count==chat::HistoryCount);const auto afterLater=chat::diagnostics().rx_text;
  chat::receive(routed.bytes,routed.length,18000,Epoch+5);CHECK(chat::diagnostics().rx_text==afterLater);
  chat::end();
}
static void meshcoreGroupCacheRollover(){
  scenario="MC group replay ring stays bounded across rollover";reset();REQUIRE(start(Protocol::MeshCore));
  const auto writes=saveCalls;mc::Frame first,last;
  for(unsigned i=0;i<40;++i){
    char text[32];std::snprintf(text,sizeof(text),"distinct group %u",i);
    CHECK(mc::groupText(mc::publicGroupKey(),16,"same claimed name",text,Epoch,0,mc::platformCrypto(),last));
    if(!i)first=last;chat::receive(last.bytes,last.length,1000+i,Epoch);
  }
  CHECK(view().history_count==chat::HistoryCount);const auto received=chat::diagnostics().rx_text;
  auto routed=groupRoute(last.bytes,last.length);chat::receive(routed.bytes,routed.length,2000,Epoch);
  CHECK(chat::diagnostics().rx_text==received);CHECK(saveCalls==writes);CHECK(chat::workspaceBytes()==3464);
  // Old entries can reappear after finite-cache eviction; this is duplicate
  // suppression, not authenticated or persistent anti-replay protection.
  chat::receive(first.bytes,first.length,3000,Epoch);CHECK(chat::diagnostics().rx_text==received+1);
  CHECK(!view().history[0].authenticated);chat::end();
}
static void meshcoreFailedAndCancelledGroups(){
  scenario="MC failed or cancelled sends never seed own-echo cache";reset();REQUIRE(start(Protocol::MeshCore));CHECK(chat::setTransmit(true));
  CHECK(chat::send(-1,nullptr,"failed local frame",5000,Epoch));mt::Frame failed;REQUIRE(take(5000,failed,false));
  CHECK(view().history_count==1&&view().history[0].delivery==chat::Delivery::Failed);
  chat::receive(failed.bytes,failed.length,6000,Epoch);CHECK(view().history_count==2&&!view().history[0].outgoing);CHECK(!view().history[0].authenticated);
  CHECK(chat::send(-1,nullptr,"cancelled local frame",10000,Epoch+1));CHECK(chat::setTransmit(false));
  mc::Frame incoming;CHECK(mc::groupText(mc::publicGroupKey(),16,"LORA Tunnel","cancelled local frame",Epoch+1,0,mc::platformCrypto(),incoming));
  chat::receive(incoming.bytes,incoming.length,11000,Epoch+1);CHECK(view().history_count==4&&!view().history[0].outgoing);
  CHECK(!view().history[0].authenticated);chat::end();
}
int main(){
  identityLifecycle();configBoundary();meshcoreTrustAndHistory();meshcoreRetryReceipts();storageFailureAndReplay();meshtasticRuntime();clockAndCancellation();meshtasticRetriesAndUnverifiedReceipts();privateChannelsAndFailures();meshtasticZeroHashChannel();meshcoreClockGate();borrowedFrameFailureLifetime();diagnosticEvidenceSurvivesExit();meshcoreDistinctGroupMessages();meshcoreOwnGroupEchoes();meshcoreGroupCacheRollover();meshcoreFailedAndCancelledGroups();chat::end();
  std::printf("Messaging actual-engine host: %u checks, %u failures; workspace=%zu bytes; zeroized releases=%u\n",checks,failures,chat::workspaceBytes(),released);
  return failures?1:0;
}
