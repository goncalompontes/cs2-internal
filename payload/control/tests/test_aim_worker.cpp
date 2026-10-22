#include "cs2/control/service.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <semaphore>
namespace cs2::control {
struct AimWorkerTestAccess {
 static bool active(const Service& service) { return service.input_.rage_active(); }
 static bool firing(const Service& service) { return service.input_.rage_fire(); }
};
}
namespace {
void check(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
}
int main(){
 using namespace cs2;
 std::promise<void> started,latest;
 auto entered=started.get_future();auto newest=latest.get_future();
 std::binary_semaphore release(0);
 std::atomic<bool> first{true},latest_seen{false},middle_seen{false};
 control::Service service;
 service.set_rage_features(true,0,false); // no activation key and no forced mode
 service.set_aim_damage_queries([&](aim::Vec3 eye,aim::Vec3,float,const aim::WeaponProfile&){
  if(first.exchange(false)){started.set_value();release.acquire();}
  if(eye.x==2)middle_seen.store(true);
  if(eye.x==3&&!latest_seen.exchange(true))latest.set_value();
  return aim::WorldDamage{50,true};
 },{});
 game::Snapshot snapshot;snapshot.valid=true;
 game::PlayerSnapshot local;local.alive=true;local.is_local=true;local.team=2;local.pawn=11;local.eye_origin={1,0,64};
 snapshot.local=local;snapshot.local_pawn=11;
 game::PlayerSnapshot enemy;enemy.alive=true;enemy.team=3;enemy.pawn=22;enemy.index=7;
 enemy.origin={100,0,0};enemy.eye_origin={100,0,64};snapshot.players.push_back(enemy);
 service.on_snapshot(snapshot);
 check(entered.wait_for(std::chrono::seconds(2))==std::future_status::ready,"toggle activates background aim with no held key");
 const auto begin=std::chrono::steady_clock::now();
 snapshot.local->eye_origin.x=2;service.on_snapshot(snapshot);
 snapshot.local->eye_origin.x=3;service.on_snapshot(snapshot);
 const auto elapsed=std::chrono::steady_clock::now()-begin;
 check(elapsed<std::chrono::milliseconds(100),"render-side submission never waits for blocked solve");
 release.release();
 check(newest.wait_for(std::chrono::seconds(2))==std::future_status::ready,"latest pending snapshot processed");
 service.set_rage_features(false,0,false);
 service.stop();
 check(!control::AimWorkerTestAccess::active(service),"toggle off cancels pending aim");
 check(!control::AimWorkerTestAccess::firing(service),"Auto fire remains off");
 check(!middle_seen.load(),"intermediate stale snapshot replaced, not queued");
 std::puts("PASS: blocked solver does not block render submission; latest snapshot wins; worker stops cleanly");
}
