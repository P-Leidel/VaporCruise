#include "render/shader-program.h"
#include "render/shader-bindings.h"
namespace vaporcruise::render {
void BindTerrainProgram(ID3D11DeviceContext* context,const TerrainProgramInputs& p) {
    context->VSSetShader(p.vertex,nullptr,0);context->PSSetShader(p.pixel,nullptr,0);
    context->VSSetConstantBuffers(bindings::Frame,1,&p.frame);context->PSSetConstantBuffers(bindings::Frame,1,&p.frame);
    context->VSSetConstantBuffers(bindings::Section,1,&p.section);
}
void BindCompositeProgram(ID3D11DeviceContext* context,const CompositeProgramInputs& p) {
    context->VSSetShader(p.vertex,nullptr,0);context->PSSetShader(p.pixel,nullptr,0);
    context->PSSetConstantBuffers(bindings::Frame,1,&p.frame);
    context->PSSetShaderResources(bindings::Coverage,1,&p.background.panorama[0]);
    context->PSSetShaderResources(bindings::Steady,1,&p.background.panorama[1]);
    context->PSSetShaderResources(bindings::Animated,1,&p.background.panorama[2]);
    context->PSSetShaderResources(bindings::Detail,1,&p.background.detail);
    context->PSSetSamplers(bindings::PanoramaSampler,1,&p.background.sampler);
}
}
