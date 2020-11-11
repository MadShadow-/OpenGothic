#include "particlesdefinitions.h"

#include <Tempest/Log>

#include "graphics/particlefx.h"
#include "utils/fileext.h"
#include "gothic.h"

using namespace Tempest;

ParticlesDefinitions::ParticlesDefinitions(Gothic& gothic) {
  vm = gothic.createVm(u"ParticleFx.dat");
  }

ParticlesDefinitions::~ParticlesDefinitions() {
  vm->clearReferences(Daedalus::IC_Pfx);
  }

const ParticleFx* ParticlesDefinitions::get(const char *n) {
  std::string name = n;
  while(FileExt::hasExt(name,"PFX"))
    name.resize(name.size()-4);

  std::lock_guard<std::mutex> guard(sync);
  return implGet(n);
  }

const ParticleFx* ParticlesDefinitions::implGet(const char* name) {
  auto it = pfx.find(name);
  if(it!=pfx.end())
    return it->second.get();
  Daedalus::GEngineClasses::C_ParticleFX decl={};
  if(!implGet(name,decl))
    return nullptr;
  std::unique_ptr<ParticleFx> p{new ParticleFx(decl,name)};
  auto elt = pfx.insert(std::make_pair<std::string,std::unique_ptr<ParticleFx>>(name,std::move(p)));

  auto* ret = elt.first->second.get();
  if(!decl.ppsCreateEm_S.empty())
    ret->ppsCreateEm = implGet(decl.ppsCreateEm_S.c_str());
  return ret;
  }

bool ParticlesDefinitions::implGet(const char *name,
                                   Daedalus::GEngineClasses::C_ParticleFX& ret) {
  if(!vm || name==nullptr || name[0]=='\0')
    return false;

  auto id = vm->getDATFile().getSymbolIndexByName(name);
  if(id==size_t(-1)) {
    Log::e("invalid particle system: \"",name,"\"");
    return false;
    }

  vm->initializeInstance(ret, id, Daedalus::IC_Pfx);
  vm->clearReferences(Daedalus::IC_Pfx);
  return true;
  }
