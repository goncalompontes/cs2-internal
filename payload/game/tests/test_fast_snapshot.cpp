#include "cs2/game/state_reader.hpp"
#include "cs2/game/memory.hpp"
#include "offsets.hpp"
#include <algorithm>
#include <array>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
void check(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
template<class T>void put(void* p,std::size_t at,T value){std::memcpy(static_cast<std::byte*>(p)+at,&value,sizeof(value));}
}
namespace cs2::game {
struct StateReaderTestAccess {
 static StateReader create(std::uintptr_t client,std::uintptr_t pages){
  StateReader reader;reader.client_base_=client;reader.entities_=EntitySystem(pages);return reader;
 }
};
}
int main(){
 using namespace cs2;
 const auto* local=sdk::find_offset("local_player_controllers");const auto* rules=sdk::find_offset("game_rules");
 check(local&&rules,"calibrated global offsets");
 std::vector<std::byte> image(static_cast<std::size_t>(std::max(local->rva,rules->rva))+16);
 std::array<std::byte,0x1000> a{},b{},other{};
 std::array<std::byte,512*0x70> page{};std::array<std::uintptr_t,64> pages{};
 pages[0]=reinterpret_cast<std::uintptr_t>(page.data());
 char name1[]="cs_player_controller",name2[]="cs_player_controller",name3[]="env_sprite";
 auto identity=[&](int index,void* object,char* name,std::uintptr_t table){
  put(page.data(),index*0x70,reinterpret_cast<std::uintptr_t>(object));
  put(page.data(),index*0x70+0x20,reinterpret_cast<std::uintptr_t>(name));
  put(object,0,table);put(object,0x10,reinterpret_cast<std::uintptr_t>(page.data()+index*0x70));
 };
 identity(1,a.data(),name1,0x1234);identity(2,b.data(),name2,0x1234);identity(3,other.data(),name3,0x5678);
 put(image.data(),local->rva,reinterpret_cast<std::uintptr_t>(a.data()));
 auto reader=game::StateReaderTestAccess::create(reinterpret_cast<std::uintptr_t>(image.data()),reinterpret_cast<std::uintptr_t>(pages.data()));
 auto full=reader.snapshot();auto fast=reader.snapshot(true);
 check(full.players.size()==2&&fast.players.size()==2,"fast and full discover the same controllers");
 check(fast.local&&fast.local->controller==reinterpret_cast<std::uintptr_t>(a.data()),"local controller retained");
 check(fast.players[1].controller==reinterpret_cast<std::uintptr_t>(b.data()),"equal class names at different pointers supported");
 put(page.data(),2*0x70,static_cast<std::uintptr_t>(0));
 check(reader.snapshot(true).players.size()==1,"removed controller not retained by type-name cache");
 int value=0;check(!game::read_bytes(1,&value,sizeof(value)),"invalid pointers rejected safely");
 char raw[4]={'n','a','m','e'};
 check(game::read_inline_string(reinterpret_cast<std::uintptr_t>(raw),4)=="name","bounded non-NUL names supported");
 std::puts("PASS: batched fast/full snapshot equivalence, type-name cache, removal, safe memory, bounded names");
}
