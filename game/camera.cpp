#include "camera.h"

#include "world/objects/npc.h"
#include "world/objects/interactive.h"
#include "world/world.h"
#include "game/playercontrol.h"
#include "game/definitions/cameradefinitions.h"
#include "game/serialize.h"
#include "utils/gthfont.h"
#include "utils/dbgpainter.h"
#include "gothic.h"

using namespace Tempest;

static float angleMod(float a){
  a = std::fmod(a,360.f);
  if(a<-180.f)
    a+=360.f;
  if(a>180.f)
    a-=360.f;
  return a;
  }

// TODO: System/Camera/CamInst.d
Camera::Camera() {
  }

void Camera::reset(World& world) {
  auto pl = world.player();
  if(pl==nullptr)
    return;
  implReset(*pl);
  }

void Camera::implReset(const Npc &npc) {
  calcControlPoints(npc,false,0);
  src = dst;
  }

void Camera::save(Serialize &s) {
  Tempest::Vec3 unused;
  s.write(src.target, src.rotSpin, unused, src.range,
          dst.target, src.rotSpin, unused, dst.range,
          hasPos);
  }

void Camera::load(Serialize &s, Npc *pl) {
  if(pl)
    implReset(*pl);
  if(s.version()<24)
    return;
  Tempest::Vec3 unused;
  s.read(src.target, src.rotSpin, unused, src.range,
         dst.target, src.rotSpin, unused, dst.range,
         hasPos);
  }

void Camera::changeZoom(int delta) {
  if(delta>0)
    dst.range-=0.1f; else
    dst.range+=0.1f;
  clampRange(dst.range);
  }

void Camera::setViewport(uint32_t w, uint32_t h) {
  proj.perspective(65.f, float(w)/float(h), 0.01f, 85.0f);
  vpWidth  = w;
  vpHeight = h;
  }

void Camera::rotateLeft() {
  implMove(KeyEvent::K_Q);
  }

void Camera::rotateRight() {
  implMove(KeyEvent::K_E);
  }

void Camera::moveForward() {
  implMove(KeyEvent::K_W);
  }

void Camera::moveBack() {
  implMove(KeyEvent::K_S);
  }

void Camera::moveLeft() {
  implMove(KeyEvent::K_A);
  }

void Camera::moveRight() {
  implMove(KeyEvent::K_D);
  }

void Camera::setMode(Camera::Mode m) {
  if(camMod==m)
    return;

  camMod = m;

  const auto& def = cameraDef();
  dst.rotSpin.y = def.bestAzimuth;
  if(auto pl = Gothic::inst().player())
    dst.rotSpin.y+=pl->rotation();
  //dst.spin.x = def.bestElevation;
  dst.range  = def.bestRange;
  }

void Camera::setToogleEnable(bool e) {
  tgEnable = e;
  }

bool Camera::isToogleEnabled() const {
  return tgEnable;
  }

void Camera::setFirstPerson(bool fp) {
  fpEnable = fp;
  }

bool Camera::isFirstPerson() const {
  return fpEnable;
  }

void Camera::setLookBack(bool lb) {
  lbEnable = lb;
  }

void Camera::toogleDebug() {
  dbg = !dbg;
  }

void Camera::setSpin(const PointF &p) {
  dst.rotSpin  = Vec3(p.x,p.y,0);
  src.rotSpin  = dst.rotSpin;
  }

void Camera::setDestSpin(const PointF& p) {
  dst.rotSpin = Vec3(p.x,p.y,0);
  if(dst.rotSpin.x<-90)
    dst.rotSpin.x = -90;
  if(dst.rotSpin.x>90)
    dst.rotSpin.x = 90;
  }

void Camera::onRotateMouse(const PointF& dpos) {
  dst.rotSpin.x += dpos.x;
  dst.rotSpin.y += dpos.y;
  }

Matrix4x4 Camera::projective() const {
  auto ret = proj;
  if(auto w = Gothic::inst().world())
    w->globalFx()->morph(ret);
  return ret;
  }

Matrix4x4 Camera::viewShadow(const Vec3& lightDir, size_t layer) const {
  //Vec3 ldir = Vec3::normalize({1,1,0});
  Vec3 ldir   = lightDir;
  Vec3 center = src.target;
  auto vp = viewProj();
  vp.project(center);

  vp.inverse();
  Vec3 l = {-1,center.y,center.z}, r = {1,center.y,center.z};
  vp.project(l);
  vp.project(r);

  float smWidth    = 0;
  float smWidthInv = 0;
  float zScale     = 1.f/5120;

  switch(layer) {
    case 0:
      smWidth    = (r-l).length();
      smWidth    = std::max(smWidth,1024.f); // ~4 pixels per santimeter
      break;
    case 1:
      smWidth    = 5120;
      zScale    *= 0.2f;
      break;
    };

  smWidthInv = 1.f/smWidth;

  Matrix4x4 view;
  view.identity();
  view.rotate(-90, 1, 0, 0);     // +Y -> -Z
  view.rotate(180+src.rotSpin.y, 0, 1, 0);
  view.scale(smWidthInv, zScale, smWidthInv);
  view.translate(src.target);
  view.scale(-1,-1,-1);

  if(ldir.y!=0.f) {
    float lx = ldir.x/ldir.y;
    float lz = ldir.z/ldir.y;

    const float ang = -(180+src.rotSpin.y)*float(M_PI)/180.f;
    const float c   = std::cos(ang), s = std::sin(ang);

    float dx = lx*c-lz*s;
    float dz = lx*s+lz*c;

    view.set(1,0, dx*smWidthInv);
    view.set(1,1, dz*smWidthInv);
    }

  if(layer>0) {
    Tempest::Matrix4x4 proj;
    proj.identity();

    static float k = -0.4f;
    proj.set(1,3, k);
    proj.mul(view);
    view = proj;
    }

  auto inv = view;
  inv.inverse();
  Vec3 mid = {};
  inv.project(mid);
  view.translate(mid-src.target);

  Tempest::Matrix4x4 proj;
  proj.identity();

  switch(layer) {
    case 0:
      proj.translate(0.f, 0.8f, 0.5f);
      break;
    case 1: {
      proj.translate(0.f, 0.5f, 0.5f);
      break;
      }
    }

  proj.mul(view);
  return proj;
  }

Vec3 Camera::applyModPosition(const Vec3& pos) {
  const auto& def = cameraDef();

  Vec3 targetOffset = Vec3(def.targetOffsetX,
                           def.targetOffsetY,
                           def.targetOffsetZ);

  if(auto pl = Gothic::inst().player()) {
    Matrix4x4 rot;
    rot.identity();
    rot.rotateOY(90-pl->rotation());
    rot.project(targetOffset.x,targetOffset.y,targetOffset.z);
    }

  return pos + targetOffset;
  }

Vec3 Camera::applyModRotation(const Vec3& spin) {
  return spin;
  /*
  const auto& def = cameraDef();
  Vec3 rotOffset = {def.rotOffsetX,def.rotOffsetY,def.rotOffsetZ};
  if(camMod==Dialog)
    rotOffset = {}; // FIXME?
  return spin-rotOffset;
  */
  }

Vec3 Camera::calcTranslation(float dist) const {
  Vec3 tr  = {0,0,dist};
  auto mTr = mkRotation(src.rotSpin);
  mTr.inverse();
  mTr.project(tr);
  return tr;
  }

const Daedalus::GEngineClasses::CCamSys &Camera::cameraDef() const {
  auto& camd = Gothic::cameraDef();
  if(camMod==Dialog)
    return camd.dialogCam();
  if(lbEnable)
    return camd.backCam();
  if(fpEnable && (camMod==Normal || camMod==Melee))
    return camd.fpCam();
  if(camMod==Normal) {
    return camd.stdCam();
    }
  if(camMod==Inventory)
    return camd.inventoryCam();
  if(camMod==Melee)
    return camd.meleeCam();
  if(camMod==Ranged)
    return camd.rangeCam();
  if(camMod==Magic)
    return camd.mageCam();
  if(camMod==Swim)
    return camd.swimCam();
  if(camMod==Dive)
    return camd.diveCam();
  if(camMod==Mobsi) {
    std::string_view tag = "", pos = "";
    if(auto pl = Gothic::inst().player())
      if(auto inter = pl->interactive()) {
        tag = inter->schemeName();
        pos = inter->posSchemeName();
        }
    return camd.mobsiCam(tag,pos);
    }
  if(camMod==Death)
    return camd.deathCam();
  return camd.stdCam();
  }

void Camera::clampRange(float &zoom) {
  const auto& def = cameraDef();
  if(zoom>def.maxRange)
    zoom = def.maxRange;
  if(zoom<def.minRange)
    zoom = def.minRange;
  }

void Camera::implMove(Tempest::Event::KeyType key) {
  float dpos = 60.f;

  float k = -float(M_PI/180.0);
  float s = std::sin(src.rotSpin.x*k), c=std::cos(src.rotSpin.x*k);

  if(key==KeyEvent::K_A) {
    src.target.x+=dpos*c;
    src.target.z-=dpos*s;
    }
  if(key==KeyEvent::K_D) {
    src.target.x-=dpos*c;
    src.target.z+=dpos*s;
    }
  if(key==KeyEvent::K_W) {
    src.target.x-=dpos*s;
    src.target.z-=dpos*c;
    }
  if(key==KeyEvent::K_S){
    src.target.x+=dpos*s;
    src.target.z+=dpos*c;
    }
  if(auto world = Gothic::inst().world())
    src.target.y = world->physic()->landRay(src.target).v.y;
  }

void Camera::setPosition(float x, float y, float z) {
  dst.target = {x,y,z};
  src.target = dst.target;
  }

void Camera::setDestPosition(const Tempest::Vec3& pos) {
  dst.target = pos;
  }

void Camera::setDialogDistance(float d) {
  dlgDist = d;
  }

void Camera::followPos(Vec3& pos, Vec3 dest, bool inMove, float dtF) {
  const auto& def = cameraDef();
  auto        dp  = (dest-pos);
  auto        len = dp.length();

  if(len>0.1f && def.translate && inertiaTarget && camMod!=Dialog && camMod!=Mobsi){
    const float maxDist = 180;
    float       speed   = 0;
    if(inMove)
      speed = def.veloTrans*dtF; else
      speed = dp.length()*dtF*2.f;
    float       tr      = std::min(speed,len);
    if(len-tr>maxDist)
      tr = (len-maxDist);

    float k = tr/len;
    pos = Vec3(pos.x+dp.x*k, pos.y+dp.y*k, pos.z+dp.z*k);
    } else {
    pos = dest;
    }
  }

void Camera::followAng(Vec3& spin, Vec3 dest, float dtF) {
  const auto& def = cameraDef();
  followAng(spin.x,dest.x,def.veloRot,dtF);
  followAng(spin.y,dest.y,def.veloRot,dtF);
  }

void Camera::followAng(float& ang, float dest, float speed, float dtF) {
  float da    = angleMod(dest-ang);
  float shift = da*std::min(1.f,2.f*speed*dtF);
  if(std::abs(da)<shift) {
    ang = dest;
    return;
    }

  static const float min=-45, max=45;
  if(da>max+1.f) {
    shift = (da-max);
    }
  if(da<min-1.f) {
    shift = (da-min);
    }
  ang += shift;
  }

void Camera::tick(const Npc& npc, uint64_t dt, bool inMove, bool includeRot) {
  if(!hasPos) {
    dst    = src;
    hasPos = true;
    }

  if(Gothic::inst().isPause())
    return;

  const float dtF = float(dt)/1000.f;
  clampRange(dst.range);

  {
  const float zSpeed = 5.f;
  const float dz = dst.range-src.range;
  src.range+=dz*std::min(1.f,2.f*zSpeed*dtF);
  }

  /*
  if(includeRot) {
    src.rotSpin.x = dst.rotSpin.x;
    //src.spin.y = dst.spin.y;
    followAng(src.rotSpin.y, dst.rotSpin.y, def.veloRot, dtF);

    if(src.rotSpin.x>def.maxElevation)
      src.rotSpin.x = def.maxElevation;
    if(src.rotSpin.x<def.minElevation)
      ;//spin.x = def.minElevation;
    }
  */
  calcControlPoints(npc,inMove,dtF);
  }

void Camera::calcControlPoints(const Npc& npc, bool inMove, float dtF) {
  const auto& def = cameraDef();
  auto  targetOffset = Vec3(def.targetOffsetX,
                            def.targetOffsetY,
                            def.targetOffsetZ);
  auto  rotOffset    = Vec3(def.rotOffsetX,
                            def.rotOffsetY,
                            def.rotOffsetZ);

  auto  pos          = npc.cameraBone();

  followAng(src.rotSpin, dst.rotSpin, dtF);
  followAng(src.spin,    rotOffset,   dtF);

  Matrix4x4 rotOffsetMat;
  rotOffsetMat.identity();
  rotOffsetMat.rotateOY(180-src.rotSpin.y);
  rotOffsetMat.rotateOX(src.rotSpin.x);
  rotOffsetMat.project(targetOffset);

  rotOffsetMat.rotateOX(src.spin.x);
  rotOffsetMat.rotateOY(src.spin.y);
  rotOffsetMat.rotateOZ(src.spin.z);

  Vec3 dir = {0,0,1};
  rotOffsetMat.project(dir);

  dst.target = pos+targetOffset;
  dst.origin = dst.target - dir*dst.range*100.f;

  followPos(src.target,dst.target,inMove,dtF);
  src.origin = src.target - dir*src.range*100.f;
  }

Matrix4x4 Camera::mkView(const Vec3& pos, const Vec3& spin) const {
  static float scale = 0.0009f;

  Matrix4x4 view;
  view.identity();
  view.scale(-1,-1,-1);
  view.scale(scale);

  view.mul(mkRotation(spin));
  view.translate(-pos);

  return view;
  }

Matrix4x4 Camera::mkRotation(const Vec3& spin) const {
  Matrix4x4 view;
  view.identity();
  view.rotateOX(spin.x);
  view.rotateOY(spin.y);
  view.rotateOZ(spin.z);
  return view;
  }

void Camera::debugDraw(DbgPainter& p) {
  if(!dbg)
    return;

  auto destP = applyModPosition(dst.target);

  p.setPen(Color(0,1,0));
  p.drawLine(destP, src.target);

  auto& fnt = Resources::font();
  int   y   = 300+fnt.pixelSize();
  char buf[256] = {};

  std::snprintf(buf,sizeof(buf),"RaysCasted : %d", raysCasted);
  p.drawText(8,y,buf); y += fnt.pixelSize();

  std::snprintf(buf,sizeof(buf),"PlayerPos : %f %f %f", dst.target.x, dst.target.y, dst.target.z);
  p.drawText(8,y,buf); y += fnt.pixelSize();

  std::snprintf(buf,sizeof(buf),"Range To Player : %f", src.range*100.f);
  p.drawText(8,y,buf); y += fnt.pixelSize();

  std::snprintf(buf,sizeof(buf),"Azimuth : %f", angleMod(dst.rotSpin.y-src.rotSpin.y));
  p.drawText(8,y,buf); y += fnt.pixelSize();
  std::snprintf(buf,sizeof(buf),"Elevation : %f", src.rotSpin.x);
  p.drawText(8,y,buf); y += fnt.pixelSize();
  }

PointF Camera::spin() const {
  return PointF(src.rotSpin.x,src.rotSpin.y);
  }

PointF Camera::destSpin() const {
  return PointF(dst.rotSpin.x,dst.rotSpin.y);
  }

Matrix4x4 Camera::viewProj() const {
  Matrix4x4 ret=projective();
  ret.mul(view());
  return ret;
  }

Matrix4x4 Camera::view() const {
  auto view = mkView(src.origin,src.rotSpin);
  return view;
  /*
  const auto& def     = cameraDef();
  float       dist    = 0;

  if(camMod==Mobsi) {
    dist = def.maxRange;
    dist*=100; //to santimeters
    }
  else if(camMod==Dialog) {
    dist = dlgDist;
    }
  else {
    dist = state.range;
    if(dist<def.minRange)
      dist = def.minRange;
    if(dist>def.maxRange)
      dist = def.maxRange;
    dist*=100; //to santimeters
    }

  Matrix4x4 view=projective();
  view.mul(mkView(state.pos,dist));

  float distMd = dist;
  if(fpEnable && (camMod==Normal || camMod==Melee))
    distMd = 0;
  if(def.collision)
    distMd = calcCameraColision(view,dist);
  view=mkView(state.pos,distMd);
  return view;
  */
  }

float Camera::calcCameraColision(const Matrix4x4& view, const float dist) const {
  auto world = Gothic::inst().world();
  if(world==nullptr)
    return dist;

  float minDist = 20;
  float padding = 20;

  auto& physic = *world->physic();

  Matrix4x4 vinv=view;
  vinv.inverse();

  raysCasted = 0;
  float distMd = dist;
  auto  tr     = calcTranslation(dist);
  static int n = 1, nn=1;
  for(int i=-n;i<=n;++i)
    for(int r=-n;r<=n;++r) {
      float u = float(i)/float(nn),v = float(r)/float(nn);
      Tempest::Vec3 r0 = src.target;
      Tempest::Vec3 r1 = {u,v,0};

      vinv.project(r1.x,r1.y,r1.z);

      auto rc = physic.ray(r0, r1);
      auto d  = rc.v;
      d -=r0;
      r1-=r0;

      float dist0 = r1.length();
      float dist1 = Vec3::dotProduct(d,tr)/dist;
      if(rc.hasCol)
        dist1 = std::max<float>(0,dist1-padding);

      float md = dist-std::max(0.f,dist0-dist1);
      if(md<distMd)
        distMd=md;
      raysCasted++;
      }
  return std::max(minDist,distMd);
  }
