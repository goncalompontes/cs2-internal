#include "cs2/features/camera.hpp"
#include "cs2/game/entity_system.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/schema.hpp"
#include "cs2/game/pattern.hpp"
#include "cs2/game/log.hpp"
#include "cs2/tls/bootstrap.hpp"
#include "offsets.hpp"
#include "MinHook.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace cs2::features {
namespace {
// Native C callbacks for the one camera feature owner in this payload.
CameraFeatures* camera_owner = nullptr;
void __fastcall override_detour(void* self, void* setup) {
  tls::ensure_thread_tls();auto* camera=camera_owner;if(!camera)return;
  reinterpret_cast<void (*)(void*,void*)>(camera->original_view())(self,setup);
  camera->apply_view(setup);
}
void __fastcall scope_detour(void* self, void* scope) {
  tls::ensure_thread_tls();auto* camera=camera_owner;if(!camera)return;
  reinterpret_cast<void (*)(void*,void*)>(camera->original_scope())(self,scope);
  camera->apply_scope(scope);
}
struct HullRay { game::Vec3 mins{},maxs{};std::byte pad[0x10]{};std::uint8_t type{2};std::byte tail[7]{}; };
static_assert(sizeof(HullRay)==0x30 && offsetof(HullRay,type)==0x28);
bool sweep(const CameraBindings* api,std::uintptr_t pawn,const game::Vec3* eye,
           const game::Vec3* end,float radius,std::byte* output) noexcept {
  std::uintptr_t manager=0,physics=0;
  if(!game::read(api->manager_global,manager)||!manager||!game::read(manager,physics)||!physics)return false;
  HullRay ray;ray.mins={-radius,-radius,-radius};ray.maxs={radius,radius,radius};
  alignas(8) std::byte filter[0x48]{};
  __try {
    api->filter(filter,pawn,0x1c3003,4,7);
    api->trace(reinterpret_cast<void*>(manager),&ray,eye,end,filter,output);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void store(std::uintptr_t address,const void* value,std::size_t size) noexcept {
  SIZE_T written=0;WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),value,size,&written);
}
}
CameraFeatures::~CameraFeatures(){uninstall();}
bool CameraFeatures::prepare(game::ModuleInfo module) noexcept {
  const auto scope=game::unique_code_match(module,"488BC453574883EC68488BFA");
  const auto table_site=game::unique_code_match(module,"A8000000488D05????????4C89742420");
  const auto trace=game::unique_code_match(module,"48895424??48894C24??55535657415441564157488DAC24????????B810250000");
  const auto manager_site=game::unique_code_match(module,"488B0D????????488D3452");
  const auto filter=game::unique_code_match(module,"48895C24??48897424??574883EC??0FB641??33FF24");
  const auto view_site=game::unique_code_match(module,"8B0D????????8BD3E8????????F20F1000");
  const auto* input=sdk::find_offset("ccsgo_input");const auto* entities=sdk::find_offset("entity_list");
  if(!scope||!table_site||!trace||!manager_site||!filter||!view_site||!input||!entities)return false;
  std::int32_t table_relative=0,manager_relative=0,view_relative=0;std::uintptr_t override_view=0;
  if(!game::read(table_site+7,table_relative)||!game::read(manager_site+3,manager_relative)||
     !game::read(view_site+9,view_relative))return false;
  const auto table=table_site+11+table_relative;
  if(!module.contains(table+0x78)||!game::read(table+0x78,override_view)||!module.contains(override_view))return false;
  const auto view=view_site+13+view_relative;
  const auto manager_global=manager_site+7+manager_relative;
  if(!module.contains(view)||!module.contains(manager_global))return false;
  api_={reinterpret_cast<void*>(override_view),reinterpret_cast<void*>(scope),
      reinterpret_cast<decltype(api_.filter)>(filter),reinterpret_cast<decltype(api_.trace)>(trace),
      reinterpret_cast<decltype(api_.view_angles)>(view),reinterpret_cast<void*>(module.base+input->rva),
      manager_global,module.base+entities->rva};
  return true;
}
bool CameraFeatures::install() noexcept {
  if(installed_)return true;
  const auto module=game::find_module("client.dll");
  if(!module.valid()||!prepare(module))return false;
  scene_offset_=game::schema_field("C_BaseEntity","m_pGameSceneNode").value_or(scene_offset_);
  origin_offset_=game::schema_field("CGameSceneNode","m_vecAbsOrigin").value_or(origin_offset_);
  eye_offset_=game::schema_field("C_BaseModelEntity","m_vecViewOffset").value_or(eye_offset_);
  health_offset_=game::schema_field("C_BaseEntity","m_iHealth").value_or(health_offset_);
  life_offset_=game::schema_field("C_BaseEntity","m_lifeState").value_or(life_offset_);
  team_offset_=game::schema_field("C_BaseEntity","m_iTeamNum").value_or(team_offset_);
  const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
  auto status=MH_CreateHook(api_.override_view,reinterpret_cast<void*>(&override_detour),&original_view_);
  if(status!=MH_OK)return false;
  status=MH_CreateHook(api_.render_scope,reinterpret_cast<void*>(&scope_detour),&original_scope_);
  if(status==MH_OK)status=MH_QueueEnableHook(api_.override_view);
  if(status==MH_OK)status=MH_QueueEnableHook(api_.render_scope);
  camera_owner=this;
  if(status==MH_OK)status=MH_ApplyQueued();
  if(status!=MH_OK){uninstall();return false;}
  installed_=true;game::append_log("[camera] thirdperson collision and scope-removal hooks installed");return true;
}
void CameraFeatures::uninstall() noexcept {
  if(!original_view_&&!original_scope_)return;
  if(original_view_)MH_QueueDisableHook(api_.override_view);
  if(original_scope_)MH_QueueDisableHook(api_.render_scope);
  MH_ApplyQueued();
  if(original_scope_)MH_RemoveHook(api_.render_scope);
  if(original_view_)MH_RemoveHook(api_.override_view);
  if(camera_owner==this)camera_owner=nullptr;
  original_view_=nullptr;original_scope_=nullptr;installed_=false;
}
void CameraFeatures::configure(bool third,bool scope,float distance,float hull) noexcept {
  distance_.store(std::isfinite(distance)?std::clamp(distance,35.0F,200.0F):85.0F);
  hull_.store(std::isfinite(hull)?std::clamp(hull,0.0F,20.0F):12.0F);
  third_person_.store(third);remove_scope_.store(scope);
}
void CameraFeatures::apply_view(void* setup) noexcept {
  if(!third_person_.load()||!setup||!api_.trace||!api_.filter||!api_.view_angles)return;
  const auto pawn=pawn_.load();std::uintptr_t scene=0;game::Vec3 origin{},offset{},angles{};
  int health=0;std::uint8_t life=1,team=0;
  if(!pawn||!game::read(pawn+health_offset_,health)||health<=0||!game::read(pawn+life_offset_,life)||life||
     !game::read(pawn+team_offset_,team)||team<2||!game::read(pawn+scene_offset_,scene)||!scene||
     !game::read(scene+origin_offset_,origin)||!game::read(pawn+eye_offset_,offset))return;
  const auto view=api_.view_angles(api_.input,0);if(!view||!game::read(reinterpret_cast<std::uintptr_t>(view),angles))return;
  constexpr float to_rad=0.017453292519943295F;
  const float pitch=angles.x*to_rad,yaw=angles.y*to_rad;
  const game::Vec3 forward{std::cos(pitch)*std::cos(yaw),std::cos(pitch)*std::sin(yaw),-std::sin(pitch)};
  const auto eye=origin+offset;auto position=eye-forward*distance_.load();
  alignas(8) std::array<std::byte,0x200> result{};
  const float clear=1;std::memcpy(result.data()+0xac,&clear,sizeof(clear));
  if(!sweep(&api_,pawn,&eye,&position,hull_.load(),result.data()))return;
  float fraction=1;std::memcpy(&fraction,result.data()+0xac,4);
  if(!std::isfinite(fraction)||fraction<0||fraction>1)return;
  std::uintptr_t hit=0,blocks=0;std::memcpy(&hit,result.data()+8,sizeof(hit));
  if(!game::read(api_.entity_blocks_global,blocks)||!blocks)return;
  const auto world=game::EntitySystem(blocks).instance(0);
  if(fraction<1&&hit==world)position=eye+(position-eye)*fraction;
  if(std::isfinite(position.x)&&std::isfinite(position.y)&&std::isfinite(position.z))
    store(reinterpret_cast<std::uintptr_t>(setup)+0x4a0,&position,sizeof(position));
}
void CameraFeatures::apply_scope(void* scope) noexcept {
  if(remove_scope_.load()&&scope){const std::uint8_t hidden=0;store(reinterpret_cast<std::uintptr_t>(scope)+4,&hidden,1);}
}
}
