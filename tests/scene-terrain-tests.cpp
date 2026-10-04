#include "scene/scene-frame-snapshot.h"
#include <iostream>
#include <limits>
#include <map>
#include <type_traits>
#include <cstring>
#include <vector>
using namespace vaporcruise::scene;
int main() {
 int failures=0;
 auto check=[&](bool ok,const char* why){if(!ok){++failures;std::cerr<<"FAIL: "<<why<<'\n';}};
 static_assert(!std::is_copy_constructible_v<SceneState>);
 static_assert(sizeof(SceneFrameSnapshot)<1024);
 SceneState state; ScenePose pose; pose.cruise={31,.75}; check(state.SetPose(pose),"install initial pose");
 for(int i=0;i<10;++i)check(state.Current().cruise.cell==31,"Current preserves pose");
 auto first=state.PrepareFrame(); check(first.rowsBuilt==363 && first.terrainSlots.size()==11,"first preparation builds eleven sections");
 check(first.scene.cruise.cell==31 && first.scene.cruise.phase==.75,"snapshot contains current pose");
 auto settled=state.PrepareFrame(); check(settled.rowsBuilt==0 && settled.terrainSlots.data()==first.terrainSlots.data(),"settled frame reuses storage");
 std::map<int64_t,uint64_t> original;
 for(const auto& slot:settled.terrainSlots)if(slot.active)original[slot.sectionIndex]=slot.revision;
 state.Advance(.25); auto crossing=state.PrepareFrame(); unsigned retained=0;
 for(const auto& slot:crossing.terrainSlots)if(slot.active){check(slot.complete,"active sections complete");if(original.contains(slot.sectionIndex)&&original[slot.sectionIndex]==slot.revision)++retained;}
 check(retained==9 && crossing.rowsBuilt==4,"crossing retains nine meshes with four row budget");
 pose.cruise=state.Current().cruise;pose.palette=1;pose.paletteProgress=.5;pose.cityLightPhase=.37;
 const auto* storage=crossing.terrainSlots.data();
 auto terrainBytes = [](const auto& slots) {
  std::vector<unsigned char> bytes;
  for(const auto& slot:slots)if(slot.active){const auto* start=reinterpret_cast<const unsigned char*>(slot.vertices.data());bytes.insert(bytes.end(),start,start+sizeof(slot.vertices));}
  return bytes;
 };
 const auto beforePalette=terrainBytes(crossing.terrainSlots);
 check(state.SetPose(pose),"same position animation pose accepted");
 auto same=state.PrepareFrame();check(same.rowsBuilt==4 && same.terrainSlots.data()==storage,"same pose preserves partial staging");
 check(terrainBytes(same.terrainSlots)==beforePalette,"palette and independent clock pose preserve visible terrain geometry");
 unsigned rows=8;
 for(int i=0;i<8;++i){auto f=state.PrepareFrame();check(f.rowsBuilt<=4,"bounded partial staging");rows+=f.rowsBuilt;}
 check(rows==33,"same pose never restarts staging");
 auto stable=state.PrepareFrame();
 uint64_t revisions[11];for(size_t i=0;i<11;++i)revisions[i]=stable.terrainSlots[i].revision;
 auto invalid=pose;invalid.paletteProgress=std::numeric_limits<double>::quiet_NaN();
 check(!state.SetPose(invalid),"invalid pose rejected atomically");
 auto after=state.PrepareFrame();check(after.rowsBuilt==0 && after.scene.cityLightPhase==.37,"invalid pose preserves scene and terrain");
 for(size_t i=0;i<11;++i)check(revisions[i]==after.terrainSlots[i].revision,"invalid pose preserves revision");
 for(auto cell:{0LL,3200LL,36028797018963971LL,static_cast<long long>(kMaxTravelCell)}){
  pose.cruise={cell,.25};check(state.SetPose(pose),"discontinuous pose accepted");auto f=state.PrepareFrame();
  check(f.rowsBuilt==363,"explicit discontinuity resets once");for(const auto& s:f.terrainSlots)check(s.complete,"reset slots complete");
  check(state.PrepareFrame().rowsBuilt==0,"reset only once");
 }
 ++pose.seed;check(state.SetPose(pose),"seed change accepted");check(state.PrepareFrame().rowsBuilt==363,"seed change resets");
 SceneState moved(std::move(state));check(moved.PrepareFrame().rowsBuilt==0,"move preserves owned terrain");
 return failures?1:0;
}
