#include "cs2/control/input.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#define NOMINMAX
#include <windows.h>
namespace {
void check(bool value, const char* why) { if (!value) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); } }
template<class T> void put(void* p, std::size_t at, T v) { std::memcpy(static_cast<std::byte*>(p)+at, &v, sizeof(v)); }
template<class T> T get(const void* p, std::size_t at) { T v{};std::memcpy(&v,static_cast<const std::byte*>(p)+at,sizeof(v));return v; }
struct Fixture {
 alignas(8) std::array<std::byte,0xa0> cmd{};
 alignas(8) std::array<std::byte,0xb0> base{};
 alignas(8) std::array<std::byte,0x40> buttons{}, base_angles{};
 alignas(8) std::array<std::byte,8+64*8> rep{};
 std::array<std::array<std::byte,0x80>,64> entries{},angles{};
 Fixture(unsigned size=3) {
  using namespace cs2::control;
  put(cmd.data(),k_cmd_pb_offset+k_pb_base_offset,reinterpret_cast<std::uintptr_t>(base.data()));
  put(base.data(),k_base_viewangles_offset,reinterpret_cast<std::uintptr_t>(base_angles.data()));
  put(base.data(),k_base_buttons_pb_offset,reinterpret_cast<std::uintptr_t>(buttons.data()));
  put(cmd.data(),k_cmd_pb_offset+k_pb_history_size_offset,size);
  put(cmd.data(),k_cmd_pb_offset+k_pb_history_rep_offset,reinterpret_cast<std::uintptr_t>(rep.data()));
  put(rep.data(),0,static_cast<unsigned>(64));
  for(unsigned i=0;i<64;++i) {
   put(rep.data(),8+i*8,reinterpret_cast<std::uintptr_t>(entries[i].data()));
   put(entries[i].data(),k_history_entry_angles_offset,reinterpret_cast<std::uintptr_t>(angles[i].data()));
  }
  reset();
 }
 void reset() {
  using namespace cs2::control;
  put(cmd.data(),k_cmd_buttons_offset,button::k_reload);
  put(cmd.data(),k_cmd_buttons_offset+8,button::k_reload);
  put(cmd.data(),k_cmd_buttons_offset+16,button::k_reload);
  put(buttons.data(),k_buttonstate_values_offset,button::k_reload);
  put(buttons.data(),k_buttonstate_values_offset+8,button::k_reload);
  put(buttons.data(),k_buttonstate_values_offset+16,button::k_reload);
  put(cmd.data(),k_cmd_pb_offset+0x3c,-42);
 }
};
}
namespace cs2::control {
struct RageInputTestAccess {
 static void apply(Input& input, void* cmd) { input.apply(cmd); }
 static void post_build(Input& input, void* cmd, NativeCommandApi api) { input.native_command_.bind(api);input.process_built_command(cmd); }
 static void stale(Input& input) { input.rage_updated_ms_.store(0); }
 static bool stop(Input& input, void* cmd, void* base) {
   return input.autostop_tick(static_cast<std::uint8_t*>(cmd),static_cast<std::uint8_t*>(base));
 }
 static void stopping(Input& input, std::uintptr_t pawn) {
   input.flags_offset_=0x10;input.velocity_offset_=0x20;input.local_pawn_.store(pawn);
   input.stop_enabled_.store(true);input.stop_fractional_.store(false);
 }
 static void fake_hook(Input& input, void** slot, void* original) {
   input.installed_=true;input.slot_=slot;input.original_=original;
   *slot=reinterpret_cast<void*>(&Input::create_move_detour);
 }
 static void live(Input& input, ShotState state) { input.require_live_shot_state_=true;input.set_shot_state(state); }
 static void configure(Input& input) { input.stop_enabled_.store(false); }
};
}
int main() {
 using namespace cs2::control;
 Input input;RageInputTestAccess::configure(input);input.set_rage_fire(true);
 Fixture f;
 for(int tick=0;tick<100;++tick) {
  f.reset();put(f.cmd.data(),8,tick+1);input.set_rage_aim(12,42,3,1);RageInputTestAccess::apply(input, f.cmd.data());
  check((get<std::uint64_t>(f.cmd.data(),k_cmd_buttons_offset)&button::k_attack)!=0,"held synthetic attack");
  check(get<int>(f.cmd.data(),k_cmd_pb_offset+0x3c)==2,"shot points at stamped history");
  check(get<float>(f.base_angles.data(),k_angles_values_offset)==12&&
        get<float>(f.base_angles.data(),k_angles_values_offset+4)==42,"base command and history agree");
  const auto changed=get<std::uint64_t>(f.buttons.data(),k_buttonstate_values_offset+8);
  check((changed&button::k_attack)==button::k_attack,"each fresh shot has an attack edge");
  check((changed&button::k_reload)!=0,"preserve unrelated buttons");
  RageInputTestAccess::apply(input, f.cmd.data());
  check((get<std::uint64_t>(f.buttons.data(),k_buttonstate_values_offset+8)&button::k_attack)==
        button::k_attack,"repeated hooks preserve the same command edge");
  for(int i=0;i<3;++i) {
   check(get<float>(f.angles[i].data(),k_angles_values_offset)==12,"history pitch stamped");
   check(get<float>(f.angles[i].data(),k_angles_values_offset+4)==42,"history yaw stamped");
   check(get<unsigned>(f.entries[i].data(),0x10)&1,"entry advertises angles");
  }
 }

 // Model a semi-automatic weapon across commands, including cooldown and reload.
 // The old held-input implementation produced only the first engine press.
 Input repeated;RageInputTestAccess::configure(repeated);repeated.set_rage_fire(true);
 Fixture cycle;int engine_shots=0,next_primary=100,clip=20;
 for(int tick=100;tick<160;++tick) {
   cycle.reset();put(cycle.cmd.data(),8,tick);
   const bool reload=tick>=116&&tick<123;
   ShotState state{11,22,tick,tick,tick,next_primary,clip,true,reload};
   RageInputTestAccess::live(repeated,state);repeated.set_rage_aim(12,42,3,1);
   RageInputTestAccess::apply(repeated,cycle.cmd.data());
   const auto held=get<std::uint64_t>(cycle.cmd.data(),k_cmd_buttons_offset);
   const auto changed=get<std::uint64_t>(cycle.cmd.data(),k_cmd_buttons_offset+8);
   const auto scroll=get<std::uint64_t>(cycle.cmd.data(),k_cmd_buttons_offset+16);
   const auto requests=repeated.rage_shots();
   if((held&button::k_attack)&&(changed&button::k_attack)&&(scroll&button::k_attack)) {
     check(!reload&&tick>=next_primary,"simulated weapon only receives ready shots");
     ++engine_shots;--clip;next_primary=tick+4;
   } else check(!(held&button::k_attack),"busy weapon receives a released trigger");
   RageInputTestAccess::apply(repeated,cycle.cmd.data());
   check(repeated.rage_shots()==requests,"duplicate command cannot count a second shot");
 }
 check(engine_shots>=10,"weapon fires repeatedly after cooldown and reload, not only once");
 cycle.reset();put(cycle.cmd.data(),8,200);
 RageInputTestAccess::live(repeated,{11,22,200,200,200,0,0,true,false});
 repeated.set_rage_aim(12,42,3,1);RageInputTestAccess::apply(repeated,cycle.cmd.data());
 check(!(get<std::uint64_t>(cycle.cmd.data(),k_cmd_buttons_offset)&button::k_attack),"empty magazine blocks firing");
 cycle.reset();put(cycle.cmd.data(),8,201);
 RageInputTestAccess::live(repeated,{11,33,201,201,201,0,10,true,false});
 repeated.set_rage_aim(12,42,3,1);RageInputTestAccess::apply(repeated,cycle.cmd.data());
 check(get<std::uint64_t>(cycle.cmd.data(),k_cmd_buttons_offset)&button::k_attack,"weapon switch resets old weapon gate");
 // A target can request braking while accuracy prevents a shot.
 cycle.reset();put(cycle.cmd.data(),8,202);repeated.set_shot_plan(false,true);
 repeated.set_rage_aim(12,42,3,1);RageInputTestAccess::apply(repeated,cycle.cmd.data());
 check(!(get<std::uint64_t>(cycle.cmd.data(),k_cmd_buttons_offset)&button::k_attack),"stop request is not a shot commit");

 // Actual post-builder feature path: normal client commands are already state 2.
 // This is not the pre-builder overwrite guard used by the Velocity hook.
 Fixture built;put(built.cmd.data(),8,500);put(built.cmd.data(),0x94,2u);
 Input post;RageInputTestAccess::configure(post);post.set_rage_fire(true);
 int observed=0;
 struct Context { Input* input; int* observed; } context{&post,&observed};
 post.set_command_observer([](void* p,void*){
   auto* c=static_cast<Context*>(p);++*c->observed;c->input->set_rage_aim(5,35,3,1);
 },&context);
 NativeCommandApi api{};
 api.command_base=[](void*)->void*{return nullptr;};
 api.command=[](void*,std::uint32_t)->void*{return nullptr;};
 api.assign_crc=[](std::uintptr_t* field,const EngineStringView*,void*){*field=0x1234;};
 api.allocate_buttons=[](void*)->void*{return nullptr;};
 api.get_view=[](void*,int)->cs2::game::Vec3*{return nullptr;};
 api.set_view=[](void*,int,const cs2::game::Vec3*){};api.handle_view=reinterpret_cast<void*>(1);
 RageInputTestAccess::post_build(post,built.cmd.data(),api);
 check(observed==1,"completed state-2 command reaches shot coordinator");
 check(get<std::uint64_t>(built.cmd.data(),k_cmd_buttons_offset)&button::k_attack,"completed command reaches attack commit");
 check(post.command_crc_writes()==1&&get<std::uintptr_t>(built.base.data(),0x30)==0x1234,"completed command reaches final serialization");
 Fixture full(64);input.set_rage_aim(1,2,3,1);RageInputTestAccess::apply(input, full.cmd.data());
 check(get<int>(full.cmd.data(),k_cmd_pb_offset+0x3c)==63,"64-entry history supported");
 Fixture empty(0);input.set_rage_aim(1,2,3,1);RageInputTestAccess::apply(input, empty.cmd.data());
 check(!(get<std::uint64_t>(empty.cmd.data(),k_cmd_buttons_offset)&button::k_attack),"empty history cannot auto-fire");
 check(get<int>(empty.cmd.data(),k_cmd_pb_offset+0x3c)==-1,"empty history index invalidated");
 f.reset();input.set_rage_aim(1,2,3,1);RageInputTestAccess::stale(input);RageInputTestAccess::apply(input, f.cmd.data());
 check(!input.rage_active(),"stale solve expires on command thread");
 check(!(get<std::uint64_t>(f.cmd.data(),k_cmd_buttons_offset)&button::k_attack),"expired solve cannot auto-fire");
 input.set_rage_fire(false);f.reset();put(f.cmd.data(),k_cmd_buttons_offset,button::k_attack|button::k_reload);
 input.set_rage_aim(1,2,3,1);RageInputTestAccess::apply(input, f.cmd.data());
 check(get<int>(f.cmd.data(),k_cmd_pb_offset+0x3c)==2,"manual fire also uses stamped history");
 f.reset();put(f.cmd.data(),k_cmd_buttons_offset,button::k_attack);input.note_local_pawn(1);
 input.set_recoil_query([](void*,std::uintptr_t,float* out){out[0]=3;out[1]=4;return true;},nullptr);
 input.set_rage_aim(12,42,3,1);RageInputTestAccess::apply(input,f.cmd.data());
 check(get<float>(f.angles[0].data(),k_angles_values_offset)==9&&
       get<float>(f.angles[0].data(),k_angles_values_offset+4)==38,"command-time recoil compensation");
 input.set_recoil_query(nullptr,nullptr);
 input.clear_rage_aim();
 // The user's failure: autostop and silent aim execute in the same command.
 std::array<std::byte,0x100> pawn{};put(pawn.data(),0x10,1u);
 put(pawn.data(),0x20,180.0F);put(pawn.data(),0x24,40.0F);
 auto world=[](const Fixture& fixture){
  const float yaw=get<float>(fixture.base_angles.data(),k_angles_values_offset+4)*k_pi/180.0F;
  const float f=get<float>(fixture.base.data(),k_base_move_offset),l=get<float>(fixture.base.data(),k_base_move_offset+4);
  return std::array<float,2>{std::cos(yaw)*f-std::sin(yaw)*l,std::sin(yaw)*f+std::cos(yaw)*l};
 };
 Fixture stopped,combined;
 Input stop_only,stop_and_aim;stop_and_aim.set_rage_fire(true);
 RageInputTestAccess::stopping(stop_only,reinterpret_cast<std::uintptr_t>(pawn.data()));
 RageInputTestAccess::stopping(stop_and_aim,reinterpret_cast<std::uintptr_t>(pawn.data()));
 stop_only.set_rage_aim(10,90,3,1);stop_and_aim.set_rage_aim(10,90,3,1);
 check(RageInputTestAccess::stop(stop_only,stopped.cmd.data(),stopped.base.data()),"autostop baseline applied");
 const auto expected=world(stopped);
 RageInputTestAccess::apply(stop_and_aim,combined.cmd.data());
 const auto actual=world(combined);
 check(std::fabs(expected[0]-actual[0])<.001F&&std::fabs(expected[1]-actual[1])<.001F,
       "silent aim preserves autostop world-space braking");
 check(get<float>(combined.base_angles.data(),k_angles_values_offset+4)==90&&
       get<float>(combined.angles[2].data(),k_angles_values_offset+4)==90,"stopping and aiming agree on shot yaw");
 for(int tick=1;tick<=100;++tick) {
  stopped.reset();combined.reset();put(stopped.cmd.data(),8,tick);put(combined.cmd.data(),8,tick);
  put(stopped.base_angles.data(),k_angles_values_offset+4,32.0F);
  put(combined.base_angles.data(),k_angles_values_offset+4,32.0F);
  put(stopped.base.data(),k_base_move_offset,0.0F);put(stopped.base.data(),k_base_move_offset+4,0.0F);
  put(combined.base.data(),k_base_move_offset,0.0F);put(combined.base.data(),k_base_move_offset+4,0.0F);
  const float yaw=normalize_angle(-150.0F+tick*3.0F);
  stop_only.set_rage_aim(10,yaw,3,1);stop_and_aim.set_rage_aim(10,yaw,3,1);
  check(RageInputTestAccess::stop(stop_only,stopped.cmd.data(),stopped.base.data()),"held-key stop baseline");
  const auto before=world(stopped);
  RageInputTestAccess::apply(stop_and_aim,combined.cmd.data());
  RageInputTestAccess::apply(stop_and_aim,combined.cmd.data());
  const auto after=world(combined);
  check(std::fabs(before[0]-after[0])<.001F&&std::fabs(before[1]-after[1])<.001F,
        "100 held-key stop/aim commands preserve braking across duplicate hooks");
  check(get<float>(combined.base_angles.data(),k_angles_values_offset+4)==yaw&&
        get<float>(combined.angles[2].data(),k_angles_values_offset+4)==yaw,"held-key command/history remain synchronized");
 }
 stop_only.clear_rage_aim();stop_and_aim.clear_rage_aim();
 // A shared protobuf default must never be modified, even if it is readable.
 auto* shared=static_cast<std::byte*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 check(shared!=nullptr,"allocate read-only default fixture");
 put(shared,k_angles_values_offset,77.0F);DWORD previous=0;
 check(VirtualProtect(shared,4096,PAGE_READONLY,&previous)!=0,"protect protobuf default fixture");
 Fixture defaults;
 for(int i=0;i<3;++i)put(defaults.entries[i].data(),k_history_entry_angles_offset,reinterpret_cast<std::uintptr_t>(shared));
 input.set_rage_fire(true);input.set_rage_aim(1,2,3,1);
 RageInputTestAccess::apply(input,defaults.cmd.data());
 check(get<float>(shared,k_angles_values_offset)==77,"shared default angles not overwritten");
 check(!(get<std::uint64_t>(defaults.cmd.data(),k_cmd_buttons_offset)&button::k_attack),"read-only history cannot auto-fire");
 input.clear_rage_aim();VirtualFree(shared,0,MEM_RELEASE);
 // An in-flight result cannot bypass the menu's off state.
 input.set_rage_enabled(false);input.set_rage_aim(1,2,3,1);
 Fixture disabled;RageInputTestAccess::apply(input,disabled.cmd.data());
 check(!input.rage_active()&&!(get<std::uint64_t>(disabled.cmd.data(),k_cmd_buttons_offset)&button::k_attack),
       "disabled toggle blocks old worker publications and firing");
 // The recorded crash was cleanup after the client vtable became unavailable.
 auto* table=static_cast<void**>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 check(table!=nullptr,"allocate shutdown hook fixture");
 auto* hooked=new Input;RageInputTestAccess::fake_hook(*hooked,table,reinterpret_cast<void*>(0x1234));
 check(VirtualProtect(table,4096,PAGE_READONLY,&previous)!=0,"protect cleanup fixture");
 delete hooked;check(*table==reinterpret_cast<void*>(0x1234),"owned hook restored safely");
 MEMORY_BASIC_INFORMATION region{};VirtualQuery(table,&region,sizeof(region));
 check(region.Protect==PAGE_READONLY,"cleanup preserves table protection");
 VirtualProtect(table,4096,PAGE_READWRITE,&previous);
 hooked=new Input;RageInputTestAccess::fake_hook(*hooked,table,reinterpret_cast<void*>(0x1234));
 VirtualFree(table,0,MEM_RELEASE);delete hooked;

 std::puts("PASS: 100 held-key commands, coherent history, attack index, edge transitions, empty/full history, expiry, manual fire");
}
