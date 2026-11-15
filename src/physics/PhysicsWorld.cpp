#include "physics/PhysicsWorld.h"
#include <cstdio>
#include <cstring>

namespace phys {

// ------------------------------------------------------------------ math impl
Mat3 matFromQuat(const Quat& q){
    float xx=q.x*q.x,yy=q.y*q.y,zz=q.z*q.z;
    float xy=q.x*q.y,xz=q.x*q.z,yz=q.y*q.z;
    float wx=q.w*q.x,wy=q.w*q.y,wz=q.w*q.z;
    Mat3 r;
    r.m[0]=1-2*(yy+zz); r.m[1]=2*(xy-wz);   r.m[2]=2*(xz+wy);
    r.m[3]=2*(xy+wz);   r.m[4]=1-2*(xx+zz); r.m[5]=2*(yz-wx);
    r.m[6]=2*(xz-wy);   r.m[7]=2*(yz+wx);   r.m[8]=1-2*(xx+yy);
    return r;
}
Vec3 mv(const Mat3& M,const Vec3& v){
    return{M.m[0]*v.x+M.m[1]*v.y+M.m[2]*v.z,
           M.m[3]*v.x+M.m[4]*v.y+M.m[5]*v.z,
           M.m[6]*v.x+M.m[7]*v.y+M.m[8]*v.z};}
Vec3 mtv(const Mat3& M,const Vec3& v){
    return{M.m[0]*v.x+M.m[3]*v.y+M.m[6]*v.z,
           M.m[1]*v.x+M.m[4]*v.y+M.m[7]*v.z,
           M.m[2]*v.x+M.m[5]*v.y+M.m[8]*v.z};}

// ------------------------------------------------------------------ helpers
void PhysicsWorld::InitMass(Body& b){
    const MatDef& m=MATS[b.mat];
    b.mass=m.density*8.f*b.half.x*b.half.y*b.half.z;
    b.invMass=1.f/b.mass;
    float hx2=b.half.x*b.half.x,hy2=b.half.y*b.half.y,hz2=b.half.z*b.half.z;
    b.invIlocal={3.f/(b.mass*(hy2+hz2)),
                 3.f/(b.mass*(hx2+hz2)),
                 3.f/(b.mass*(hx2+hy2))};
}
void PhysicsWorld::Refresh(Body& b){
    b.R=matFromQuat(b.q);
    const float d[3]={b.invIlocal.x,b.invIlocal.y,b.invIlocal.z};
    for(int r=0;r<3;r++)
        for(int c=0;c<3;c++){
            float s=0;
            for(int k=0;k<3;k++) s+=b.R.m[r*3+k]*d[k]*b.R.m[c*3+k];
            b.invIw.m[r*3+c]=s;
        }
}
void PhysicsWorld::PointImpulse(Body& b,const Vec3& p,const Vec3& J){
    Refresh(b);
    b.vel+=J*b.invMass;
    b.w+=mv(b.invIw,cross(p-b.pos,J));
    b.asleep=false; b.sleepT=0;
}
float PhysicsWorld::InvEff(const Body& X,const Vec3& r,const Vec3& d){
    Vec3 t=mv(X.invIw,cross(r,d));
    return X.invMass+dot(cross(t,r),d);
}
void PhysicsWorld::ApplyJ(Body& A,Body* B,
                          const Vec3& rA,const Vec3& rB,const Vec3& J){
    A.vel+=J*A.invMass;
    A.w+=mv(A.invIw,cross(rA,J));
    if(B){ B->vel-=J*B->invMass; B->w-=mv(B->invIw,cross(rB,J)); }
}
void PhysicsWorld::WakeNear(Vec3 p,float r){
    for(Body& b:bodies){
        Vec3 d=b.pos-p;
        if(dot(d,d)<r*r){b.asleep=false;b.sleepT=0;}
    }
}

// ------------------------------------------------------------------ terrain
Vec3 PhysicsWorld::TerrainNormal(float x,float z) const{
    if(!terrainH) return{0,1,0};
    const float e=0.5f;
    return vnorm({terrainH(x-e,z)-terrainH(x+e,z),
                  2*e,
                  terrainH(x,z-e)-terrainH(x,z+e)});
}
void PhysicsWorld::TerrainContacts(int i){
    if(!terrainH) return;
    Body& b=bodies[i];
    float rad=vlen(b.half);
    float hc=terrainH(b.pos.x,b.pos.z);
    if(b.pos.y-rad>hc+rad*2.f+1.f) return;
    for(int s=0;s<8;s++){
        Vec3 c{(s&1?1.f:-1.f)*b.half.x,
               (s&2?1.f:-1.f)*b.half.y,
               (s&4?1.f:-1.f)*b.half.z};
        Vec3 p=b.pos+mv(b.R,c);
        float h=terrainH(p.x,p.z);
        if(p.y<h) contacts.push_back({i,-1,p,TerrainNormal(p.x,p.z),h-p.y});
    }
}

// ------------------------------------------------------------------ OBB-OBB
void PhysicsWorld::PairContacts(int ia,int ib){
    const Body& A=bodies[ia];
    const Body& B=bodies[ib];
    const Vec3 a[3]={{A.R.m[0],A.R.m[3],A.R.m[6]},
                     {A.R.m[1],A.R.m[4],A.R.m[7]},
                     {A.R.m[2],A.R.m[5],A.R.m[8]}};
    const Vec3 b[3]={{B.R.m[0],B.R.m[3],B.R.m[6]},
                     {B.R.m[1],B.R.m[4],B.R.m[7]},
                     {B.R.m[2],B.R.m[5],B.R.m[8]}};
    const float hA[3]={A.half.x,A.half.y,A.half.z};
    const float hB[3]={B.half.x,B.half.y,B.half.z};
    Vec3 t=B.pos-A.pos;

    auto radius=[](const Vec3* ax,const float* h,const Vec3& L){
        return h[0]*fabsf(dot(ax[0],L))+h[1]*fabsf(dot(ax[1],L))+h[2]*fabsf(dot(ax[2],L));};

    float best=-1e9f; int type=-1,bi=0,bj=0; Vec3 bestL;
    for(int i=0;i<3;i++){
        float sep=fabsf(dot(t,a[i]))-radius(a,hA,a[i])-radius(b,hB,a[i]);
        if(sep>0) return;
        if(sep>best){best=sep;type=0;bi=i;bestL=a[i];}
    }
    for(int j=0;j<3;j++){
        float sep=fabsf(dot(t,b[j]))-radius(a,hA,b[j])-radius(b,hB,b[j]);
        if(sep>0) return;
        if(sep>best){best=sep;type=1;bi=j;bestL=b[j];}
    }
    float bestFace=best;
    for(int i=0;i<3;i++)
        for(int j=0;j<3;j++){
            Vec3 L=cross(a[i],b[j]);
            float l=vlen(L); if(l<1e-4f) continue;
            L*=1.f/l;
            float sep=fabsf(dot(t,L))-radius(a,hA,L)-radius(b,hB,L);
            if(sep>0) return;
            if(sep>best&&sep>bestFace+0.01f){best=sep;type=2;bi=i;bj=j;bestL=L;}
        }

    Vec3 nAB=bestL;
    if(dot(nAB,t)<0) nAB=nAB*-1.f;
    Vec3 nBA=nAB*-1.f;

    if(type==2){  // edge-edge
        Vec3 pa=A.pos,pb=B.pos;
        for(int k=0;k<3;k++){
            if(k!=bi) pa=pa+a[k]*(hA[k]*(dot(a[k],nAB)>0?1.f:-1.f));
            if(k!=bj) pb=pb+b[k]*(hB[k]*(dot(b[k],nAB)>0?-1.f:1.f));
        }
        Vec3 r=pa-pb;
        float bb=dot(a[bi],b[bj]),cc=dot(a[bi],r),ff=dot(b[bj],r);
        float den=1.f-bb*bb;
        if(fabsf(den)<1e-6f) return;
        float s=(bb*ff-cc)/den,tt=(ff-bb*cc)/den;
        Vec3 p=(pa+a[bi]*s+pb+b[bj]*tt)*0.5f;
        contacts.push_back({ia,ib,p,nBA,-best});
        return;
    }

    // face-face clipping
    const Body& ref=type==0?A:B;
    const Body& inc=type==0?B:A;
    const Vec3* rax=type==0?a:b;
    const Vec3* iax=type==0?b:a;
    const float* rh=type==0?hA:hB;
    const float* ih=type==0?hB:hA;
    Vec3 nRef=type==0?nAB:nBA;
    int k=bi;
    int mm=0; float md=-1;
    for(int q=0;q<3;q++){float d=fabsf(dot(iax[q],nRef));if(d>md){md=d;mm=q;}}
    Vec3 faceN=iax[mm]*(dot(iax[mm],nRef)>0?-1.f:1.f);
    Vec3 fc=inc.pos+faceN*ih[mm];
    int p1=(mm+1)%3,p2=(mm+2)%3;
    std::vector<Vec3> poly={
        fc+iax[p1]*ih[p1]+iax[p2]*ih[p2],
        fc-iax[p1]*ih[p1]+iax[p2]*ih[p2],
        fc-iax[p1]*ih[p1]-iax[p2]*ih[p2],
        fc+iax[p1]*ih[p1]-iax[p2]*ih[p2]};

    for(int side=0;side<4;side++){
        int u=(k+1+(side>>1))%3;
        float sg=(side&1)?-1.f:1.f;
        Vec3 pn=rax[u]*sg;
        float pd=dot(ref.pos,pn)+rh[u];
        std::vector<Vec3> out;
        for(size_t i=0;i<poly.size();i++){
            Vec3 P=poly[i],Q=poly[(i+1)%poly.size()];
            float dP=dot(P,pn)-pd,dQ=dot(Q,pn)-pd;
            if(dP<=0) out.push_back(P);
            if((dP<0&&dQ>0)||(dP>0&&dQ<0))
                out.push_back(P+(Q-P)*(dP/(dP-dQ)));
        }
        poly.swap(out);
        if(poly.empty()) return;
    }
    float planeD=dot(ref.pos,nRef)+rh[k];
    for(const Vec3& P:poly){
        float depth=planeD-dot(P,nRef);
        if(depth>-0.002f)
            contacts.push_back({ia,ib,P,nBA,std::max(depth,0.f)});
    }
}

// ------------------------------------------------------------------ break
void PhysicsWorld::BreakBody(int idx,float dv){
    Body src=bodies[idx];
    bodies[idx].alive=false;
    if(heldId==src.id) heldId=0;
    const MatDef& m=MATS[src.mat];
    printf("[phys] BROKEN: %s (dv=%.1f m/s)\n",m.name,dv);
    WakeNear(src.pos,4.f);
    Vec3 cell{src.half.x*2.f/m.nx,src.half.y*2.f/m.ny,src.half.z*2.f/m.nz};
    float burst=std::max(0.8f,std::min(5.f,dv*0.3f));
    for(int ix=0;ix<m.nx;ix++)
        for(int iy=0;iy<m.ny;iy++)
            for(int iz=0;iz<m.nz;iz++){
                Vec3 lc{-src.half.x+cell.x*(ix+0.5f),
                        -src.half.y+cell.y*(iy+0.5f),
                        -src.half.z+cell.z*(iz+0.5f)};
                Vec3 r=mv(src.R,lc);
                Body f;
                f.id=nextId++; f.mat=src.mat; f.fragment=true;
                f.half={cell.x*0.46f,cell.y*0.46f,cell.z*0.46f};
                f.pos=src.pos+r; f.q=src.q;
                Vec3 dir=vnorm(r+Vec3{frand()-0.5f,frand()-0.3f,frand()-0.5f}*(vlen(cell)*0.5f));
                f.vel=src.vel+cross(src.w,r)+dir*(burst*(0.4f+frand()));
                f.w=src.w+Vec3{frand()-0.5f,frand()-0.5f,frand()-0.5f}*8.f;
                float v=0.88f+frand()*0.2f;
                f.cr=src.cr*v; f.cg=src.cg*v; f.cb=src.cb*v;
                f.life=18.f+frand()*8.f;
                InitMass(f); Refresh(f);
                bodies.push_back(f);
            }
}

// ------------------------------------------------------------------ step
void PhysicsWorld::Step(float dt){
    const float rr=simRange*simRange;
    for(Body& b:bodies){
        if(!b.alive) continue;
        Vec3 d=b.pos-eye;
        b.active=(d.x*d.x+d.z*d.z)<rr;
        b.hit=false; b.corr={0,0,0}; b.corrN=0; b.jAccum={0,0,0};
        if(!b.active) continue;
        Refresh(b);
        b.age+=dt;
        if(b.asleep) continue;
        if(b.id==heldId){
            Vec3 target=eye+fwd*holdDist;
            Vec3 v=(target-b.pos)*12.f;
            float l=vlen(v);
            if(l>14.f) v*=14.f/l;
            b.vel=v;
            b.w*=(1.f-8.f*dt);
        } else {
            b.vel.y-=9.81f*dt;
            b.vel*=(1.f-0.02f*dt);
            b.w*=(1.f-0.3f*dt);
        }
        float sp=vlen(b.vel);
        if(sp>60.f) b.vel*=60.f/sp;
        float aw=vlen(b.w);
        if(aw>40.f) b.w*=40.f/aw;
        b.vPre=b.vel;
    }

    // contacts
    contacts.clear();
    int n=(int)bodies.size();
    for(int i=0;i<n;i++){
        Body& a=bodies[i];
        if(!a.alive||!a.active) continue;
        if(!a.asleep) TerrainContacts(i);
        float ra=vlen(a.half);
        for(int j=i+1;j<n;j++){
            Body& b=bodies[j];
            if(!b.alive||!b.active||(a.asleep&&b.asleep)) continue;
            Vec3 d=a.pos-b.pos;
            float rs=ra+vlen(b.half);
            if(dot(d,d)>rs*rs) continue;
            PairContacts(i,j);
        }
    }

    // prepare contacts: restitution, friction, tangent axes, warm-start
    for(Contact& c:contacts){
        Body& A=bodies[c.a];
        Body* B=c.b>=0?&bodies[c.b]:nullptr;
        if(B){ c.e=std::min(MATS[A.mat].restitution,MATS[B->mat].restitution);
               c.mu=sqrtf(MATS[A.mat].friction*MATS[B->mat].friction);}
        else { c.e=MATS[A.mat].restitution;
               c.mu=sqrtf(MATS[A.mat].friction*0.8f);}
        Vec3 rA=c.p-A.pos, rB=B?c.p-B->pos:Vec3{};
        Vec3 rv=(A.vel+cross(A.w,rA))-(B?B->vel+cross(B->w,rB):Vec3{});
        float vn=dot(rv,c.n);
        c.bounce=(-vn>1.0f)?-c.e*vn:0.f;
        Vec3 ref=fabsf(c.n.y)<0.9f?Vec3{0,1,0}:Vec3{1,0,0};
        c.t1=vnorm(cross(c.n,ref));
        c.t2=cross(c.n,c.t1);
        c.ida=A.id; c.idb=B?B->id:-1;
        // warm-start from previous frame
        for(const Contact& pc:prevContacts){
            if(pc.ida!=c.ida||pc.idb!=c.idb) continue;
            Vec3 d=pc.p-c.p;
            if(dot(d,d)>0.0025f) continue;
            c.jn=pc.jn; c.jt1=pc.jt1; c.jt2=pc.jt2;
            Vec3 J=c.n*c.jn+c.t1*c.jt1+c.t2*c.jt2;
            ApplyJ(A,B,rA,rB,J);
            break;
        }
    }

    // sequential impulse solver
    int nc=(int)contacts.size();
    for(int it=0;it<ITER;it++)
        for(int ci=0;ci<nc;ci++){
            Contact& c=contacts[ci];
            Body& A=bodies[c.a];
            Body* B=c.b>=0?&bodies[c.b]:nullptr;
            Vec3 rA=c.p-A.pos, rB=B?c.p-B->pos:Vec3{};
            auto relVel=[&](){
                return (A.vel+cross(A.w,rA))-(B?B->vel+cross(B->w,rB):Vec3{});};
            // friction
            for(int k=0;k<2;k++){
                const Vec3& t=k?c.t2:c.t1;
                float& jt=k?c.jt2:c.jt1;
                float denom=InvEff(A,rA,t)+(B?InvEff(*B,rB,t):0.f);
                float dj=-dot(relVel(),t)/denom;
                float lim=c.mu*c.jn;
                float nj=std::max(-lim,std::min(lim,jt+dj));
                dj=nj-jt; jt=nj;
                ApplyJ(A,B,rA,rB,t*dj);
            }
            // normal
            float denom=InvEff(A,rA,c.n)+(B?InvEff(*B,rB,c.n):0.f);
            float dj=(c.bounce-dot(relVel(),c.n))/denom;
            float nj=std::max(0.f,c.jn+dj);
            dj=nj-c.jn; c.jn=nj;
            ApplyJ(A,B,rA,rB,c.n*dj);
        }
    prevContacts=contacts;
    for(const Contact& c:contacts)
        if(c.jn>1e-6f){
            bodies[c.a].hit=true; bodies[c.a].jAccum+=c.n*c.jn;
            if(c.b>=0){bodies[c.b].hit=true; bodies[c.b].jAccum-=c.n*c.jn;}
        }

    // position correction
    for(const Contact& c:contacts){
        float depth=c.depth-0.005f;
        if(depth<=0) continue;
        Body& A=bodies[c.a];
        if(c.b>=0){
            Body& B=bodies[c.b];
            float tot=A.invMass+B.invMass;
            A.corr+=c.n*(depth*0.25f*A.invMass/tot); A.corrN++;
            B.corr-=c.n*(depth*0.25f*B.invMass/tot); B.corrN++;
        } else {
            A.corr+=c.n*(depth*0.25f); A.corrN++;
        }
    }

    // integrate, sleep, break
    std::vector<std::pair<int,float>> toBreak;
    for(int i=0;i<n;i++){
        Body& b=bodies[i];
        if(!b.alive||!b.active) continue;
        if(b.asleep){
            if(vlen(b.vel)>0.2f||vlen(b.w)>0.4f){b.asleep=false;b.sleepT=0;}
            else {b.vel={0,0,0};b.w={0,0,0};continue;}
        }
        if(b.hit&&b.id!=heldId&&vlen(b.vel)<0.6f){b.vel*=0.99f;b.w*=0.95f;}
        if(b.corrN>0) b.pos+=b.corr*(1.f/b.corrN);
        b.pos+=b.vel*dt;
        Quat wq{0,b.w.x,b.w.y,b.w.z};
        Quat dq=qmul(wq,b.q);
        b.q=qnorm({b.q.w+0.5f*dt*dq.w,b.q.x+0.5f*dt*dq.x,
                   b.q.y+0.5f*dt*dq.y,b.q.z+0.5f*dt*dq.z});

        if(b.id!=heldId&&vlen(b.vel)<0.2f&&vlen(b.w)<0.4f){
            b.sleepT+=dt;
            if(b.sleepT>0.4f){b.asleep=true;b.vel={0,0,0};b.w={0,0,0};}
        } else b.sleepT=0;

        if(b.hit&&!b.fragment&&b.id!=heldId&&b.age>0.4f){
            float dv=vlen(b.jAccum)*b.invMass;
            if(dv>MATS[b.mat].breakDv) toBreak.push_back({i,dv});
        }
    }
    for(auto& tb:toBreak) BreakBody(tb.first,tb.second);
}

// ------------------------------------------------------------------ Update
void PhysicsWorld::Update(float dt){
    accum+=std::min(dt,0.05f);
    int steps=0;
    while(accum>=STEP&&steps<6){Step(STEP);accum-=STEP;steps++;}
    if(accum>STEP) accum=0.f;

    for(Body& b:bodies)
        if(b.alive&&b.fragment){b.life-=dt;if(b.life<=0)b.alive=false;}

    // trim oldest fragments above maxBodies
    int cnt=0;
    for(Body& b:bodies) if(b.alive) cnt++;
    while(cnt>maxBodies){
        Body* o=nullptr;
        for(Body& b:bodies)
            if(b.alive&&b.fragment&&(!o||b.life<o->life)) o=&b;
        if(!o) break;
        o->alive=false; cnt--;
    }
    bodies.erase(std::remove_if(bodies.begin(),bodies.end(),
        [](const Body& b){return !b.alive;}),bodies.end());
    if(heldId&&!FindById(heldId)) heldId=0;
}

// ------------------------------------------------------------------ spawn
int PhysicsWorld::Spawn(int mat,Vec3 pos,Quat q,Vec3 vel){
    Body b;
    b.id=nextId++; b.mat=mat; b.pos=pos; b.q=qnorm(q); b.vel=vel;
    b.half=MATS[mat].half;
    b.cr=MATS[mat].r; b.cg=MATS[mat].g; b.cb=MATS[mat].b;
    InitMass(b); Refresh(b);
    bodies.push_back(b);
    return b.id;
}
void PhysicsWorld::SpawnWall(Vec3 base,float yaw){
    if(!terrainH) return;
    float hx=MATS[0].half.x, st=hx*2.04f;
    Vec3 right{cosf(yaw),0,sinf(yaw)};
    float gy=-1e9f;
    for(int c=-2;c<=2;c++){
        Vec3 p=base+right*(c*st);
        gy=std::max(gy,terrainH(p.x,p.z));
    }
    for(int row=0;row<4;row++)
        for(int col=0;col<3;col++){
            Vec3 p=base+right*((col-1)*st);
            p.y=gy+hx+0.03f+row*st;
            Spawn(0,p,quatY(-yaw),{0,0,0});
        }
}

// ------------------------------------------------------------------ raycast
int PhysicsWorld::Raycast(Vec3 o,Vec3 d,float maxT,float& tHit){
    int best=-1; float bt=maxT;
    for(int i=0;i<(int)bodies.size();i++){
        Body& b=bodies[i];
        if(!b.alive) continue;
        Refresh(b);
        Vec3 lo=mtv(b.R,o-b.pos), ld=mtv(b.R,d);
        const float h[3]={b.half.x,b.half.y,b.half.z};
        const float po[3]={lo.x,lo.y,lo.z}, pd[3]={ld.x,ld.y,ld.z};
        float t0=0,t1=bt; bool ok=true;
        for(int k=0;k<3&&ok;k++){
            if(fabsf(pd[k])<1e-8f){ if(fabsf(po[k])>h[k]) ok=false; }
            else{
                float a=(-h[k]-po[k])/pd[k], c=(h[k]-po[k])/pd[k];
                if(a>c) std::swap(a,c);
                t0=std::max(t0,a); t1=std::min(t1,c);
                if(t0>t1) ok=false;
            }
        }
        if(ok&&t0<bt){bt=t0;best=i;}
    }
    tHit=bt; return best;
}

// ------------------------------------------------------------------ interaction
Body* PhysicsWorld::FindById(int id){
    if(!id) return nullptr;
    for(Body& b:bodies) if(b.alive&&b.id==id) return &b;
    return nullptr;
}
Body* PhysicsWorld::Held(){ return FindById(heldId); }

int PhysicsWorld::Grab(){
    float t;
    int i=Raycast(eye,fwd,5.f,t);
    if(i<0) return 2;
    if(bodies[i].mass>120.f) return 1;
    heldId=bodies[i].id;
    holdDist=std::max(1.2f,std::min(4.f,t));
    bodies[i].asleep=false; bodies[i].sleepT=0;
    WakeNear(bodies[i].pos,1.5f);
    return 0;
}
void PhysicsWorld::Release(){ heldId=0; }

void PhysicsWorld::ThrowHeld(Vec3 playerVel){
    Body* b=FindById(heldId);
    heldId=0;
    if(!b) return;
    float speed=20.f/(1.f+b->mass/40.f);
    b->vel=fwd*speed+playerVel;
    b->w={frand()-0.5f,frand()-0.5f,frand()-0.5f};
    b->w*=6.f;
    b->asleep=false; b->sleepT=0;
}
int PhysicsWorld::Strike(){
    float t;
    int i=Raycast(eye,fwd,6.f,t);
    if(i<0) return -1;
    Body& b=bodies[i];
    Vec3 J=fwd*450.f; J.y+=60.f;
    PointImpulse(b,eye+fwd*t,J);
    WakeNear(b.pos,3.f);
    float dv=vlen(J)*b.invMass;
    if(!b.fragment&&dv>MATS[b.mat].breakDv) BreakBody(i,dv);
    return i;
}

} // namespace phys
