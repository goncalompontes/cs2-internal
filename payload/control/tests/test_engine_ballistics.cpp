#include "cs2/control/engine_ballistics.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
namespace {
int mode=0;
alignas(8) std::byte hitbox_data[0x40]{};
void check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
void filter(cs2::control::engine_trace::Filter*,std::uintptr_t local,std::uint64_t mask,std::uint8_t layer,int type){check(local==11&&mask==0x1c300b&&layer==3&&type==15,"engine filter arguments");}
void init(cs2::control::engine_trace::Data* data,const cs2::aim::Vec3*,const cs2::aim::Vec3* delta,cs2::control::engine_trace::Filter*,int count,bool world){
 check(delta->x==8192&&count==4&&world,"range, penetration count, world tracing");
 data->hit_count=mode==4?9:2;
 data->hit_elements[0]={0.1f,mode==1?1.0f:0.2f,80,2,0,0,1,{}};
 data->hit_elements[1]={0.2f,0.3f,30,3,0,1,0,{}};
 // The entry contact is deliberately a different actor: only +0x12 works.
 const std::uint32_t prior=99;std::memcpy(data->elements[0].bytes+0x2c,&prior,4);
 const auto pointer=reinterpret_cast<std::uintptr_t>(hitbox_data);
 std::memcpy(data->elements[1].bytes+0x10,&pointer,sizeof(pointer));
 const int group=mode==8?12:1;std::memcpy(hitbox_data+0x38,&group,4);
 data->elements[1].bytes[0x33]=static_cast<std::byte>(mode==7?0:1);
 std::uint32_t handle=mode==2?99:77;std::memcpy(data->elements[1].bytes+0x2c,&handle,4);
 if(mode==3)data->hit_elements[1].exit_contact=128;
 if(mode==5)data->hit_elements[1].damage=std::numeric_limits<float>::quiet_NaN();
 if(mode==6)data->hit_count=0;
}
void bullet(cs2::control::engine_trace::Data*,float damage,float penetration,float modifier,int count,int team,std::uintptr_t unknown){check(damage==100&&penetration==2&&modifier==.98f&&count==4&&team==2&&unknown==0,"engine bullet parameters");}
std::uintptr_t resolve(void*,std::uint32_t handle){return handle==77?22:33;}
}
int main(){
 cs2::control::EngineBallistics engine;
 cs2::aim::WeaponProfile weapon;weapon.damage=100;weapon.penetration=2;weapon.range=8192;weapon.range_modifier=.98f;
 check(!engine.evaluate(11,2,22,{},{1,0,0},weapon).known,"unbound backend unknown");
 engine.bind({filter,init,bullet,resolve,nullptr});
 for(mode=0;mode<=8;++mode){
  auto result=engine.evaluate(11,2,22,{},{1,0,0},weapon);
  if(mode==0)check(result.known&&result.damage==30&&result.hitgroup==1,"engine penetration damage before hitgroup scaling");
  if(mode==1||mode==2||mode==6||mode==7)check(result.known&&result.damage==0,"blocked/wrong actor/miss do not pass damage");
  if(mode==3||mode==4||mode==5||mode==8)check(!result.known&&result.damage==0,"bad counts/indices/NaNs fail closed");
 }
 std::puts("PASS: engine ABI arguments, material exit, target identity, bounds, invalid damage, misses");
}
