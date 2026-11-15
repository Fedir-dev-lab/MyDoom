#pragma once
// ---------------------------------------------------------------------------
// PhysicsWorld — OBB rigid-body physics ported from physics.hpp.
// No external libraries. Units: metres / kg / seconds.
// Fixed substep 1/120 s, impulse solver with warm-starting, sleeping,
// breakable bodies, grab/throw/strike player interaction.
// ---------------------------------------------------------------------------
#include <cmath>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace phys {

// ------------------------------------------------------------------ math
struct Vec3 { float x=0,y=0,z=0; };
inline Vec3 operator+(const Vec3& a,const Vec3& b){return{a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 operator-(const Vec3& a,const Vec3& b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 operator*(const Vec3& a,float s)      {return{a.x*s,a.y*s,a.z*s};}
inline Vec3& operator+=(Vec3& a,const Vec3& b){a.x+=b.x;a.y+=b.y;a.z+=b.z;return a;}
inline Vec3& operator-=(Vec3& a,const Vec3& b){a.x-=b.x;a.y-=b.y;a.z-=b.z;return a;}
inline Vec3& operator*=(Vec3& a,float s)      {a.x*=s;a.y*=s;a.z*=s;return a;}
inline float dot(const Vec3& a,const Vec3& b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3  cross(const Vec3& a,const Vec3& b){
    return{a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};}
inline float vlen(const Vec3& a){return sqrtf(dot(a,a));}
inline Vec3  vnorm(const Vec3& a){float l=vlen(a);return l>1e-8f?a*(1.f/l):Vec3{0,1,0};}
inline float frand(){return rand()/(float)RAND_MAX;}

struct Quat{float w=1,x=0,y=0,z=0;};
inline Quat qmul(const Quat& a,const Quat& b){
    return{a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
           a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
           a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
           a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};}
inline Quat qnorm(Quat q){
    float l=sqrtf(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
    if(l<1e-8f)return Quat();
    float k=1.f/l;return{q.w*k,q.x*k,q.y*k,q.z*k};}
inline Quat quatY(float a){return{cosf(a*0.5f),0,sinf(a*0.5f),0};}

struct Mat3{float m[9]={1,0,0,0,1,0,0,0,1};};
Mat3  matFromQuat(const Quat& q);
Vec3  mv (const Mat3& M,const Vec3& v);   // M*v
Vec3  mtv(const Mat3& M,const Vec3& v);   // M^T*v

// ------------------------------------------------------------------ materials
struct MatDef{
    const char* name;
    Vec3  half;        // half-extents (m)
    float density;     // kg/m³
    float restitution;
    float friction;
    float breakDv;     // break threshold (m/s velocity-change)
    float r,g,b;
    int   nx,ny,nz;    // fragment grid
    bool  glass;
};

// 0=crate  1=glass  2=stone
static const MatDef MATS[3]={
    {"wooden crate",{0.25f,0.25f,0.25f},300.f, 0.25f,0.6f, 9.0f, 0.62f,0.42f,0.22f, 2,2,2,false},
    {"glass block", {0.10f,0.20f,0.10f},600.f, 0.10f,0.4f, 3.5f, 0.60f,0.85f,0.90f, 2,3,2,true },
    {"stone block", {0.15f,0.15f,0.15f},2400.f,0.20f,0.7f,14.0f, 0.50f,0.50f,0.52f, 2,2,2,false},
};

// ------------------------------------------------------------------ Body
struct Body{
    int   id=0,mat=0;
    bool  fragment=false,alive=true,asleep=false,active=true,hit=false;
    Vec3  pos,vel,w,half,invIlocal,corr,vPre;
    Quat  q;
    Mat3  R,invIw;
    float mass=1,invMass=1,sleepT=0,age=0,life=0;
    int   corrN=0;
    Vec3  jAccum;
    float cr=1,cg=1,cb=1;
};

// ------------------------------------------------------------------ Contact
struct Contact{
    int   a,b;          // b==-1 → terrain
    Vec3  p,n;
    float depth;
    float jn=0,jt1=0,jt2=0;
    float bounce=0,e=0,mu=0;
    Vec3  t1,t2;
    int   ida=0,idb=-1;
};

// ------------------------------------------------------------------ World
class PhysicsWorld{
public:
    // Set this before the first Update call.
    float (*terrainH)(float,float) = nullptr;

    std::vector<Body>    bodies;
    std::vector<Contact> contacts, prevContacts;

    int   nextId   = 1;
    int   heldId   = 0;
    float holdDist = 2.2f;
    float accum    = 0.f;
    int   maxBodies= 500;
    float simRange = 300.f;

    // Set every frame by the caller (player camera).
    Vec3  eye, fwd{0,0,-1};

    static constexpr float STEP = 1.f/120.f;
    static constexpr int   ITER = 6;

    // --- public API ---
    int   Spawn(int mat, Vec3 pos, Quat q, Vec3 vel);
    void  SpawnWall(Vec3 base, float yaw);  // 3×4 crate wall
    void  Update(float dt);

    // Interaction
    int   Grab();           // 0=ok 1=too heavy 2=nothing
    void  Release();
    void  ThrowHeld(Vec3 playerVel);
    int   Strike();         // returns hit body index or -1

    Body* Held();
    Body* FindById(int id);

    // Raycast against all alive bodies; returns index or -1
    int   Raycast(Vec3 o, Vec3 d, float maxT, float& tHit);

private:
    static void InitMass(Body& b);
    static void Refresh(Body& b);
    static void PointImpulse(Body& b, const Vec3& p, const Vec3& J);
    static float InvEff(const Body& X, const Vec3& r, const Vec3& d);
    static void  ApplyJ(Body& A, Body* B,
                        const Vec3& rA, const Vec3& rB, const Vec3& J);

    Vec3 TerrainNormal(float x, float z) const;
    void TerrainContacts(int i);
    void PairContacts(int ia, int ib);
    void BreakBody(int idx, float dv);
    void WakeNear(Vec3 p, float r);
    void Step(float dt);
};

} // namespace phys
