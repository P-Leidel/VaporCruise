#include "render/terrain-upload-state.h"
#include <iostream>
#include <memory>
using namespace vaporcruise;
int main(){
 int failures=0;auto check=[&](bool ok,const char* msg){if(!ok){++failures;std::cerr<<msg<<'\n';}};
 render::TerrainUploadState uploads;auto slot=std::make_unique<scene::TerrainSectionSlot>();slot->complete=true;
 check(uploads.NeedsUpload(0,*slot),"fresh complete revision zero uploads");
 check(uploads.NeedsUpload(0,*slot),"failed upload remains pending");
 uploads.Uploaded(0,0);check(!uploads.NeedsUpload(0,*slot),"successful zero revision skipped");
 slot->revision=1;check(uploads.NeedsUpload(0,*slot),"changed revision uploads");uploads.Uploaded(0,1);
 slot->complete=false;slot->revision=2;check(!uploads.NeedsUpload(0,*slot),"partial recycled staging never uploads");
 slot->complete=true;check(uploads.NeedsUpload(0,*slot),"completed staging uploads once");uploads.Uploaded(0,2);
 check(!uploads.NeedsUpload(0,*slot),"unchanged staging skipped");uploads.Reset();check(uploads.NeedsUpload(0,*slot),"new device reuploads unchanged revision");
 for(size_t i=0;i<scene::kTerrainSlotCount;++i){check(uploads.NeedsUpload(i,*slot),"all complete slots needed after reset");uploads.Uploaded(i,2);}
 for(size_t i=0;i<scene::kTerrainSlotCount;++i)check(!uploads.NeedsUpload(i,*slot),"retained physical slots skipped");
 return failures?1:0;
}
