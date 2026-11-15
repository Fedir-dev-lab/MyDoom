// Прототип відкритого світу: 3x3 чанки, чанк = 5000x5000 блоків (1 блок = 1 м)
// C++17 + GLFW + OpenGL 2.1 (фіксований конвеєр, display lists)
//
// Збірка:
//   Linux:   g++ -O2 -std=c++17 openworld.cpp -o openworld -lglfw -lGL
//   Windows: g++ -O2 -std=c++17 openworld.cpp -o openworld.exe -lglfw3 -lopengl32 -lgdi32
//   macOS:   clang++ -O2 -std=c++17 openworld.cpp -o openworld -lglfw -framework OpenGL
//
// Керування: WASD - рух, миша - огляд, Shift - біг x5, Ctrl - турбо x50, F - політ/ходьба (Space/C вгору/вниз),
//   E - взяти/покласти предмет, ЛКМ - кинути тримаючи, ПКМ - удар молотом (розбиває),
//   колесо - відстань тримання, 1/2/3 - створити ящик/скло/камінь, T - стіна з ящиків, Esc - вихід.

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
#include "physics.hpp"

// ------------------------------------------------------------------ налаштування
static const int   CHUNK_SIZE   = 5000;              // блоків у чанку по стороні
static const int   GRID         = 3;                 // карта 3x3 чанки
static const float WORLD_SIZE   = (float)(CHUNK_SIZE * GRID);
static const int   TILE_SIZE    = 100;               // плитка (одиниця рендеру/кешу) 100x100
static const int   TILES_PER_CH = CHUNK_SIZE / TILE_SIZE; // 50x50 плиток у чанку
static const float FOG_START    = 1500.f;            // з цієї відстані об'єкти починають "розмиватися"
static const float FOG_END      = 2200.f;            // тут вони повністю зникають у серпанку
static const float LOAD_DIST    = FOG_END + 100.f;   // чанк підвантажується, коли до його краю <= цієї відстані
static const float UNLOAD_HYST  = 25.f;              // чанк забувається, коли гравець відійшов ще на 25 м далі
static const int   BUILD_BUDGET = 6;                 // скільки плиток будувати за кадр
static const float PI           = 3.14159265f;

// ------------------------------------------------------------------ процедурна генерація
static float hash2(int x, int z, unsigned salt = 0) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)z * 668265263u + salt * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFFu) / 16777216.0f;
}
static float vnoise(float x, float z) {
    int xi = (int)floorf(x), zi = (int)floorf(z);
    float fx = x - xi, fz = z - zi;
    float u = fx * fx * (3 - 2 * fx), v = fz * fz * (3 - 2 * fz);
    float a = hash2(xi, zi), b = hash2(xi + 1, zi), c = hash2(xi, zi + 1), d = hash2(xi + 1, zi + 1);
    float l0 = a + (b - a) * u, l1 = c + (d - c) * u;
    return l0 + (l1 - l0) * v;
}
static float fbm(float x, float z) {
    float s = 0, a = 0.5f, f = 1;
    for (int i = 0; i < 5; i++) { s += a * vnoise(x * f, z * f); f *= 2; a *= 0.5f; }
    return s / 0.96875f;
}
// Висота світу в точці (метри). Світ детермінований - нічого не зберігаємо.
static float terrainH(float x, float z) {
    float h = (fbm(x * 0.0006f, z * 0.0006f) - 0.42f) * 420.f;
    return h < 0 ? 0 : h; // 0 = рівень води
}

// ------------------------------------------------------------------ чанки та плитки
struct Tile { int lod = -1; GLuint list = 0; };
struct Chunk {
    bool loaded = false;
    std::vector<Tile> tiles;
};
static Chunk g_chunks[GRID][GRID];

static void freeTile(Tile& t) {
    if (t.list) { glDeleteLists(t.list, 1); t.list = 0; }
    t.lod = -1;
}
static void loadChunk(int cx, int cz) {
    Chunk& c = g_chunks[cz][cx];
    c.tiles.assign(TILES_PER_CH * TILES_PER_CH, Tile());
    c.loaded = true;
    printf("[chunk] ЗАВАНТАЖЕНО (%d,%d)\n", cx, cz);
}
static void unloadChunk(int cx, int cz) {
    Chunk& c = g_chunks[cz][cx];
    for (Tile& t : c.tiles) freeTile(t);
    std::vector<Tile>().swap(c.tiles);
    c.loaded = false;
    printf("[chunk] ЗАБУТО (%d,%d)\n", cx, cz);
}
static float distToChunk(int cx, int cz, float px, float pz) {
    float x0 = (float)cx * CHUNK_SIZE, x1 = x0 + CHUNK_SIZE;
    float z0 = (float)cz * CHUNK_SIZE, z1 = z0 + CHUNK_SIZE;
    float dx = std::max(std::max(x0 - px, 0.f), px - x1);
    float dz = std::max(std::max(z0 - pz, 0.f), pz - z1);
    return sqrtf(dx * dx + dz * dz);
}
static void updateChunks(float px, float pz) {
    for (int cz = 0; cz < GRID; cz++)
        for (int cx = 0; cx < GRID; cx++) {
            float d = distToChunk(cx, cz, px, pz);
            Chunk& c = g_chunks[cz][cx];
            if (!c.loaded && d <= LOAD_DIST) loadChunk(cx, cz);
            else if (c.loaded && d > LOAD_DIST + UNLOAD_HYST) unloadChunk(cx, cz);
        }
}

// ------------------------------------------------------------------ побудова меша плитки
struct V { float x, y, z, r, g, b; };

static const int LOD_STEP[4] = { 4, 10, 20, 50 };
static int pickLod(float d) { return d < 250 ? 0 : d < 600 ? 1 : d < 1200 ? 2 : 3; }

static V makeVertex(float x, float z, float e) {
    float h = terrainH(x, z);
    float nx = terrainH(x - e, z) - terrainH(x + e, z);
    float nz = terrainH(x, z - e) - terrainH(x, z + e);
    float ny = 2 * e;
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    nx /= len; ny /= len; nz /= len;
    float shade = 0.45f + 0.55f * std::max(0.f, nx * 0.5f + ny * 0.8f + nz * 0.3f);
    float r, g, b;
    if (h <= 0.01f)      { r = 0.10f; g = 0.30f; b = 0.60f; shade = 1.f; }
    else if (h < 3.f)    { r = 0.80f; g = 0.75f; b = 0.50f; }
    else if (ny < 0.80f || (h > 90.f && h < 140.f)) { r = 0.45f; g = 0.42f; b = 0.40f; }
    else if (h >= 140.f) { r = 0.95f; g = 0.95f; b = 0.97f; }
    else                 { r = 0.22f; g = 0.50f - h * 0.0015f; b = 0.18f; }
    return { x, h, z, r * shade, g * shade, b * shade };
}

static void emit(const V& v, float k = 1.f) {
    glColor3f(v.r * k, v.g * k, v.b * k);
    glVertex3f(v.x, v.y, v.z);
}
static void skirt(const V& p, const V& q) { // "спідниця" ховає щілини між різними LOD
    const float SK = 40.f;
    V p2 = p, q2 = q; p2.y -= SK; q2.y -= SK;
    emit(p, 0.8f); emit(q, 0.8f); emit(q2, 0.8f);
    emit(p, 0.8f); emit(q2, 0.8f); emit(p2, 0.8f);
}

static void emitTree(float x, float y, float z, float s, float shadeSeed) {
    const int N = 6;
    float rad = 3.5f * s, hgt = 12.f * s;
    float gr = 0.28f + shadeSeed * 0.15f;
    float px[N + 1], pz[N + 1];
    for (int i = 0; i <= N; i++) {
        float a = (float)i / N * 2 * PI;
        px[i] = x + cosf(a) * rad; pz[i] = z + sinf(a) * rad;
    }
    for (int i = 0; i < N; i++) {
        float k = (i & 1) ? 0.75f : 1.0f;
        glColor3f(0.08f * k, gr * k, 0.10f * k);
        glVertex3f(x, y + hgt, z);
        glVertex3f(px[i], y, pz[i]);
        glVertex3f(px[i + 1], y, pz[i + 1]);
    }
}

static GLuint buildTile(int wx0, int wz0, int lod) {
    int step = LOD_STEP[lod];
    int n = TILE_SIZE / step;
    float e = (float)std::max(step, 2);

    std::vector<V> g((n + 1) * (n + 1));
    for (int j = 0; j <= n; j++)
        for (int i = 0; i <= n; i++)
            g[j * (n + 1) + i] = makeVertex((float)(wx0 + i * step), (float)(wz0 + j * step), e);

    GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glBegin(GL_TRIANGLES);
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            const V& a = g[j * (n + 1) + i];
            const V& b = g[j * (n + 1) + i + 1];
            const V& c = g[(j + 1) * (n + 1) + i];
            const V& d = g[(j + 1) * (n + 1) + i + 1];
            emit(a); emit(c); emit(b);
            emit(b); emit(c); emit(d);
        }
    for (int i = 0; i < n; i++) {
        skirt(g[i], g[i + 1]);                                   // північ
        skirt(g[n * (n + 1) + i], g[n * (n + 1) + i + 1]);       // південь
        skirt(g[i * (n + 1)], g[(i + 1) * (n + 1)]);             // захід
        skirt(g[i * (n + 1) + n], g[(i + 1) * (n + 1) + n]);     // схід
    }
    // дерева - лише на ближніх/середніх LOD, клітинки належать рівно одній плитці
    if (lod <= 2) {
        int cs = lod <= 1 ? 20 : 40;
        float scale = lod <= 1 ? 1.0f : 1.7f;
        for (int gz = 0; gz < TILE_SIZE / cs; gz++)
            for (int gx = 0; gx < TILE_SIZE / cs; gx++) {
                int cwx = wx0 + gx * cs, cwz = wz0 + gz * cs;
                float px = cwx + hash2(cwx, cwz, 1) * cs;
                float pz = cwz + hash2(cwx, cwz, 2) * cs;
                if (vnoise(px * 0.004f, pz * 0.004f) < 0.5f) continue; // ліси плямами
                if (hash2(cwx, cwz, 3) > 0.45f) continue;
                float h = terrainH(px, pz);
                if (h < 4.f || h > 85.f) continue;
                emitTree(px, h - 0.5f, pz, scale * (0.8f + hash2(cwx, cwz, 4) * 0.5f), hash2(cwx, cwz, 5));
            }
    }
    glEnd();
    glEndList();
    return list;
}

// ------------------------------------------------------------------ камера / ввід
static float g_px = WORLD_SIZE / 2, g_py = 100, g_pz = WORLD_SIZE / 2;
static float g_yaw = 0, g_pitch = 20;
static bool  g_fly = false;
static double g_lastMx = 0, g_lastMy = 0;
static bool  g_firstMouse = true;

static phys::World g_world;
static bool g_kE = false, g_kT = false, g_k1 = false, g_k2 = false, g_k3 = false;
static const char* g_msg = "";
static float g_msgT = 0;

static void say(const char* m) { g_msg = m; g_msgT = 2.5f; }

static void onKey(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, 1);
    if (key == GLFW_KEY_F) g_fly = !g_fly;
    if (key == GLFW_KEY_E) g_kE = true;
    if (key == GLFW_KEY_T) g_kT = true;
    if (key == GLFW_KEY_1) g_k1 = true;
    if (key == GLFW_KEY_2) g_k2 = true;
    if (key == GLFW_KEY_3) g_k3 = true;
}
static void onScroll(GLFWwindow*, double, double dy) {
    g_world.holdDist = std::max(1.f, std::min(5.f, g_world.holdDist + (float)dy * 0.3f));
}

// ближча до центру карти "суша" без крутих схилів - щоб старт був не у воді
static void findLand() {
    float best = 1e30f;
    for (float z = 1500; z < WORLD_SIZE - 1500; z += 40)
        for (float x = 1500; x < WORLD_SIZE - 1500; x += 40) {
            float h = terrainH(x, z);
            if (h < 8.f || h > 35.f) continue;
            if (fabsf(terrainH(x + 15, z) - h) > 6.f || fabsf(terrainH(x, z + 15) - h) > 6.f) continue;
            float dx = x - WORLD_SIZE / 2, dz = z - WORLD_SIZE / 2, d = dx * dx + dz * dz;
            if (d < best) { best = d; g_px = x; g_pz = z; }
        }
}

// ------------------------------------------------------------------ малювання тіл
static const float FN[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
static const float FV[6][4][3] = {
    {{1,-1,-1},{1,1,-1},{1,1,1},{1,-1,1}},   {{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}},
    {{-1,1,-1},{-1,1,1},{1,1,1},{1,1,-1}},   {{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}},
    {{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},   {{-1,-1,-1},{-1,1,-1},{1,1,-1},{1,-1,-1}} };

static void drawBody(const phys::Body& b, bool held, float alpha) {
    const phys::MatDef& m = phys::MATS[b.mat];
    float mat[16] = {
        b.R.m[0], b.R.m[3], b.R.m[6], 0,
        b.R.m[1], b.R.m[4], b.R.m[7], 0,
        b.R.m[2], b.R.m[5], b.R.m[8], 0,
        b.pos.x,  b.pos.y,  b.pos.z,  1 };
    glPushMatrix();
    glMultMatrixf(mat);
    glScalef(b.half.x, b.half.y, b.half.z);
    glBegin(GL_QUADS);
    for (int f = 0; f < 6; f++) {
        phys::Vec3 n = phys::mv(b.R, { FN[f][0], FN[f][1], FN[f][2] });
        float shade = 0.45f + 0.55f * std::max(0.f, n.x * 0.51f + n.y * 0.82f + n.z * 0.31f);
        float add = held ? 0.25f : 0.f;
        glColor4f(std::min(1.f, b.cr * shade + add), std::min(1.f, b.cg * shade + add), std::min(1.f, b.cb * shade + add), alpha);
        for (int v = 0; v < 4; v++) glVertex3f(FV[f][v][0], FV[f][v][1], FV[f][v][2]);
    }
    glEnd();
    glPopMatrix();
    (void)m;
}

static void drawCrosshair(int w, int h, bool holding, bool aimingAtBody) {
    glDisable(GL_FOG);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
    float cx = w * 0.5f, cy = h * 0.5f, s = holding ? 4.f : 8.f;
    if (aimingAtBody) glColor3f(1.f, 0.9f, 0.2f); else glColor3f(1.f, 1.f, 1.f);
    glLineWidth(2.f);
    glBegin(GL_LINES);
    glVertex2f(cx - s, cy); glVertex2f(cx + s, cy);
    glVertex2f(cx, cy - s); glVertex2f(cx, cy + s);
    glEnd();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_FOG);
}

struct DrawItem { float dist; Tile* t; int wx0, wz0, lod; };

int main() {
    if (!glfwInit()) return 1;
    GLFWwindow* win = glfwCreateWindow(1280, 720, "Open world prototype", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetKeyCallback(win, onKey);
    glfwSetScrollCallback(win, onScroll);

    const float sky[4] = { 0.62f, 0.74f, 0.88f, 1.f };
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, FOG_START);
    glFogf(GL_FOG_END, FOG_END);
    glFogfv(GL_FOG_COLOR, sky);
    glClearColor(sky[0], sky[1], sky[2], 1.f);

    // ---- фізика: старт на суші + кілька предметів навколо
    srand(1234);
    g_world.terrainH = terrainH;
    findLand();
    for (int i = 0; i < 24; i++) {
        float a = phys::frand() * 6.2832f, r = 6.f + phys::frand() * 30.f;
        float x = g_px + cosf(a) * r, z = g_pz + sinf(a) * r;
        g_world.spawn(i % 3, { x, terrainH(x, z) + 1.f + phys::frand() * 3.f, z }, phys::quatY(phys::frand() * 6.f), phys::Vec3());
    }
    g_py = terrainH(g_px, g_pz) + 1.8f;

    double lastT = glfwGetTime(), titleT = 0;
    int frames = 0;
    bool prevL = false, prevR = false;
    phys::Vec3 playerVel;

    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        float dt = (float)(now - lastT);
        lastT = now;
        if (dt < 1e-5f) dt = 1e-5f;
        glfwPollEvents();

        // ---- миша
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        if (g_firstMouse) { g_lastMx = mx; g_lastMy = my; g_firstMouse = false; }
        g_yaw   += (float)(mx - g_lastMx) * 0.12f;
        g_pitch += (float)(my - g_lastMy) * 0.12f;
        g_lastMx = mx; g_lastMy = my;
        g_pitch = std::max(-89.f, std::min(89.f, g_pitch));

        // ---- рух
        float oldx = g_px, oldy = g_py, oldz = g_pz;
        float yr = g_yaw * PI / 180.f, pr = g_pitch * PI / 180.f;
        float fx = sinf(yr), fz = -cosf(yr), rx = cosf(yr), rz = sinf(yr);
        float speed = 12.f;
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT)) speed *= 5.f;
        if (glfwGetKey(win, GLFW_KEY_LEFT_CONTROL)) speed *= 50.f;
        float mvx = 0, mvz = 0;
        if (glfwGetKey(win, GLFW_KEY_W)) { mvx += fx; mvz += fz; }
        if (glfwGetKey(win, GLFW_KEY_S)) { mvx -= fx; mvz -= fz; }
        if (glfwGetKey(win, GLFW_KEY_D)) { mvx += rx; mvz += rz; }
        if (glfwGetKey(win, GLFW_KEY_A)) { mvx -= rx; mvz -= rz; }
        float ml = sqrtf(mvx * mvx + mvz * mvz);
        if (ml > 0) { g_px += mvx / ml * speed * dt; g_pz += mvz / ml * speed * dt; }
        g_px = std::max(1.f, std::min(WORLD_SIZE - 1.f, g_px)); // межі карти 3x3
        g_pz = std::max(1.f, std::min(WORLD_SIZE - 1.f, g_pz));
        if (g_fly) {
            if (glfwGetKey(win, GLFW_KEY_SPACE)) g_py += speed * dt;
            if (glfwGetKey(win, GLFW_KEY_C))     g_py -= speed * dt;
        } else {
            g_py = terrainH(g_px, g_pz) + 1.8f;
        }
        // швидкість гравця (для кидка "з розгону"); різкі стрибки по рельєфу відсікаємо
        playerVel = { (g_px - oldx) / dt, 0.f, (g_pz - oldz) / dt };
        if (g_fly) playerVel.y = (g_py - oldy) / dt;
        if (phys::len(playerVel) > 40.f) playerVel = phys::Vec3();

        // ---- стрімінг чанків
        updateChunks(g_px, g_pz);

        // ---- взаємодія з предметами
        g_world.eye = { g_px, g_py, g_pz };
        g_world.fwd = { sinf(yr) * cosf(pr), -sinf(pr), -cosf(yr) * cosf(pr) };
        phys::Vec3 flatF = { fx, 0, fz };
        if (g_kE) {
            if (g_world.heldId) { g_world.release(); say("Покладено"); }
            else {
                int r = g_world.grab();
                if (r == 0) say("Взято (ЛКМ - кинути, колесо - відстань)");
                else if (r == 1) say("Надто важке");
                else say("Нічого немає перед тобою");
            }
        }
        bool L = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool R = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (L && !prevL && g_world.heldId) { g_world.throwHeld(playerVel); say("Кинуто"); }
        if (R && !prevR) {
            if (g_world.heldId) g_world.release();
            if (g_world.strike() < 0) say("Мимо");
        }
        prevL = L; prevR = R;
        auto spawnAhead = [&](int mat) {
            phys::Vec3 p = g_world.eye + g_world.fwd * 2.5f;
            g_world.spawn(mat, p, phys::quatY(phys::frand() * 6.f), g_world.fwd * 2.f);
        };
        if (g_k1) spawnAhead(0);
        if (g_k2) spawnAhead(1);
        if (g_k3) spawnAhead(2);
        if (g_kT) g_world.spawnWall(g_world.eye + flatF * 5.f, yr);
        g_kE = g_kT = g_k1 = g_k2 = g_k3 = false;

        g_world.update(dt);
        if (g_msgT > 0) g_msgT -= dt;

        // ---- камера
        int fbw, fbh;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        if (fbh == 0) fbh = 1;
        float aspect = (float)fbw / fbh;
        glViewport(0, 0, fbw, fbh);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        float nearP = 0.1f, farP = FOG_END + 400.f, fovY = 70.f;
        float tanHalfV = tanf(fovY * 0.5f * PI / 180.f);
        glFrustum(-nearP * tanHalfV * aspect, nearP * tanHalfV * aspect, -nearP * tanHalfV, nearP * tanHalfV, nearP, farP);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glRotatef(g_pitch, 1, 0, 0);
        glRotatef(g_yaw, 0, 1, 0);
        glTranslatef(-g_px, -g_py, -g_pz);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ---- відбір видимих плиток (відстань + огляд камери), решта не рендеряться
        std::vector<DrawItem> items;
        float tanHalfH = tanHalfV * aspect * 1.15f; // з запасом на нахил камери
        const float Rt = TILE_SIZE * 0.75f;
        int loadedChunks = 0;
        for (int cz = 0; cz < GRID; cz++)
            for (int cx = 0; cx < GRID; cx++) {
                Chunk& c = g_chunks[cz][cx];
                if (!c.loaded) continue;
                loadedChunks++;
                for (int tz = 0; tz < TILES_PER_CH; tz++)
                    for (int tx = 0; tx < TILES_PER_CH; tx++) {
                        Tile& t = c.tiles[tz * TILES_PER_CH + tx];
                        int wx0 = cx * CHUNK_SIZE + tx * TILE_SIZE;
                        int wz0 = cz * CHUNK_SIZE + tz * TILE_SIZE;
                        float dx = wx0 + TILE_SIZE * 0.5f - g_px;
                        float dz = wz0 + TILE_SIZE * 0.5f - g_pz;
                        float d = sqrtf(dx * dx + dz * dz);
                        if (d > FOG_END + 400.f) { if (t.list) freeTile(t); continue; }
                        if (d > FOG_END + Rt) continue;
                        if (d > Rt * 2) { // поряд із гравцем завжди малюємо
                            float zf = dx * fx + dz * fz;
                            float xr = dx * rx + dz * rz;
                            if (zf < -Rt) continue;
                            if (fabsf(xr) > zf * tanHalfH + Rt * 1.5f) continue;
                        }
                        items.push_back({ d, &t, wx0, wz0, pickLod(d) });
                    }
            }

        // ---- будуємо найближчі потрібні плитки (бюджет на кадр)
        std::sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) { return a.dist < b.dist; });
        int built = 0;
        for (DrawItem& it : items) {
            if (it.t->lod == it.lod && it.t->list) continue;
            if (built >= BUILD_BUDGET) break;
            if (it.t->list) glDeleteLists(it.t->list, 1);
            it.t->list = buildTile(it.wx0, it.wz0, it.lod);
            it.t->lod = it.lod;
            built++;
        }
        int drawn = 0;
        for (DrawItem& it : items)
            if (it.t->list) { glCallList(it.t->list); drawn++; }

        // ---- предмети: спочатку непрозорі, потім скло з прозорістю
        const float maxD2 = FOG_END * FOG_END;
        int bodiesDrawn = 0;
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            }
            for (const phys::Body& b : g_world.bodies) {
                if (!b.alive || phys::MATS[b.mat].glass != (pass == 1)) continue;
                float dx = b.pos.x - g_px, dz = b.pos.z - g_pz;
                if (dx * dx + dz * dz > maxD2) continue;
                drawBody(b, b.id == g_world.heldId, pass == 1 ? 0.55f : 1.f);
                bodiesDrawn++;
            }
            if (pass == 1) { glDepthMask(GL_TRUE); glDisable(GL_BLEND); }
        }

        float tHit;
        bool aim = g_world.raycast(g_world.eye, g_world.fwd, 5.f, tHit) >= 0;
        drawCrosshair(fbw, fbh, g_world.heldId != 0, aim);

        glfwSwapBuffers(win);

        // ---- заголовок вікна як HUD
        frames++;
        titleT += dt;
        if (titleT >= 0.5) {
            char buf[320];
            phys::Body* hb = g_world.held();
            char held[96] = "";
            if (hb) snprintf(held, sizeof held, " | тримаєш: %s, %.0f кг", phys::MATS[hb->mat].name, hb->mass);
            snprintf(buf, sizeof buf,
                     "XYZ %.0f %.0f %.0f | чанк (%d,%d) [завантажено: %d] | плиток: %d | тіл: %d | %.0f FPS%s%s%s%s",
                     g_px, g_py, g_pz, (int)(g_px / CHUNK_SIZE), (int)(g_pz / CHUNK_SIZE),
                     loadedChunks, drawn, bodiesDrawn, frames / titleT, g_fly ? " | FLY" : "", held,
                     g_msgT > 0 ? " | " : "", g_msgT > 0 ? g_msg : "");
            glfwSetWindowTitle(win, buf);
            frames = 0; titleT = 0;
        }
    }
    glfwTerminate();
    return 0;
}
