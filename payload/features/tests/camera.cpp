#include "cs2/features/camera.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace cs2;
namespace {
void check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
template<class T> void put(void* p,std::size_t at,T v){std::memcpy(static_cast<std::byte*>(p)+at,&v,sizeof(v));}
template<class T> T get(const void* p,std::size_t at){T v{};std::memcpy(&v,static_cast<const std::byte*>(p)+at,sizeof(v));return v;}
alignas(8) std::array<std::byte,0x1000> pawn{};
alignas(8) std::array<std::byte,0x100> scene{};
game::Vec3 angles{};
float fraction=1, radius=12;
std::uintptr_t hit=0x44;
int traces=0;
game::Vec3* view(void*,int slot){check(slot==0,"camera view slot");return &angles;}
void filter(void*,std::uintptr_t skip,std::uint64_t mask,std::uint8_t layer,int type){
  check(skip==reinterpret_cast<std::uintptr_t>(pawn.data()),"camera skips local pawn");
  check(mask==0x1c3003&&layer==4&&type==7,"Velocity camera collision filter");
}
bool trace(void*,void* ray,const game::Vec3* start,const game::Vec3*,const void*,void* result){
  ++traces;
  const auto mins=get<game::Vec3>(ray,0),maxs=get<game::Vec3>(ray,12);
  check(mins.x==-radius&&mins.y==-radius&&mins.z==-radius&&maxs.x==radius&&maxs.y==radius&&maxs.z==radius,"symmetric camera collision hull");
  check(get<unsigned char>(ray,0x28)==2&&start->z==64,"hull type and eye origin");
  put(result,0xac,fraction);put(result,8,hit);return fraction<1;
}
void position(const void* setup,float x,float y,float z){const auto v=get<game::Vec3>(setup,0x4a0);check(std::abs(v.x-x)<0.001F&&std::abs(v.y-y)<0.001F&&std::abs(v.z-z)<0.001F,"camera position");}
}
int main(){
  features::CameraFeatures camera;
  std::uintptr_t physics=1,manager=reinterpret_cast<std::uintptr_t>(&physics);
  std::array<std::uintptr_t,64> blocks{};
  std::array<std::byte,0x70> page{};put(page.data(),0,std::uintptr_t{0x44});
  blocks[0]=reinterpret_cast<std::uintptr_t>(page.data());
  std::uintptr_t block_base=reinterpret_cast<std::uintptr_t>(blocks.data());
  features::CameraBindings api{};api.filter=filter;api.trace=trace;api.view_angles=view;
  api.manager_global=reinterpret_cast<std::uintptr_t>(&manager);api.entity_blocks_global=reinterpret_cast<std::uintptr_t>(&block_base);
  camera.bind(api);camera.note_local_pawn(reinterpret_cast<std::uintptr_t>(pawn.data()));
  put(pawn.data(),0x330,reinterpret_cast<std::uintptr_t>(scene.data()));put(pawn.data(),0x34c,100);
  put(pawn.data(),0x354,std::uint8_t{0});put(pawn.data(),0x3e7,std::uint8_t{2});
  put(pawn.data(),0xf60,game::Vec3{0,0,64});
  std::array<std::byte,0x600> setup{};put(setup.data(),0x498,90.0F);
  camera.configure(true,true,85,12);camera.apply_view(setup.data());position(setup.data(),-85,0,64);
  fraction=.5F;camera.apply_view(setup.data());position(setup.data(),-42.5F,0,64);
  hit=0x55;camera.apply_view(setup.data());position(setup.data(),-85,0,64);
  hit=0x44;fraction=1;angles={0,90,0};camera.apply_view(setup.data());position(setup.data(),0,-85,64);
  camera.configure(true,true,300,30);radius=20;angles={};camera.apply_view(setup.data());position(setup.data(),-200,0,64);
  const int before=traces;camera.configure(false,true,85,12);camera.apply_view(setup.data());check(traces==before,"disabled camera makes no engine trace");
  camera.configure(true,true,85,12);put(pawn.data(),0x34c,0);camera.apply_view(setup.data());check(traces==before,"dead pawn leaves camera unchanged");
  std::array<std::uint8_t,8> scope;scope.fill(0xff);camera.apply_scope(scope.data());
  for(unsigned i=0;i<scope.size();++i)check(scope[i]==(i==4?0:0xff),"only scope overlay flag changed");
  camera.configure(false,false,85,12);scope.fill(0xff);camera.apply_scope(scope.data());check(scope[4]==0xff,"disabled scope removal preserves output");
  check(get<float>(setup.data(),0x498)==90.0F,"zoom/FOV left intact");
  std::puts("PASS: camera placement, world collision, hull bounds, activation, alive check and scope-only removal");
}
