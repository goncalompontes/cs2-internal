#include "cs2/control/native_command.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
using namespace cs2;
namespace {
void check(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
template<class T>void put(void* p,std::size_t at,T value){std::memcpy(static_cast<std::byte*>(p)+at,&value,sizeof(value));}
template<class T>T get(const void* p,std::size_t at){T value{};std::memcpy(&value,static_cast<const std::byte*>(p)+at,sizeof(value));return value;}
alignas(8) std::array<std::byte,0x5920> command_ring{};
alignas(8) std::array<std::byte,0xa0> command{};
alignas(8) std::array<std::byte,0xb0> base{};
alignas(8) std::array<std::byte,0x30> buttons{};
alignas(8) std::array<std::byte,0x28> angles{};
control::MoveCrcBytes serialized{};
void* expected_arena=nullptr;
game::Vec3 camera{10,20,0};
void* get_ring(void* controller){check(controller==reinterpret_cast<void*>(7),"live command controller");return command_ring.data();}
void* get_command(void* controller,std::uint32_t sequence){check(controller==reinterpret_cast<void*>(7)&&sequence==123,"current ring sequence");return command.data();}
void assign(std::uintptr_t* field,const control::EngineStringView* text,void* arena){
 check(arena==expected_arena,"protobuf arena preserved");check(text->size<=64,"bounded canonical bytes");
 const void* bytes=text->storage.data();if(text->capacity>=16)std::memcpy(&bytes,text->storage.data(),sizeof(bytes));
 serialized.size=text->size;std::memcpy(serialized.data.data(),bytes,text->size);*field=0x1232;
}
void* allocate(void* arena){check(arena==expected_arena,"button PB allocation arena");buttons.fill(std::byte{});return buttons.data();}
game::Vec3* get_view(void*,int slot){check(slot==0,"view slot");return &camera;}
void set_view(void*,int slot,const game::Vec3* value){check(slot==0,"restore view slot");camera=*value;}
}
int main(){
 const std::uint64_t zero[3]{};const float no_angles[3]{};
 check(control::encode_move_crc(zero,no_angles).size==0,"canonical zero fields omitted");
 const std::uint64_t one[3]={1,0,0};const float yaw[3]={0,90,0};
 auto bytes=control::encode_move_crc(one,yaw);
 const std::array<unsigned char,18> expected={0x1a,9,9,1,0,0,0,0,0,0,0,0x22,5,0x15,0,0,0xb4,0x42};
 check(bytes.size==expected.size()&&std::memcmp(bytes.data.data(),expected.data(),expected.size())==0,
       "independent protobuf wire fixture for attack plus yaw");
 const std::uint64_t all[3]={1,2,4};const float every_angle[3]={1,2,3};
 check(control::encode_move_crc(all,every_angle).size==46,"maximum canonical encoding is 46 bytes");
 const float nan[3]={std::numeric_limits<float>::quiet_NaN(),0,0};
 check(control::encode_move_crc(all,nan).size==0,"invalid view angles rejected");
 control::NativeCommandBridge bridge;bridge.bind({get_ring,get_command,assign,allocate,get_view,set_view,reinterpret_cast<void*>(1)});
 put(command_ring.data(),0x5910,123u);
 check(bridge.current(reinterpret_cast<void*>(7))==command.data(),"current command uses direct base lookup");
 put(command.data(),0x40,reinterpret_cast<std::uintptr_t>(base.data()));
 put(base.data(),0x40,reinterpret_cast<std::uintptr_t>(angles.data()));
 put(angles.data(),0x1c,90.0F);
 put(command.data(),0x60,std::uint64_t{1});put(command.data(),0x68,std::uint64_t{2});put(command.data(),0x70,std::uint64_t{4});
 check(bridge.finalize(command.data()),"missing button submessage is allocated and finalized");
 check(get<std::uintptr_t>(base.data(),0x38)==reinterpret_cast<std::uintptr_t>(buttons.data()),"button PB attached");
 check(get<std::uint64_t>(buttons.data(),0x18)==1&&get<std::uint64_t>(buttons.data(),0x20)==2&&get<std::uint64_t>(buttons.data(),0x28)==4,"all three button words synchronized");
 check((get<unsigned>(base.data(),0x10)&3)==3&&get<std::uintptr_t>(base.data(),0x30)==0x1232,"CRC/buttons presence and assignment");
 const float canonical_angles[3]={0,90,0};const auto canonical=control::encode_move_crc(all,canonical_angles);
 check(serialized.size==canonical.size&&std::memcmp(serialized.data.data(),canonical.data.data(),canonical.size)==0,"finalization encodes post-mutation values");
 std::uintptr_t owner=0x4000;expected_arena=reinterpret_cast<void*>(owner);
 put(base.data(),8,reinterpret_cast<std::uintptr_t>(&owner)|std::uintptr_t{1});
 check(bridge.finalize(command.data()),"indirect tagged arena decoded");
 game::Vec3 saved{};check(bridge.view(nullptr,saved),"visible view captured");camera={80,130,0};bridge.restore_view(nullptr,saved);
 check(camera.x==10&&camera.y==20,"visible camera restored separately from command aim");
 std::puts("PASS: direct command lookup, protobuf wire bytes, tagged arena, button allocation/sync, CRC commit, camera restoration");
}
