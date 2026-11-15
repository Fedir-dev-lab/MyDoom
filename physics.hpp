// physics.hpp - фізика твердих тіл (C++17, без сторонніх бібліотек)
//
// Що вміє:
//  * тіла - орієнтовані паралелепіпеди (OBB) з масою, тензором інерції, обертанням;
//  * зіткнення з рельєфом (будь-яка функція висоти) та тіл між собою (кути в об'ємі іншого тіла);
//  * імпульсний солвер: відскок (restitution), тертя Кулона, позиційна корекція;
//  * сон (sleeping) - нерухомі тіла не рахуються;
//  * підняти тіло (спрямоване "тримання" до точки перед камерою), кинути (з урахуванням маси),
//    вдарити молотом;
//  * руйнування: якщо різка зміна швидкості тіла при ударі перевищує поріг матеріалу,
//    тіло розпадається на уламки, які успадковують швидкість і обертання.
//
// Одиниці: метри, кілограми, секунди. Фіксований крок 1/120 с.

#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace phys {

// ------------------------------------------------------------------ математика
struct Vec3 { float x = 0, y = 0, z = 0; };
inline Vec3 operator+(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator*(const Vec3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline Vec3& operator+=(Vec3& a, const Vec3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline Vec3& operator-=(Vec3& a, const Vec3& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline Vec3& operator*=(Vec3& a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline float len(const Vec3& a) { return sqrtf(dot(a, a)); }
inline Vec3 norm(const Vec3& a) { float l = len(a); return l > 1e-8f ? a * (1.f / l) : Vec3{ 0, 1, 0 }; }
inline float frand() { return rand() / (float)RAND_MAX; }

struct Quat { float w = 1, x = 0, y = 0, z = 0; };
inline Quat qmul(const Quat& a, const Quat& b) {
    return { a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
             a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
             a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
             a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w };
}
inline Quat qnorm(Quat q) {
    float l = sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (l < 1e-8f) return Quat();
    float k = 1.f / l;
    return { q.w * k, q.x * k, q.y * k, q.z * k };
}
inline Quat quatY(float a) { return { cosf(a * 0.5f), 0, sinf(a * 0.5f), 0 }; }

struct Mat3 { float m[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }; }; // рядки: m[r*3+c]
inline Mat3 matFromQuat(const Quat& q) {
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat3 r;
    r.m[0] = 1 - 2 * (yy + zz); r.m[1] = 2 * (xy - wz);     r.m[2] = 2 * (xz + wy);
    r.m[3] = 2 * (xy + wz);     r.m[4] = 1 - 2 * (xx + zz); r.m[5] = 2 * (yz - wx);
    r.m[6] = 2 * (xz - wy);     r.m[7] = 2 * (yz + wx);     r.m[8] = 1 - 2 * (xx + yy);
    return r;
}
inline Vec3 mv(const Mat3& M, const Vec3& v) {   // M * v
    return { M.m[0] * v.x + M.m[1] * v.y + M.m[2] * v.z,
             M.m[3] * v.x + M.m[4] * v.y + M.m[5] * v.z,
             M.m[6] * v.x + M.m[7] * v.y + M.m[8] * v.z };
}
inline Vec3 mtv(const Mat3& M, const Vec3& v) {  // M^T * v
    return { M.m[0] * v.x + M.m[3] * v.y + M.m[6] * v.z,
             M.m[1] * v.x + M.m[4] * v.y + M.m[7] * v.z,
             M.m[2] * v.x + M.m[5] * v.y + M.m[8] * v.z };
}

// ------------------------------------------------------------------ матеріали
struct MatDef {
    const char* name;
    Vec3  half;          // піврозміри (м)
    float density;       // кг/м^3 (з урахуванням порожнин)
    float restitution;   // пружність
    float friction;      // тертя
    float breakDv;       // поріг руйнування: різка зміна швидкості, м/с
    float r, g, b;
    int   nx, ny, nz;    // на скільки частин розпадається
    bool  glass;
};
static const MatDef MATS[3] = {
    { "дерев'яний ящик", { 0.25f, 0.25f, 0.25f }, 300.f,  0.25f, 0.6f,  9.0f, 0.62f, 0.42f, 0.22f, 2, 2, 2, false },
    { "скляний блок",    { 0.10f, 0.20f, 0.10f }, 600.f,  0.10f, 0.4f,  3.5f, 0.60f, 0.85f, 0.90f, 2, 3, 2, true  },
    { "кам'яний блок",   { 0.15f, 0.15f, 0.15f }, 2400.f, 0.20f, 0.7f, 14.0f, 0.50f, 0.50f, 0.52f, 2, 2, 2, false },
};

// ------------------------------------------------------------------ тіло
struct Body {
    int   id = 0, mat = 0;
    bool  fragment = false, alive = true, asleep = false, active = true, hit = false;
    Vec3  pos, vel, w, half, invIlocal, corr, vPre;
    Quat  q;
    Mat3  R, invIw;
    float mass = 1, invMass = 1, sleepT = 0, age = 0, life = 0;
    int   corrN = 0;
    Vec3  jAccum;                      // сума нормальних імпульсів за крок (для порогу руйнування)
    float cr = 1, cg = 1, cb = 1;
};

struct Contact {                      // b = -1 -> рельєф; n дивиться від b до a
    int a, b; Vec3 p, n; float depth;
    float jn = 0, jt1 = 0, jt2 = 0;   // накопичені імпульси (нормальний і два дотичних)
    float bounce = 0, e = 0, mu = 0;
    Vec3 t1, t2;
    int ida = 0, idb = -1;            // id тіл - для перенесення імпульсів між кроками (warm starting)
};

// ------------------------------------------------------------------ світ
struct World {
    float (*terrainH)(float, float) = nullptr;
    std::vector<Body> bodies;
    int   nextId = 1, heldId = 0;
    float holdDist = 2.2f, accum = 0.f;
    Vec3  eye, fwd{ 0, 0, -1 };       // позиція/напрямок камери (виставляє застосунок)
    int   maxBodies = 500;
    float simRange = 600.f;           // далі цієї відстані від гравця фізика "заморожена"
    std::vector<Contact> contacts, prevContacts;

    static constexpr float STEP = 1.f / 120.f;
    static constexpr int   ITER = 6;

    // ---------- створення
    static void initMass(Body& b) {
        const MatDef& m = MATS[b.mat];
        b.mass = m.density * 8.f * b.half.x * b.half.y * b.half.z;
        b.invMass = 1.f / b.mass;
        float hx2 = b.half.x * b.half.x, hy2 = b.half.y * b.half.y, hz2 = b.half.z * b.half.z;
        b.invIlocal = { 3.f / (b.mass * (hy2 + hz2)), 3.f / (b.mass * (hx2 + hz2)), 3.f / (b.mass * (hx2 + hy2)) };
    }
    static void refresh(Body& b) {
        b.R = matFromQuat(b.q);
        const float d[3] = { b.invIlocal.x, b.invIlocal.y, b.invIlocal.z };
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) {
                float s = 0;
                for (int k = 0; k < 3; k++) s += b.R.m[r * 3 + k] * d[k] * b.R.m[c * 3 + k];
                b.invIw.m[r * 3 + c] = s;
            }
    }
    int spawn(int mat, Vec3 pos, Quat q, Vec3 vel) {
        Body b;
        b.id = nextId++; b.mat = mat; b.pos = pos; b.q = qnorm(q); b.vel = vel;
        b.half = MATS[mat].half;
        b.cr = MATS[mat].r; b.cg = MATS[mat].g; b.cb = MATS[mat].b;
        initMass(b); refresh(b);
        bodies.push_back(b);
        return b.id;
    }
    // стіна з ящиків 3x4 перед гравцем; yaw - у радіанах (як у камери)
    void spawnWall(Vec3 base, float yaw) {
        float hx = MATS[0].half.x, st = hx * 2.04f;
        Vec3 right{ cosf(yaw), 0, sinf(yaw) };
        float gy = -1e9f;
        for (int c = -2; c <= 2; c++) {
            Vec3 p = base + right * (c * st);
            gy = std::max(gy, terrainH(p.x, p.z));
        }
        for (int row = 0; row < 4; row++)
            for (int col = 0; col < 3; col++) {
                Vec3 p = base + right * ((col - 1) * st);
                p.y = gy + hx + 0.03f + row * st;
                spawn(0, p, quatY(-yaw), Vec3());
            }
    }

    Body* find(int id) {
        if (!id) return nullptr;
        for (Body& b : bodies) if (b.alive && b.id == id) return &b;
        return nullptr;
    }
    void wakeNear(Vec3 p, float r) {
        for (Body& b : bodies) {
            Vec3 d = b.pos - p;
            if (dot(d, d) < r * r) { b.asleep = false; b.sleepT = 0; }
        }
    }

    // ---------- імпульси
    static void pointImpulse(Body& b, const Vec3& p, const Vec3& J) {
        refresh(b);
        b.vel += J * b.invMass;
        b.w += mv(b.invIw, cross(p - b.pos, J));
        b.asleep = false; b.sleepT = 0;
    }
    static float invEff(const Body& X, const Vec3& r, const Vec3& d) {
        Vec3 t = mv(X.invIw, cross(r, d));
        return X.invMass + dot(cross(t, r), d);
    }
    static void applyJ(Body& A, Body* B, const Vec3& rA, const Vec3& rB, const Vec3& J) {
        A.vel += J * A.invMass;
        A.w += mv(A.invIw, cross(rA, J));
        if (B) { B->vel -= J * B->invMass; B->w -= mv(B->invIw, cross(rB, J)); }
    }
    // ---------- виявлення контактів
    Vec3 terrainNormal(float x, float z) const {
        const float e = 0.5f;
        return norm({ terrainH(x - e, z) - terrainH(x + e, z), 2 * e, terrainH(x, z - e) - terrainH(x, z + e) });
    }
    void terrainContacts(int i) {
        Body& b = bodies[i];
        float rad = len(b.half);
        float hc = terrainH(b.pos.x, b.pos.z);
        if (b.pos.y - rad > hc + rad * 2.f + 1.f) return;
        for (int s = 0; s < 8; s++) {
            Vec3 c{ (s & 1 ? 1.f : -1.f) * b.half.x, (s & 2 ? 1.f : -1.f) * b.half.y, (s & 4 ? 1.f : -1.f) * b.half.z };
            Vec3 p = b.pos + mv(b.R, c);
            float h = terrainH(p.x, p.z);
            if (p.y < h) contacts.push_back({ i, -1, p, terrainNormal(p.x, p.z), h - p.y });
        }
    }
    // Повний тест OBB-OBB за теоремою розділяючих осей (15 осей) + відсікання грані (стійкі стопки)
    void pairContacts(int ia, int ib) {
        const Body& A = bodies[ia];
        const Body& B = bodies[ib];
        const Vec3 a[3] = { { A.R.m[0], A.R.m[3], A.R.m[6] }, { A.R.m[1], A.R.m[4], A.R.m[7] }, { A.R.m[2], A.R.m[5], A.R.m[8] } };
        const Vec3 b[3] = { { B.R.m[0], B.R.m[3], B.R.m[6] }, { B.R.m[1], B.R.m[4], B.R.m[7] }, { B.R.m[2], B.R.m[5], B.R.m[8] } };
        const float hA[3] = { A.half.x, A.half.y, A.half.z }, hB[3] = { B.half.x, B.half.y, B.half.z };
        Vec3 t = B.pos - A.pos;
        auto radius = [](const Vec3* ax, const float* h, const Vec3& L) {
            return h[0] * fabsf(dot(ax[0], L)) + h[1] * fabsf(dot(ax[1], L)) + h[2] * fabsf(dot(ax[2], L));
        };
        float best = -1e9f; int type = -1, bi = 0, bj = 0; Vec3 bestL;
        for (int i = 0; i < 3; i++) {
            float sep = fabsf(dot(t, a[i])) - radius(a, hA, a[i]) - radius(b, hB, a[i]);
            if (sep > 0) return;
            if (sep > best) { best = sep; type = 0; bi = i; bestL = a[i]; }
        }
        for (int j = 0; j < 3; j++) {
            float sep = fabsf(dot(t, b[j])) - radius(a, hA, b[j]) - radius(b, hB, b[j]);
            if (sep > 0) return;
            if (sep > best) { best = sep; type = 1; bi = j; bestL = b[j]; }
        }
        float bestFace = best;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                Vec3 L = cross(a[i], b[j]);
                float l = len(L);
                if (l < 1e-4f) continue;
                L *= 1.f / l;
                float sep = fabsf(dot(t, L)) - radius(a, hA, L) - radius(b, hB, L);
                if (sep > 0) return;
                if (sep > best && sep > bestFace + 0.01f) { best = sep; type = 2; bi = i; bj = j; bestL = L; }
            }
        Vec3 nAB = bestL;
        if (dot(nAB, t) < 0) nAB = nAB * -1.f;       // від A до B
        Vec3 nBA = nAB * -1.f;                        // від B до A - нормаль контакту

        if (type == 2) {                              // ребро-ребро
            Vec3 pa = A.pos, pb = B.pos;
            for (int k = 0; k < 3; k++) {
                if (k != bi) pa += a[k] * (hA[k] * (dot(a[k], nAB) > 0 ? 1.f : -1.f));
                if (k != bj) pb += b[k] * (hB[k] * (dot(b[k], nAB) > 0 ? -1.f : 1.f));
            }
            Vec3 r = pa - pb;
            float bb = dot(a[bi], b[bj]), cc = dot(a[bi], r), ff = dot(b[bj], r);
            float den = 1.f - bb * bb;
            if (fabsf(den) < 1e-6f) return;
            float s = (bb * ff - cc) / den, tt = (ff - bb * cc) / den;
            Vec3 p = (pa + a[bi] * s + pb + b[bj] * tt) * 0.5f;
            contacts.push_back({ ia, ib, p, nBA, -best });
            return;
        }

        // грань-грань: опорна (reference) і падаюча (incident) грані
        const Body& ref = type == 0 ? A : B;
        const Body& inc = type == 0 ? B : A;
        const Vec3* rax = type == 0 ? a : b;
        const Vec3* iax = type == 0 ? b : a;
        const float* rh = type == 0 ? hA : hB;
        const float* ih = type == 0 ? hB : hA;
        Vec3 nRef = type == 0 ? nAB : nBA;            // назовні від опорного тіла
        int k = bi;
        int m = 0; float md = -1;
        for (int q = 0; q < 3; q++) { float d = fabsf(dot(iax[q], nRef)); if (d > md) { md = d; m = q; } }
        Vec3 faceN = iax[m] * (dot(iax[m], nRef) > 0 ? -1.f : 1.f);
        Vec3 fc = inc.pos + faceN * ih[m];
        int p1 = (m + 1) % 3, p2 = (m + 2) % 3;
        std::vector<Vec3> poly = {
            fc + iax[p1] * ih[p1] + iax[p2] * ih[p2], fc - iax[p1] * ih[p1] + iax[p2] * ih[p2],
            fc - iax[p1] * ih[p1] - iax[p2] * ih[p2], fc + iax[p1] * ih[p1] - iax[p2] * ih[p2] };
        for (int side = 0; side < 4; side++) {        // відсікання бічними площинами опорної грані
            int u = (k + 1 + (side >> 1)) % 3;
            float sg = (side & 1) ? -1.f : 1.f;
            Vec3 pn = rax[u] * sg;
            float pd = dot(ref.pos, pn) + rh[u];
            std::vector<Vec3> out;
            for (size_t i = 0; i < poly.size(); i++) {
                Vec3 P = poly[i], Q = poly[(i + 1) % poly.size()];
                float dP = dot(P, pn) - pd, dQ = dot(Q, pn) - pd;
                if (dP <= 0) out.push_back(P);
                if ((dP < 0 && dQ > 0) || (dP > 0 && dQ < 0)) out.push_back(P + (Q - P) * (dP / (dP - dQ)));
            }
            poly.swap(out);
            if (poly.empty()) return;
        }
        float planeD = dot(ref.pos, nRef) + rh[k];
        for (const Vec3& P : poly) {
            float depth = planeD - dot(P, nRef);
            if (depth > -0.002f) contacts.push_back({ ia, ib, P, nBA, std::max(depth, 0.f) });
        }
    }

    // ---------- руйнування
    void breakBody(int idx, float dv) {
        Body src = bodies[idx];
        bodies[idx].alive = false;
        if (heldId == src.id) heldId = 0;
        const MatDef& m = MATS[src.mat];
        printf("[phys] РОЗБИТО: %s (зміна швидкості %.1f м/с)\n", m.name, dv);
        wakeNear(src.pos, 4.f);
        Vec3 cell{ src.half.x * 2.f / m.nx, src.half.y * 2.f / m.ny, src.half.z * 2.f / m.nz };
        float burst = std::max(0.8f, std::min(5.f, dv * 0.3f));
        for (int ix = 0; ix < m.nx; ix++)
            for (int iy = 0; iy < m.ny; iy++)
                for (int iz = 0; iz < m.nz; iz++) {
                    Vec3 lc{ -src.half.x + cell.x * (ix + 0.5f), -src.half.y + cell.y * (iy + 0.5f), -src.half.z + cell.z * (iz + 0.5f) };
                    Vec3 r = mv(src.R, lc);
                    Body f;
                    f.id = nextId++; f.mat = src.mat; f.fragment = true;
                    f.half = { cell.x * 0.46f, cell.y * 0.46f, cell.z * 0.46f }; // 8% зазор - без початкового overlap
                    f.pos = src.pos + r; f.q = src.q;
                    Vec3 dir = norm(r + Vec3{ frand() - 0.5f, frand() - 0.3f, frand() - 0.5f } * (len(cell) * 0.5f));
                    f.vel = src.vel + cross(src.w, r) + dir * (burst * (0.4f + frand()));
                    f.w = src.w + Vec3{ frand() - 0.5f, frand() - 0.5f, frand() - 0.5f } * 8.f;
                    float v = 0.88f + frand() * 0.2f;
                    f.cr = src.cr * v; f.cg = src.cg * v; f.cb = src.cb * v;
                    f.life = 18.f + frand() * 8.f;
                    initMass(f); refresh(f);
                    bodies.push_back(f);
                }
    }

    // ---------- один фіксований крок
    void step(float dt) {
        const float rr = simRange * simRange;
        for (Body& b : bodies) {
            if (!b.alive) continue;
            Vec3 d = b.pos - eye;
            b.active = (d.x * d.x + d.z * d.z) < rr;
            b.hit = false; b.corr = Vec3(); b.corrN = 0; b.jAccum = Vec3();
            if (!b.active) continue;
            refresh(b);
            b.age += dt;
            if (b.asleep) continue;
            if (b.id == heldId) {
                Vec3 target = eye + fwd * holdDist;
                Vec3 v = (target - b.pos) * 12.f;
                float l = len(v);
                if (l > 14.f) v *= 14.f / l;
                b.vel = v;
                b.w *= (1.f - 8.f * dt);
            } else {
                b.vel.y -= 9.81f * dt;
                b.vel *= (1.f - 0.02f * dt);   // опір повітря
                b.w *= (1.f - 0.3f * dt);
            }
            float sp = len(b.vel);
            if (sp > 60.f) b.vel *= 60.f / sp;
            float aw = len(b.w);
            if (aw > 40.f) b.w *= 40.f / aw;
            b.vPre = b.vel;
        }

        // контакти
        contacts.clear();
        int n = (int)bodies.size();
        for (int i = 0; i < n; i++) {
            Body& a = bodies[i];
            if (!a.alive || !a.active) continue;
            if (!a.asleep) terrainContacts(i);
            float ra = len(a.half);
            for (int j = i + 1; j < n; j++) {
                Body& b = bodies[j];
                if (!b.alive || !b.active || (a.asleep && b.asleep)) continue;
                Vec3 d = a.pos - b.pos;
                float rs = ra + len(b.half);
                if (dot(d, d) > rs * rs) continue;
                pairContacts(i, j);
            }
        }

        // підготовка контактів: пружність, тертя, дотичні напрямки
        for (Contact& c : contacts) {
            Body& A = bodies[c.a];
            Body* B = c.b >= 0 ? &bodies[c.b] : nullptr;
            if (B) { c.e = std::min(MATS[A.mat].restitution, MATS[B->mat].restitution);
                     c.mu = sqrtf(MATS[A.mat].friction * MATS[B->mat].friction); }
            else   { c.e = MATS[A.mat].restitution; c.mu = sqrtf(MATS[A.mat].friction * 0.8f); }
            Vec3 rA = c.p - A.pos, rB = B ? c.p - B->pos : Vec3();
            Vec3 rv = (A.vel + cross(A.w, rA)) - (B ? B->vel + cross(B->w, rB) : Vec3());
            float vn = dot(rv, c.n);
            c.bounce = (-vn > 1.0f) ? -c.e * vn : 0.f;   // відскок лише при помітному ударі
            Vec3 ref = fabsf(c.n.y) < 0.9f ? Vec3{ 0, 1, 0 } : Vec3{ 1, 0, 0 };
            c.t1 = norm(cross(c.n, ref));
            c.t2 = cross(c.n, c.t1);
            // warm starting: беремо накопичені імпульси найближчого контакту попереднього кроку
            c.ida = A.id; c.idb = B ? B->id : -1;
            for (const Contact& pc : prevContacts) {
                if (pc.ida != c.ida || pc.idb != c.idb) continue;
                Vec3 d = pc.p - c.p;
                if (dot(d, d) > 0.0025f) continue;
                c.jn = pc.jn; c.jt1 = pc.jt1; c.jt2 = pc.jt2;
                Vec3 J = c.n * c.jn + c.t1 * c.jt1 + c.t2 * c.jt2;
                applyJ(A, B, rA, rB, J);
                break;
            }
        }

        // послідовний солвер з накопиченими імпульсами (тертя обмежене mu * накопичений нормальний)
        const int nc = (int)contacts.size();
        for (int it = 0; it < ITER; it++)
            for (int ci = 0; ci < nc; ci++) {
                Contact& c = contacts[ci];
                Body& A = bodies[c.a];
                Body* B = c.b >= 0 ? &bodies[c.b] : nullptr;
                Vec3 rA = c.p - A.pos, rB = B ? c.p - B->pos : Vec3();
                auto relVel = [&]() {
                    return (A.vel + cross(A.w, rA)) - (B ? B->vel + cross(B->w, rB) : Vec3());
                };
                for (int k = 0; k < 2; k++) {
                    const Vec3& t = k ? c.t2 : c.t1;
                    float& jt = k ? c.jt2 : c.jt1;
                    float denom = invEff(A, rA, t) + (B ? invEff(*B, rB, t) : 0.f);
                    float dj = -dot(relVel(), t) / denom;
                    float lim = c.mu * c.jn;
                    float nj = std::max(-lim, std::min(lim, jt + dj));
                    dj = nj - jt; jt = nj;
                    applyJ(A, B, rA, rB, t * dj);
                }
                float denom = invEff(A, rA, c.n) + (B ? invEff(*B, rB, c.n) : 0.f);
                float dj = (c.bounce - dot(relVel(), c.n)) / denom;
                float nj = std::max(0.f, c.jn + dj);
                dj = nj - c.jn; c.jn = nj;
                applyJ(A, B, rA, rB, c.n * dj);
            }
        prevContacts = contacts;
        for (const Contact& c : contacts)
            if (c.jn > 1e-6f) {
                bodies[c.a].hit = true; bodies[c.a].jAccum += c.n * c.jn;
                if (c.b >= 0) { bodies[c.b].hit = true; bodies[c.b].jAccum -= c.n * c.jn; }
            }

        // позиційна корекція
        for (const Contact& c : contacts) {
            float depth = c.depth - 0.005f;
            if (depth <= 0) continue;
            Body& A = bodies[c.a];
            if (c.b >= 0) {
                Body& B = bodies[c.b];
                float tot = A.invMass + B.invMass;
                A.corr += c.n * (depth * 0.25f * A.invMass / tot); A.corrN++;
                B.corr -= c.n * (depth * 0.25f * B.invMass / tot); B.corrN++;
            } else {
                A.corr += c.n * (depth * 0.25f); A.corrN++;
            }
        }

        // інтеграція, сон, руйнування
        std::vector<std::pair<int, float>> toBreak;
        for (int i = 0; i < n; i++) {
            Body& b = bodies[i];
            if (!b.alive || !b.active) continue;
            if (b.asleep) {
                if (len(b.vel) > 0.2f || len(b.w) > 0.4f) { b.asleep = false; b.sleepT = 0; }
                else { b.vel = Vec3(); b.w = Vec3(); continue; }
            }
            if (b.hit && b.id != heldId && len(b.vel) < 0.6f) { b.vel *= 0.99f; b.w *= 0.95f; }
            if (b.corrN > 0) b.pos += b.corr * (1.f / b.corrN);
            b.pos += b.vel * dt;
            Quat wq{ 0, b.w.x, b.w.y, b.w.z };
            Quat dq = qmul(wq, b.q);
            b.q = qnorm({ b.q.w + 0.5f * dt * dq.w, b.q.x + 0.5f * dt * dq.x, b.q.y + 0.5f * dt * dq.y, b.q.z + 0.5f * dt * dq.z });

            if (b.id != heldId && len(b.vel) < 0.2f && len(b.w) < 0.4f) {
                b.sleepT += dt;
                if (b.sleepT > 0.4f) { b.asleep = true; b.vel = Vec3(); b.w = Vec3(); }
            } else b.sleepT = 0;

            if (b.hit && !b.fragment && b.id != heldId && b.age > 0.4f) {
                float dv = len(b.jAccum) * b.invMass;   // різка зміна швидкості вздовж нормалей контакту
                if (dv > MATS[b.mat].breakDv) toBreak.push_back({ i, dv });
            }
        }
        for (auto& tb : toBreak) breakBody(tb.first, tb.second);
    }

    // ---------- кадр
    void update(float dt) {
        accum += std::min(dt, 0.05f);
        int steps = 0;
        while (accum >= STEP && steps < 6) { step(STEP); accum -= STEP; steps++; }
        if (accum > STEP) accum = 0.f;

        for (Body& b : bodies)
            if (b.alive && b.fragment) { b.life -= dt; if (b.life <= 0) b.alive = false; }
        // ліміт кількості: прибираємо найстаріші уламки
        int cnt = 0;
        for (Body& b : bodies) if (b.alive) cnt++;
        while (cnt > maxBodies) {
            Body* o = nullptr;
            for (Body& b : bodies) if (b.alive && b.fragment && (!o || b.life < o->life)) o = &b;
            if (!o) break;
            o->alive = false; cnt--;
        }
        bodies.erase(std::remove_if(bodies.begin(), bodies.end(), [](const Body& b) { return !b.alive; }), bodies.end());
        if (heldId && !find(heldId)) heldId = 0;
    }

    // ---------- взаємодія гравця
    int raycast(Vec3 o, Vec3 d, float maxT, float& tHit) {
        int best = -1; float bt = maxT;
        for (int i = 0; i < (int)bodies.size(); i++) {
            Body& b = bodies[i];
            if (!b.alive) continue;
            refresh(b);
            Vec3 lo = mtv(b.R, o - b.pos), ld = mtv(b.R, d);
            const float h[3] = { b.half.x, b.half.y, b.half.z };
            const float po[3] = { lo.x, lo.y, lo.z }, pd[3] = { ld.x, ld.y, ld.z };
            float t0 = 0, t1 = bt; bool ok = true;
            for (int k = 0; k < 3 && ok; k++) {
                if (fabsf(pd[k]) < 1e-8f) { if (fabsf(po[k]) > h[k]) ok = false; }
                else {
                    float a = (-h[k] - po[k]) / pd[k], c = (h[k] - po[k]) / pd[k];
                    if (a > c) std::swap(a, c);
                    t0 = std::max(t0, a); t1 = std::min(t1, c);
                    if (t0 > t1) ok = false;
                }
            }
            if (ok && t0 < bt) { bt = t0; best = i; }
        }
        tHit = bt;
        return best;
    }
    // 0 - взято, 1 - надто важке, 2 - нічого немає
    int grab() {
        float t;
        int i = raycast(eye, fwd, 5.f, t);
        if (i < 0) return 2;
        if (bodies[i].mass > 120.f) return 1;
        heldId = bodies[i].id;
        holdDist = std::max(1.2f, std::min(4.f, t));
        bodies[i].asleep = false; bodies[i].sleepT = 0;
        wakeNear(bodies[i].pos, 1.5f);
        return 0;
    }
    void release() { heldId = 0; }
    void throwHeld(Vec3 playerVel) {
        Body* b = find(heldId);
        heldId = 0;
        if (!b) return;
        float speed = 20.f / (1.f + b->mass / 40.f);   // важче - повільніше
        b->vel = fwd * speed + playerVel;
        b->w = Vec3{ frand() - 0.5f, frand() - 0.5f, frand() - 0.5f } * 6.f;
        b->asleep = false; b->sleepT = 0;
    }
    // удар молотом (~450 Н·с); повертає індекс влученого тіла або -1
    int strike() {
        float t;
        int i = raycast(eye, fwd, 6.f, t);
        if (i < 0) return -1;
        Body& b = bodies[i];
        Vec3 J = fwd * 450.f; J.y += 60.f;
        pointImpulse(b, eye + fwd * t, J);
        wakeNear(b.pos, 3.f);
        float dv = len(J) * b.invMass;
        if (!b.fragment && dv > MATS[b.mat].breakDv) breakBody(i, dv);
        return i;
    }
    Body* held() { return find(heldId); }
};

} // namespace phys
