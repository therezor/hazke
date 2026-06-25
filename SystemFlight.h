#pragma once
#include <M5GFX.h>
#include <math.h>
#include <stdint.h>
#include "Config.h"
#include "GameState.h"
#include "Galaxy.h"
#include "SolarSystem.h"
#include "NPCShip.h"
#include "Ship3D.h"
#include "Combat.h"
#include "Particles.h"
#include "Missile.h"
#include "Faction.h"
#include "Rank.h"
#include "Audio.h"

// Refit R11: in-system free flight.
//
// Player has a position and heading in system units (sysu). Throttle moves
// forward in the heading direction; pitch input pitches the heading
// vertically; roll input yaws the heading horizontally (arcade flight feel,
// no separate roll axis affecting the camera).
//
// Each frame we:
//   1. integrate input → orientation → position
//   2. project the active system's POIs into camera space
//   3. paint them as colored markers behind the cockpit overlay
//   4. draw a top-down minimap inset + a "nearest POI" HUD line
//
// All docked services live on planet surfaces — orbital stations were
// removed along with the old Coriolis docking mini-game.

namespace SystemFlight {

using Galaxy::lcg;   // shared LCG used by belt rock spawning

constexpr float NearZ          = 50.0f;   // sysu, behind-camera cull plane
constexpr float MaxSpeed       = 600.0f;  // sysu/sec at full throttle
constexpr float FocalLen       = 110.0f;  // perspective focal length for the cockpit view

// R12 set-course warp.
constexpr float WarpSpeed      = 7000.0f; // sysu/sec — ~5.7× throttle max
constexpr float WarpDropRange  = 3000.0f; // sysu — drop out when this close
constexpr float WarpMinRange   = 1000.0f; // sysu — refuse to engage if closer
constexpr float WarpStarSpeed  = 4.0f;    // throttle equiv. for streaking stars

// R13 planet landing.
// Visual body sizes. The icosphere world radius for planets and the sun
// is `p.radius * VisualScale`. Planets render at their full physical
// radius (1×) so they read as proper worlds at a glance; the sun is
// blown up further so it dominates the system from anywhere inside it.
constexpr float PlanetVisualScale = 1.0f;
constexpr float StarVisualScale   = 2.0f;

// Planet collision. The rendered icosphere is treated as a solid sphere.
// The cockpit gets a small skin so a tangent brush of the surface is
// enough to land, but nothing closer than the surface itself is needed.
// The motion integrator (update()) clamps the player to the outside of
// the sphere if a fast frame would otherwise teleport them through the
// body — that clamp also fires the landing hand-off so contact == land.
constexpr float LandingSkin  = 30.0f;   // sysu of "atmosphere" past surface
constexpr float LandingRange = 2000.0f; // kept for HUD prompt math

// Jump gate. Within GateApproachRange the cockpit shows a prompt; once
// the player actually flies into the gate (GateContactRange) the chart
// opens automatically — that's the only way to leave the system.
constexpr float GateApproachRange = 1500.0f;
constexpr float GateContactRange  = 350.0f;

// Sun heat. The star sits at the origin; `sunProximity()` returns a
// 0..1 factor describing how deep into the star's atmosphere the
// player is. Three zones, no more accumulator:
//   prox <  WarnFrac     — nothing on screen
//   prox >= WarnFrac     — HEAT indicator, still no damage (lead-up)
//   prox >= DamageFrac   — direct hull damage, rate scales with prox
//   prox >= FatalFrac    — instant death (you flew into the corona)
constexpr float SunHeatBuffer     = 6000.0f;  // sysu of atmosphere above the star
constexpr float SunCriticalBuffer = 600.0f;   // inside this -> prox = 1
constexpr float SunWarnFrac       = 0.25f;
constexpr float SunDamageFrac     = 0.55f;
constexpr float SunFatalFrac      = 0.95f;
constexpr float HeatDamageMaxRate = 0.85f;    // hull/sec at the inner edge of the damage band

// R14 asteroid belts. A belt is a toroidal volume around its parent
// planet (only on planets with PoiFlagBelt set). Rocks are ephemeral
// scenery — we keep a tiny per-belt cache and respawn it when the
// active belt changes.
constexpr int   NumRocks       = 24;
constexpr float BeltInnerExtra = 600.0f;   // inner radius = planet.radius + this
constexpr float BeltWidth      = 1800.0f;
constexpr float BeltHalfHeight = 250.0f;

// Orientation uses a forward + up unit vector pair (camera basis). The
// right axis is derived as right = up × forward. Pitch input rotates the
// pair around the right axis (local nose-up/down); roll input rotates
// up around forward (local bank). Combined banking + pitching produces
// Elite-style coordinated turns.
struct Kin {
  float px, py, pz;   // position, sysu
  float fx, fy, fz;   // forward unit vector
  float ux, uy, uz;   // up unit vector
  bool  initialized;
  int   loadedSys;    // which system layout we currently cache
  int   targetIdx;    // R12: user-selected POI (Tab cycles, J warps to it)
  bool  warping;
  int   lockedNPC;    // R21: NPC slot currently locked for missile fire (-1)
  bool  dying;        // set when hull hits 0 (collisions / lasers / sun)
  float deathTimer;   // seconds remaining in death animation
  bool  deathFinished;// set when timer expires; outer loop transitions to GameOver
};

constexpr float DeathAnimTime = 2.2f;   // seconds

inline Kin state = {
  0, 0, 0,         // position
  0.0f, 0.0f, 1.0f, // forward = +Z
  0.0f, 1.0f, 0.0f, // up = +Y
  false, -1, 0, false, -1,
  false, 0.0f, false
};

// Re-orthonormalize the camera basis. Tiny drift accumulates each frame;
// this keeps fwd/up unit length and perpendicular.
inline void renormalize() {
  float fl = sqrtf(state.fx*state.fx + state.fy*state.fy + state.fz*state.fz);
  if (fl > 1e-6f) { state.fx /= fl; state.fy /= fl; state.fz /= fl; }
  // up -= (up·fwd) fwd
  float d = state.ux*state.fx + state.uy*state.fy + state.uz*state.fz;
  state.ux -= d * state.fx;
  state.uy -= d * state.fy;
  state.uz -= d * state.fz;
  float ul = sqrtf(state.ux*state.ux + state.uy*state.uy + state.uz*state.uz);
  if (ul > 1e-6f) { state.ux /= ul; state.uy /= ul; state.uz /= ul; }
}

// R21: missile lock tuning — same cone as laser aim is too tight; use a
// fatter cone so the player can sweep a lock without precise tracking.
constexpr float LockConeRad = 0.45f;   // ~26° half-angle
// Lock range generous enough to grab anything inside the system cube —
// the cone is the real selectivity, distance shouldn't be a blocker.
constexpr float LockMaxRange = 30000.0f;

// Belt rock cache. `activeBelt` is the POI index of the planet whose
// belt we're currently inside, or -1.
struct Rock { int16_t rx, ry, rz; };  // position relative to parent planet
inline Rock rocks[NumRocks];
inline int  activeBelt = -1;

// Forward decls — belt helpers live further down so they can call
// toCamera, but `update()` above needs to call updateBelt(). The
// landing/gate probes are also defined later but called from renderHUD.
inline void updateBelt();
inline bool inBelt();
inline int  planetInLandingRange();
inline int  gateInRange();
inline bool nearGate();
inline SolarSystem::Layout layout;   // cached for the loaded system

// R26 sun-heat helpers. The star is the Star POI sitting at the origin;
// we cache its radius once after layoutFor for cheap proximity checks.
inline float starRadius() {
  for (int i = 0; i < layout.numPOIs; i++) {
    if (layout.poi[i].type == SolarSystem::POIType::Star) {
      return (float)layout.poi[i].radius;
    }
  }
  return 0.0f;
}

// Returns the player's "proximity factor" to the star, 0..1.
//   0 -> outside the heat buffer entirely (no warming)
//   1 -> grazing the surface (or inside the body)
inline float sunProximity() {
  float sr = starRadius();
  if (sr <= 1.0f) return 0.0f;
  float d  = sqrtf(state.px * state.px + state.py * state.py + state.pz * state.pz);
  float outer = sr + SunHeatBuffer;
  float inner = sr + SunCriticalBuffer;
  if (d >= outer) return 0.0f;
  if (d <= inner) return 1.0f;
  return (outer - d) / (outer - inner);
}

// ---------- Setup ----------

// Spawn flavor — only one variant now that stations are gone:
//   AtGate — drop at the jump gate, facing the star (witchspace arrival,
//            cold-launch with no save context). Kept as an enum so future
//            spawn flavors can slot in.
enum class SpawnAt : uint8_t { AtGate };

inline void enter(int sysIdx, SpawnAt /*where*/) {
  SolarSystem::layoutFor(sysIdx, layout);
  state.loadedSys = sysIdx;
  activeBelt = -1;   // invalidate belt cache on system switch / respawn
  NPCShip::spawnFor(sysIdx, layout);
  Combat::resetFlashes();
  Missile::resetAll();
  Particles::reset();
  state.lockedNPC      = -1;
  state.dying          = false;
  state.deathFinished  = false;
  state.deathTimer     = 0.0f;

  int gateI = -1;
  for (int i = 0; i < layout.numPOIs; i++) {
    if (layout.poi[i].type == SolarSystem::POIType::JumpGate) {
      gateI = i;
      break;
    }
  }

  auto faceTowardOrigin = [&]() {
    float dx = -state.px, dy = -state.py, dz = -state.pz;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 1.0f) { state.fx = 0.0f; state.fy = 0.0f; state.fz = 1.0f; }
    else            { state.fx = dx/len; state.fy = dy/len; state.fz = dz/len; }
    state.ux = 0.0f; state.uy = 1.0f; state.uz = 0.0f;
    renormalize();
  };

  if (gateI >= 0) {
    const auto& g = layout.poi[gateI];
    state.px = (float)g.x * 0.9f;
    state.py = (float)g.y;
    state.pz = (float)g.z * 0.9f;
    faceTowardOrigin();
  } else {
    state.px = 18000.0f; state.py = 0.0f; state.pz = 0.0f;
    state.fx = 0.0f; state.fy = 0.0f; state.fz = 1.0f;
    state.ux = 0.0f; state.uy = 1.0f; state.uz = 0.0f;
  }
  state.warping = false;

  // No marker on entry — the player picks a target from the map (or via
  // Tab) when they want one.
  state.targetIdx = -1;
  state.initialized = true;
}

// ---------- Target & warp ----------

// Distance in sysu from the player to the current target POI.
inline float targetDistance() {
  if (state.targetIdx < 0) return 0.0f;
  const auto& p = layout.poi[state.targetIdx];
  float dx = (float)p.x - state.px;
  float dy = (float)p.y - state.py;
  float dz = (float)p.z - state.pz;
  return sqrtf(dx*dx + dy*dy + dz*dz);
}

// Tab: pick the next non-star POI in layout order. Cycling also clears
// any NPC lock — the marker is one thing, so the bracket in the cockpit
// stays on a single target.
inline void cycleTarget() {
  if (layout.numPOIs <= 1) return;
  int start = state.targetIdx;
  int i = (start + 1) % layout.numPOIs;
  while (i != start) {
    if (layout.poi[i].type != SolarSystem::POIType::Star) {
      state.targetIdx  = i;
      state.lockedNPC  = -1;
      return;
    }
    i = (i + 1) % layout.numPOIs;
  }
}

// R21: pick the next NPC inside the forward LockConeRad / LockMaxRange.
// Walks slots starting after the current lock so repeated R presses cycle.
// Sets lockedNPC = -1 if no candidate is in front.
inline void cycleLock() {
  float fx = state.fx;
  float fy = state.fy;
  float fz = state.fz;
  float coneCos = cosf(LockConeRad);
  int start = (state.lockedNPC < 0) ? 0
                                    : (state.lockedNPC + 1) % NPCShip::MaxNPCs;
  for (int step = 0; step < NPCShip::MaxNPCs; step++) {
    int i = (start + step) % NPCShip::MaxNPCs;
    const auto& sh = NPCShip::ships[i];
    if (!sh.active) continue;
    if (i == state.lockedNPC) continue;
    float dx = sh.wx - state.px, dy = sh.wy - state.py, dz = sh.wz - state.pz;
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    if (dist < 1.0f || dist > LockMaxRange) continue;
    float dot = (dx * fx + dy * fy + dz * fz) / dist;
    if (dot < coneCos) continue;
    state.lockedNPC = i;
    // Marker is exclusive — drop the POI target so the cockpit shows
    // one bracket, not two.
    state.targetIdx = -1;
    return;
  }
  // No candidate — clear the lock so the HUD reflects it.
  state.lockedNPC = -1;
}

// R21: drop the lock if the locked NPC dies / is recycled.
inline void validateLock() {
  if (state.lockedNPC < 0) return;
  if (state.lockedNPC >= NPCShip::MaxNPCs ||
      !NPCShip::ships[state.lockedNPC].active) {
    state.lockedNPC = -1;
  }
}

// J: try to engage warp toward the current target. Refuses if already
// inside WarpMinRange (you're effectively there). Cancels any input.
inline bool engageWarp() {
  if (state.targetIdx < 0) return false;
  if (targetDistance() < WarpMinRange) return false;
  state.warping = true;
  return true;
}

inline void cancelWarp() { state.warping = false; }

// ---------- Kinematics ----------

// Trigger the death animation. Idempotent — safe to call from anywhere
// that subtracts hull (collisions, sun, lasers).
inline void triggerDeath() {
  if (state.dying) return;
  state.dying      = true;
  state.deathTimer = DeathAnimTime;
  Audio::explosion();
}

inline void update(GameState& g, float dt) {
  // Once the player is dead the world freezes — only the death-timer
  // and a slow tumble keep ticking. After it expires, the outer loop
  // flips to GameMode::GameOver.
  if (state.dying) {
    state.deathTimer -= dt;
    // Banking spiral: roll up around forward for that "spinning out"
    // feel without needing extra state.
    {
      float ca = cosf(1.8f * dt), sa = sinf(1.8f * dt);
      float rx = state.uy * state.fz - state.uz * state.fy;
      float ry = state.uz * state.fx - state.ux * state.fz;
      float rz = state.ux * state.fy - state.uy * state.fx;
      state.ux = state.ux * ca + rx * sa;
      state.uy = state.uy * ca + ry * sa;
      state.uz = state.uz * ca + rz * sa;
      renormalize();
    }
    // Periodic burst sparks at the cockpit so the world reads as still
    // very much alive (and the ship very much not).
    static float sparkCD = 0.0f;
    sparkCD -= dt;
    if (sparkCD <= 0.0f) {
      Particles::spawnBurst(state.px, state.py, state.pz, 0xFD20, 18);
      sparkCD = 0.18f;
    }
    if (state.deathTimer <= 0.0f) {
      Particles::spawnBurst(state.px, state.py, state.pz, 0xFFE0, 40);
      state.deathFinished = true;
    }
    return;
  }

  if (state.warping) {
    // Lock forward on the target and plough straight in.
    const auto& p = layout.poi[state.targetIdx];
    float dx = (float)p.x - state.px;
    float dy = (float)p.y - state.py;
    float dz = (float)p.z - state.pz;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len > 1.0f) {
      state.fx = dx / len;
      state.fy = dy / len;
      state.fz = dz / len;
      // Recompute world-up–aligned up vector.
      state.ux = 0.0f; state.uy = 1.0f; state.uz = 0.0f;
      renormalize();
    }
    state.px += state.fx * WarpSpeed * dt;
    state.py += state.fy * WarpSpeed * dt;
    state.pz += state.fz * WarpSpeed * dt;

    if (targetDistance() < WarpDropRange) state.warping = false;
    return;
  }

  // Local-axis rotation. pitchRate rotates forward+up around the right
  // axis (nose up/down in ship frame); rollRate rotates up around forward
  // (bank). Combined banking + pitching produces a coordinated turn.
  {
    // Pitch: rotate forward and up around right by ap.
    float ap = g.pitchRate * dt * 0.9f;
    float cp = cosf(ap), sp = sinf(ap);
    float nfx = state.fx * cp + state.ux * sp;
    float nfy = state.fy * cp + state.uy * sp;
    float nfz = state.fz * cp + state.uz * sp;
    float nux = -state.fx * sp + state.ux * cp;
    float nuy = -state.fy * sp + state.uy * cp;
    float nuz = -state.fz * sp + state.uz * cp;
    state.fx = nfx; state.fy = nfy; state.fz = nfz;
    state.ux = nux; state.uy = nuy; state.uz = nuz;
  }
  {
    // Roll: rotate up around forward by ar. right = up × fwd.
    float ar = g.rollRate * dt * 1.4f;
    float cr = cosf(ar), sr = sinf(ar);
    float rx = state.uy * state.fz - state.uz * state.fy;
    float ry = state.uz * state.fx - state.ux * state.fz;
    float rz = state.ux * state.fy - state.uy * state.fx;
    state.ux = state.ux * cr + rx * sr;
    state.uy = state.uy * cr + ry * sr;
    state.uz = state.uz * cr + rz * sr;
  }
  renormalize();

  // Hull damage cripples engines the same way it does for NPCs: below
  // 50% hull the ship can only manage half thrust.
  float speedMul = (g.hull < 0.5f) ? 0.5f : 1.0f;
  float v = g.speed * MaxSpeed * speedMul;
  state.px += state.fx * v * dt;
  state.py += state.fy * v * dt;
  state.pz += state.fz * v * dt;

  // Soft clamp inside the system cube so you can't fly forever — bouncing
  // back from the wall feels wrong; for now we just stop you crossing it.
  const float half = (float)SolarSystem::SystemHalfExtent;
  if (state.px >  half) state.px =  half;
  if (state.px < -half) state.px = -half;
  if (state.py >  half) state.py =  half;
  if (state.py < -half) state.py = -half;
  if (state.pz >  half) state.pz =  half;
  if (state.pz < -half) state.pz = -half;

  // Planet collision. Each rendered icosphere is a hard sphere — if the
  // post-integration position ended up inside one, snap the cockpit to
  // the contact shell. The outer SystemFlight loop reads
  // planetInLandingRange() right after update() returns and hands off to
  // LandingScreen, so this clamp also makes "touch the body" the actual
  // landing trigger.
  for (int i = 0; i < layout.numPOIs; i++) {
    const auto& pp = layout.poi[i];
    if (pp.type != SolarSystem::POIType::Planet) continue;
    float dx = state.px - (float)pp.x;
    float dy = state.py - (float)pp.y;
    float dz = state.pz - (float)pp.z;
    float d2 = dx*dx + dy*dy + dz*dz;
    float visualR = (float)pp.radius * PlanetVisualScale;
    float surface = visualR + LandingSkin;
    if (d2 < surface * surface) {
      float d = sqrtf(d2);
      if (d > 1.0f) {
        float k = surface / d;
        state.px = (float)pp.x + dx * k;
        state.py = (float)pp.y + dy * k;
        state.pz = (float)pp.z + dz * k;
      } else {
        // Cockpit landed exactly on center (numerically unlikely).
        state.px = (float)pp.x + surface;
        state.py = (float)pp.y;
        state.pz = (float)pp.z;
      }
      break;   // first contact is enough; landing will fire this frame
    }
  }

  // R14: refresh belt cache once per frame (post-movement so detection
  // and rendering see the same player position).
  updateBelt();

    // Sun proximity → indicator + damage. hullHeat just mirrors the
  // current proximity factor (0..1) so HUD code can read it; the
  // damage path now bypasses shields entirely and scales by depth.
  {
    float prox = sunProximity();
    g.hullHeat = prox;

    // Periodic alarm chirp once you're past the warning threshold.
    static float alarmCooldown = 0.0f;
    alarmCooldown -= dt;
    if (prox >= SunWarnFrac && alarmCooldown <= 0.0f) {
      Audio::alarm();
      alarmCooldown = 1.0f;
    }

    if (prox >= SunFatalFrac) {
      // Flew straight into the corona — done immediately.
      g.hull = 0.0f;
    } else if (prox >= SunDamageFrac) {
      // Damage rate ramps from 0 at SunDamageFrac to HeatDamageMaxRate
      // at SunFatalFrac. Hit the hull directly — no shields to soak
      // sun damage.
      float t = (prox - SunDamageFrac) / (SunFatalFrac - SunDamageFrac);
      if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
      g.hull -= HeatDamageMaxRate * t * dt;
      if (g.hull < 0.0f) g.hull = 0.0f;
    }
  }

  // R22: precompute per-slot patrol hostility from the player's faction
  // standing. Patrols of a faction the player has soured (≤ -30) chase
  // and fire like pirates; otherwise they wander peacefully. System 0
  // is the friendly starter zone — patrols there never engage.
  bool patrolHostile[NPCShip::MaxNPCs];
  const bool friendlySystem = (state.loadedSys == 0);
  for (int i = 0; i < NPCShip::MaxNPCs; i++) {
    const auto& sh = NPCShip::ships[i];
    patrolHostile[i] = sh.active
                    && sh.role == NPCShip::Role::Patrol
                    && !friendlySystem
                    && Faction::patrolWouldAttack(g, (Faction::Id)sh.homeFaction);
  }

  // R16/R19: tick NPC AI (traders, pirates, patrols). Pass the player
  // position so Pirate role can chase.
  NPCShip::update(layout, dt, state.px, state.py, state.pz, patrolHostile);

  // Ship-on-ship collisions. If an NPC overlaps the player's bubble,
  // both take direct hull damage and the NPC counts itself provoked.
  // Damage is gated by a short timer so a sustained overlap doesn't
  // delete the player in a single frame burst.
  static float collisionCD = 0.0f;
  if (collisionCD > 0.0f) collisionCD -= dt;
  constexpr float CollisionRadius = 280.0f;          // sysu
  constexpr float CollisionDamage = 0.18f;           // direct hull, both sides
  for (int i = 0; i < NPCShip::MaxNPCs; i++) {
    auto& sh = NPCShip::ships[i];
    if (!sh.active) continue;
    float dx = sh.wx - state.px;
    float dy = sh.wy - state.py;
    float dz = sh.wz - state.pz;
    float d2 = dx*dx + dy*dy + dz*dz;
    if (d2 > CollisionRadius * CollisionRadius) continue;
    if (collisionCD <= 0.0f) {
      Combat::damagePlayerHull(g, CollisionDamage);
      Audio::collisionThump();
      collisionCD = 0.45f;
    }
    sh.hull    -= CollisionDamage;
    sh.provoked = true;
    if (sh.hull <= 0.0f) {
      // Ram kills count the same as a clean laser kill: bounty,
      // kill counter, faction shift, quest progress, rank check.
      Combat::registerPlayerKill(g, sh);
    }
    // Shove the NPC outward so they don't sit clipped inside the player.
    float dist = sqrtf(d2);
    if (dist > 1.0f) {
      float push = (CollisionRadius - dist) + 30.0f;
      sh.wx += dx / dist * push;
      sh.wy += dy / dist * push;
      sh.wz += dz / dist * push;
    }
  }

  // Any damage source that emptied the hull this frame ignites the
  // death animation. The check sits at the end of update so all damage
  // paths above feed into it.
  if (g.hull <= 0.0f) triggerDeath();
}

// ---------- Projection ----------

// Transform world-space delta `dp` into camera space; returns true if the
// resulting cz is in front of the near plane (visible).
inline bool toCamera(float dpx, float dpy, float dpz,
                     float& cx, float& cy, float& cz) {
  // right = up × forward
  float rx = state.uy * state.fz - state.uz * state.fy;
  float ry = state.uz * state.fx - state.ux * state.fz;
  float rz = state.ux * state.fy - state.uy * state.fx;
  cx = dpx * rx       + dpy * ry       + dpz * rz;
  cy = dpx * state.ux + dpy * state.uy + dpz * state.uz;
  cz = dpx * state.fx + dpy * state.fy + dpz * state.fz;
  return cz > NearZ;
}

inline bool projectPOI(const SolarSystem::POI& p, int& sx, int& sy, float& outCz) {
  float dpx = (float)p.x - state.px;
  float dpy = (float)p.y - state.py;
  float dpz = (float)p.z - state.pz;
  float cx, cy, cz;
  if (!toCamera(dpx, dpy, dpz, cx, cy, cz)) return false;
  outCz = cz;
  const int vx = Config::ViewX + Config::ViewW / 2;
  const int vy = Config::ViewY + Config::ViewH / 2;
  sx = vx + (int)(cx * FocalLen / cz);
  sy = vy - (int)(cy * FocalLen / cz);
  return true;
}

// ---------- Rendering ----------

// Per-planet tint picked from a small palette by (flags + subIdx). Gives
// each planet a recognizable color without authoring per-world art.
inline uint16_t planetColor(const SolarSystem::POI& p) {
  static const uint16_t pal[8] = {
    0xFD00, // amber
    0x07FF, // cyan
    0x5FE0, // pale green
    0xF81F, // magenta
    0xFFE0, // yellow
    0xFC10, // coral
    0x867F, // sky blue
    0xCE59, // tan
  };
  return pal[(uint8_t)(p.flags * 3u + p.subIdx * 5u) & 7u];
}

// Multiply an RGB565 color by k ∈ [0..1] for cheap Lambert shading. A
// small floor keeps the dark side from going pure black.
inline uint16_t shadeColor565(uint16_t c, float k) {
  if (k < 0.12f) k = 0.12f;
  if (k > 1.0f)  k = 1.0f;
  int r = (c >> 11) & 0x1F;
  int gg = (c >> 5)  & 0x3F;
  int b = c          & 0x1F;
  r  = (int)((float)r  * k);
  gg = (int)((float)gg * k);
  b  = (int)((float)b  * k);
  return (uint16_t)((r << 11) | (gg << 5) | b);
}

// Subdivided icosphere (12 + 30 = 42 verts, 20 * 4 = 80 tris). The base
// icosahedron is too faceted for close approach — one round of edge
// midpoint splitting smooths it out without the per-frame cost of a
// proper subdivision surface. Built lazily on first use and cached.
constexpr int IcoSubVerts = 42;
constexpr int IcoSubFaces = 80;
inline float   icoSubV[IcoSubVerts][3];
inline uint8_t icoSubF[IcoSubFaces][3];
inline bool    icoSubReady = false;

inline void buildIcoSubdivided() {
  static const float A = 0.5257311f;
  static const float B = 0.8506508f;
  static const float baseV[12][3] = {
    {-A,  B,  0.0f}, { A,  B,  0.0f}, {-A, -B,  0.0f}, { A, -B,  0.0f},
    { 0.0f, -A,  B}, { 0.0f,  A,  B}, { 0.0f, -A, -B}, { 0.0f,  A, -B},
    {  B,  0.0f, -A}, {  B,  0.0f,  A}, { -B,  0.0f, -A}, { -B,  0.0f,  A},
  };
  static const uint8_t baseF[20][3] = {
    { 0,11, 5}, { 0, 5, 1}, { 0, 1, 7}, { 0, 7,10}, { 0,10,11},
    { 1, 5, 9}, { 5,11, 4}, {11,10, 2}, {10, 7, 6}, { 7, 1, 8},
    { 3, 9, 4}, { 3, 4, 2}, { 3, 2, 6}, { 3, 6, 8}, { 3, 8, 9},
    { 4, 9, 5}, { 2, 4,11}, { 6, 2,10}, { 8, 6, 7}, { 9, 8, 1},
  };

  for (int i = 0; i < 12; i++) {
    icoSubV[i][0] = baseV[i][0];
    icoSubV[i][1] = baseV[i][1];
    icoSubV[i][2] = baseV[i][2];
  }

  // Tiny edge-to-midpoint dedup. The icosahedron has 30 unique edges; a
  // 32-slot scratch is plenty.
  struct EdgeMid { uint8_t lo, hi, mid; };
  EdgeMid edges[32];
  int     numEdges = 0;
  int     nextVert = 12;

  auto midOf = [&](uint8_t a, uint8_t b) -> uint8_t {
    uint8_t lo = a < b ? a : b;
    uint8_t hi = a < b ? b : a;
    for (int e = 0; e < numEdges; e++) {
      if (edges[e].lo == lo && edges[e].hi == hi) return edges[e].mid;
    }
    float mx = (icoSubV[lo][0] + icoSubV[hi][0]) * 0.5f;
    float my = (icoSubV[lo][1] + icoSubV[hi][1]) * 0.5f;
    float mz = (icoSubV[lo][2] + icoSubV[hi][2]) * 0.5f;
    float ln = sqrtf(mx*mx + my*my + mz*mz);
    if (ln > 1e-6f) { mx /= ln; my /= ln; mz /= ln; }
    icoSubV[nextVert][0] = mx;
    icoSubV[nextVert][1] = my;
    icoSubV[nextVert][2] = mz;
    edges[numEdges].lo  = lo;
    edges[numEdges].hi  = hi;
    edges[numEdges].mid = (uint8_t)nextVert;
    numEdges++;
    return (uint8_t)nextVert++;
  };

  int nf = 0;
  for (int f = 0; f < 20; f++) {
    uint8_t a = baseF[f][0], b = baseF[f][1], c = baseF[f][2];
    uint8_t mAB = midOf(a, b);
    uint8_t mBC = midOf(b, c);
    uint8_t mCA = midOf(c, a);
    icoSubF[nf][0] = a;   icoSubF[nf][1] = mAB; icoSubF[nf][2] = mCA; nf++;
    icoSubF[nf][0] = b;   icoSubF[nf][1] = mBC; icoSubF[nf][2] = mAB; nf++;
    icoSubF[nf][0] = c;   icoSubF[nf][1] = mCA; icoSubF[nf][2] = mBC; nf++;
    icoSubF[nf][0] = mAB; icoSubF[nf][1] = mBC; icoSubF[nf][2] = mCA; nf++;
  }
  icoSubReady = true;
}

// Project all 42 verts of the subdivided icosphere into screen space, then
// fill the visible faces shaded by `shader`. Shared between planet and
// star renderers so they stay in sync mesh-wise.
template <typename Shader>
inline void renderIcoSphere(M5Canvas& g,
                            float pcx_w, float pcy_w, float pcz_w,
                            float radius, Shader shader) {
  if (!icoSubReady) buildIcoSubdivided();

  // Verts are accepted down to a much tighter plane than the POI cull
  // (NearZ = 50): on a close approach the body spans verts sitting just
  // in front of the camera, and dropping their faces punches holes in
  // the sphere. Projected coords are clamped so a vert grazing the
  // plane can't hand the rasterizer a megapixel triangle.
  struct V { int sx, sy; bool vis; };
  V pv[IcoSubVerts];
  const int vxc = Config::ViewX + Config::ViewW / 2;
  const int vyc = Config::ViewY + Config::ViewH / 2;
  constexpr float BodyNearZ  = 6.0f;
  constexpr int   CoordLimit = 3000;
  for (int i = 0; i < IcoSubVerts; i++) {
    float dpx = pcx_w + icoSubV[i][0] * radius - state.px;
    float dpy = pcy_w + icoSubV[i][1] * radius - state.py;
    float dpz = pcz_w + icoSubV[i][2] * radius - state.pz;
    float cx, cy, cz;
    toCamera(dpx, dpy, dpz, cx, cy, cz);
    pv[i].vis = (cz > BodyNearZ);
    if (pv[i].vis) {
      int sxv = vxc + (int)(cx * FocalLen / cz);
      int syv = vyc - (int)(cy * FocalLen / cz);
      if (sxv < -CoordLimit) sxv = -CoordLimit;
      if (sxv >  CoordLimit) sxv =  CoordLimit;
      if (syv < -CoordLimit) syv = -CoordLimit;
      if (syv >  CoordLimit) syv =  CoordLimit;
      pv[i].sx = sxv;
      pv[i].sy = syv;
    }
  }

  // View direction (planet → camera, world space).
  float vdx = state.px - pcx_w;
  float vdy = state.py - pcy_w;
  float vdz = state.pz - pcz_w;
  float vdLen = sqrtf(vdx*vdx + vdy*vdy + vdz*vdz);
  if (vdLen > 0.001f) { vdx/=vdLen; vdy/=vdLen; vdz/=vdLen; }

  for (int f = 0; f < IcoSubFaces; f++) {
    int ia = icoSubF[f][0], ib = icoSubF[f][1], ic = icoSubF[f][2];
    if (!pv[ia].vis || !pv[ib].vis || !pv[ic].vis) continue;
    // Face normal: centroid of unit-sphere verts points outward.
    float nx = icoSubV[ia][0] + icoSubV[ib][0] + icoSubV[ic][0];
    float ny = icoSubV[ia][1] + icoSubV[ib][1] + icoSubV[ic][1];
    float nz = icoSubV[ia][2] + icoSubV[ib][2] + icoSubV[ic][2];
    float nl = sqrtf(nx*nx + ny*ny + nz*nz);
    if (nl < 0.001f) continue;
    nx /= nl; ny /= nl; nz /= nl;
    float vd = nx*vdx + ny*vdy + nz*vdz;
    if (vd <= 0.0f) continue;     // backface
    uint16_t col = shader(nx, ny, nz, vd);
    g.fillTriangle(pv[ia].sx, pv[ia].sy,
                   pv[ib].sx, pv[ib].sy,
                   pv[ic].sx, pv[ic].sy, col);
  }
}

// Planet body — Lambert shading lit from the actual star at the system
// origin, so the day side always faces the sun and the terminator moves
// correctly as the player circles the body.
inline void renderPlanet3D(M5Canvas& g,
                           float pcx_w, float pcy_w, float pcz_w,
                           float radius, uint16_t baseColor) {
  float lx = -pcx_w, ly = -pcy_w, lz = -pcz_w;
  float ll = sqrtf(lx*lx + ly*ly + lz*lz);
  if (ll > 1.0f) { lx /= ll; ly /= ll; lz /= ll; }
  else           { lx = 0.408f; ly = 0.408f; lz = -0.816f; }
  renderIcoSphere(g, pcx_w, pcy_w, pcz_w, radius,
                  [&](float nx, float ny, float nz, float /*vd*/) {
    float ldot = nx*lx + ny*ly + nz*lz;
    if (ldot < 0.0f) ldot = 0.0f;
    float k = 0.18f + 0.82f * ldot;
    return shadeColor565(baseColor, k);
  });
}

// Star — self-illuminated with limb darkening (sub-camera point burns
// white, edges fade through yellow into amber).
inline void renderStar3D(M5Canvas& g,
                         float pcx_w, float pcy_w, float pcz_w,
                         float radius) {
  renderIcoSphere(g, pcx_w, pcy_w, pcz_w, radius,
                  [](float /*nx*/, float /*ny*/, float /*nz*/, float vd) {
    float k = 0.55f + 0.45f * vd;
    if      (k > 0.92f) return (uint16_t)0xFFFB;   // near-white hot core
    else if (k > 0.78f) return (uint16_t)0xFFE0;   // yellow
    else                return (uint16_t)0xFD00;   // amber
  });
}

// 3D wireframe ring around a planet body. The ring sits in a plane
// spanned by `u` and `v` (world-space basis vectors). Inner + outer
// circles are drawn with N segments each, plus four spokes to give the
// ring a constructed look at distance. Each per-vertex point is
// projected through `toCamera` so the ring banks correctly as the
// player rolls or pitches. `bodyRadius` is the planet's solid icosphere
// radius — vertices behind the body are screen-space-occluded so the
// ring doesn't appear to pass through the globe.
inline void renderPlanetRing3D(M5Canvas& g,
                               float pcx_w, float pcy_w, float pcz_w,
                               float innerR, float outerR,
                               float ux, float uy, float uz,
                               float vx, float vy, float vz,
                               float bodyRadius,
                               uint16_t color) {
  constexpr int RingN = 18;
  int  sxO[RingN]; int syO[RingN]; bool visO[RingN];
  int  sxI[RingN]; int syI[RingN]; bool visI[RingN];
  const int vxc = Config::ViewX + Config::ViewW / 2;
  const int vyc = Config::ViewY + Config::ViewH / 2;

  // Planet center in camera space + screen-space disk for occlusion.
  float pcxc, pcyc, pczc;
  bool planetInFront = toCamera(pcx_w - state.px, pcy_w - state.py,
                                pcz_w - state.pz, pcxc, pcyc, pczc);
  int planetSx = 0, planetSy = 0;
  float bodyRSq = 0.0f;
  if (planetInFront) {
    planetSx = vxc + (int)(pcxc * FocalLen / pczc);
    planetSy = vyc - (int)(pcyc * FocalLen / pczc);
    float pr = bodyRadius * FocalLen / pczc;
    bodyRSq = pr * pr;
  }

  // Occlusion test: a ring vertex at screen (sx, sy) with camera-z `cz`
  // is hidden by the body if it sits behind the planet center's depth
  // (with a tiny bias so the silhouette edge stays clean) and falls
  // inside the planet's projected disk.
  auto occluded = [&](int sxv, int syv, float cz) -> bool {
    if (!planetInFront) return false;
    if (cz < pczc) return false;
    int dx = sxv - planetSx;
    int dy = syv - planetSy;
    return (float)(dx * dx + dy * dy) < bodyRSq;
  };

  for (int k = 0; k < RingN; k++) {
    float ang = (float)k * (6.2831853f / (float)RingN);
    float ca = cosf(ang), sa = sinf(ang);

    // Outer point.
    {
      float wx = pcx_w + ca * outerR * ux + sa * outerR * vx;
      float wy = pcy_w + ca * outerR * uy + sa * outerR * vy;
      float wz = pcz_w + ca * outerR * uz + sa * outerR * vz;
      float cx, cy, cz;
      if (toCamera(wx - state.px, wy - state.py, wz - state.pz, cx, cy, cz)) {
        sxO[k] = vxc + (int)(cx * FocalLen / cz);
        syO[k] = vyc - (int)(cy * FocalLen / cz);
        visO[k] = !occluded(sxO[k], syO[k], cz);
      } else visO[k] = false;
    }
    // Inner point.
    {
      float wx = pcx_w + ca * innerR * ux + sa * innerR * vx;
      float wy = pcy_w + ca * innerR * uy + sa * innerR * vy;
      float wz = pcz_w + ca * innerR * uz + sa * innerR * vz;
      float cx, cy, cz;
      if (toCamera(wx - state.px, wy - state.py, wz - state.pz, cx, cy, cz)) {
        sxI[k] = vxc + (int)(cx * FocalLen / cz);
        syI[k] = vyc - (int)(cy * FocalLen / cz);
        visI[k] = !occluded(sxI[k], syI[k], cz);
      } else visI[k] = false;
    }
  }

  for (int k = 0; k < RingN; k++) {
    int kn = (k + 1) % RingN;
    if (visO[k] && visO[kn]) {
      g.drawLine(sxO[k], syO[k], sxO[kn], syO[kn], color);
    }
    if (visI[k] && visI[kn]) {
      g.drawLine(sxI[k], syI[k], sxI[kn], syI[kn], color);
    }
    // Four spokes connecting inner to outer rim.
    if ((k % (RingN / 4)) == 0 && visO[k] && visI[k]) {
      g.drawLine(sxI[k], syI[k], sxO[k], syO[k], color);
    }
  }
}

// 3D belt halo around a planet. Scatters ~40 deterministic dots in the
// toroidal band between innerR and outerR (in the same plane as the
// ring), each projected through toCamera so they bank with the camera.
// Replaces the old 2D 6-dot ellipse indicator. `seed` is hashed from the
// planet POI index so dots stay put across frames.
inline void renderPlanetBelt3D(M5Canvas& g,
                               float pcx_w, float pcy_w, float pcz_w,
                               float innerR, float outerR,
                               float ux, float uy, float uz,
                               float vx, float vy, float vz,
                               float bodyRadius,
                               uint16_t color,
                               uint32_t seed) {
  constexpr int N = 40;
  uint32_t s = seed | 1u;
  const int vxc = Config::ViewX + Config::ViewW / 2;
  const int vyc = Config::ViewY + Config::ViewH / 2;

  // Planet disk for occluding particles passing behind the body.
  float pcxc, pcyc, pczc;
  bool planetInFront = toCamera(pcx_w - state.px, pcy_w - state.py,
                                pcz_w - state.pz, pcxc, pcyc, pczc);
  int planetSx = 0, planetSy = 0;
  float bodyRSq = 0.0f;
  if (planetInFront) {
    planetSx = vxc + (int)(pcxc * FocalLen / pczc);
    planetSy = vyc - (int)(pcyc * FocalLen / pczc);
    float pr = bodyRadius * FocalLen / pczc;
    bodyRSq = pr * pr;
  }

  for (int i = 0; i < N; i++) {
    s = s * 1664525u + 1013904223u;
    float ang = (float)(s & 0xFFFFu) * (6.2831853f / 65536.0f);
    s = s * 1664525u + 1013904223u;
    float t  = (float)(s & 0xFFFFu) / 65536.0f;
    float radius = innerR + t * (outerR - innerR);
    float ca = cosf(ang), sa = sinf(ang);
    float wx = pcx_w + ca * radius * ux + sa * radius * vx;
    float wy = pcy_w + ca * radius * uy + sa * radius * vy;
    float wz = pcz_w + ca * radius * uz + sa * radius * vz;
    float cx, cy, cz;
    if (!toCamera(wx - state.px, wy - state.py, wz - state.pz, cx, cy, cz))
      continue;
    int sx = vxc + (int)(cx * FocalLen / cz);
    int sy = vyc - (int)(cy * FocalLen / cz);
    if (sx < Config::ViewX || sx >= Config::ViewX + Config::ViewW) continue;
    if (sy < Config::ViewY || sy >= Config::ViewY + Config::ViewH) continue;
    // Behind-body occlusion.
    if (planetInFront && cz > pczc) {
      int dx = sx - planetSx;
      int dy = sy - planetSy;
      if ((float)(dx * dx + dy * dy) < bodyRSq) continue;
    }
    g.drawPixel(sx, sy, color);
  }
}

// Per-planet tilt + ring-plane basis. Picks an axial tilt deterministic
// in (flags, subIdx) so every world keeps its look across reloads.
// Sets unit u (in-plane horizontal) and v (in-plane tilted vertical).
inline void planetRingBasis(const SolarSystem::POI& p,
                            float& ux, float& uy, float& uz,
                            float& vx, float& vy, float& vz) {
  // ~ -0.6..+0.6 rad (≈ ±34°) tilt around the world X axis, biased by
  // subIdx so each planet in a system reads distinct.
  float t = ((int)p.subIdx * 0.42f) + ((p.flags & 0x70) * 0.013f) - 0.6f;
  ux = 1.0f; uy = 0.0f; uz = 0.0f;
  vx = 0.0f; vy = -sinf(t); vz = cosf(t);
}

inline uint16_t poiColor(const SolarSystem::POI& p) {
  switch (p.type) {
    case SolarSystem::POIType::Star:     return TFT_YELLOW;
    case SolarSystem::POIType::Planet:   return planetColor(p);
    case SolarSystem::POIType::Station:  return TFT_CYAN;
    case SolarSystem::POIType::JumpGate: return TFT_MAGENTA;
  }
  return TFT_WHITE;
}

// Forward decls — render passes that live further down but are called
// from renderWorld at the top of the file.
inline void renderBelt(M5Canvas& g);
inline void renderNPCShips(M5Canvas& g);
inline void renderParticles(M5Canvas& g);
inline void renderLasers(M5Canvas& g);
inline void renderMissiles(M5Canvas& g);
inline void renderLockBracket(M5Canvas& g);
inline void renderTargetBracket(M5Canvas& g);

// Project + draw POIs in the viewport. Bigger, brighter for closer; tiny
// pixel for very distant. Markers drawn back-to-front (rough painter sort
// by descending cz).
inline void renderWorld(M5Canvas& g) {
  // Rocks first so POI markers and labels overlay them naturally.
  renderBelt(g);

  // First pass: project everything, remember cz for sorting. Bodies with
  // real volume (star / planets) get a cull margin that grows with their
  // projected radius — a screen-filling sphere must not vanish the moment
  // its *center* leaves the viewport. If the center sits beside or behind
  // the camera but the body is close enough that its limb can still wrap
  // into view, keep it anyway and sort by raw distance.
  struct Hit { int sx, sy; float cz; uint8_t i; };
  Hit hits[SolarSystem::MaxPOIs];
  int n = 0;
  const int vcx = Config::ViewX + Config::ViewW / 2;
  const int vcy = Config::ViewY + Config::ViewH / 2;
  for (int i = 0; i < layout.numPOIs; i++) {
    const auto& p = layout.poi[i];
    int sx, sy; float cz;
    bool centerOk = projectPOI(p, sx, sy, cz);
    float bodyScale = (p.type == SolarSystem::POIType::Star)   ? StarVisualScale
                    : (p.type == SolarSystem::POIType::Planet) ? PlanetVisualScale
                    : 0.0f;
    if (bodyScale > 0.0f) {
      float dx = (float)p.x - state.px;
      float dy = (float)p.y - state.py;
      float dz = (float)p.z - state.pz;
      float dist = sqrtf(dx*dx + dy*dy + dz*dz);
      float worldR = (float)p.radius * bodyScale;
      if (!centerOk) {
        if (dist < worldR * 1.6f + 2.0f * NearZ) {
          hits[n++] = { vcx, vcy, dist, (uint8_t)i };
        }
        continue;
      }
      int margin = 40 + (int)(worldR * FocalLen / cz);
      if (margin > 4000) margin = 4000;
      if (sx < Config::ViewX - margin || sx > Config::ViewX + Config::ViewW + margin) continue;
      if (sy < Config::ViewY - margin || sy > Config::ViewY + Config::ViewH + margin) continue;
      hits[n++] = { sx, sy, cz, (uint8_t)i };
      continue;
    }
    if (!centerOk) continue;
    if (sx < Config::ViewX - 40 || sx > Config::ViewX + Config::ViewW + 40) continue;
    if (sy < Config::ViewY - 40 || sy > Config::ViewY + Config::ViewH + 40) continue;
    hits[n++] = { sx, sy, cz, (uint8_t)i };
  }
  // Insertion sort by cz desc — n is tiny.
  for (int i = 1; i < n; i++) {
    Hit h = hits[i]; int j = i - 1;
    while (j >= 0 && hits[j].cz < h.cz) { hits[j+1] = hits[j]; j--; }
    hits[j+1] = h;
  }

  for (int k = 0; k < n; k++) {
    const auto& p  = layout.poi[hits[k].i];
    int sx = hits[k].sx, sy = hits[k].sy;
    float cz = hits[k].cz;
    uint16_t col = poiColor(p);

    // Raw perspective radius — used by Star/Gate/Station as-is, and divided
    // for planets so they read as star-like dots from afar and grow into a
    // disk only when you're close.
    int rawR = (int)((float)p.radius * FocalLen / cz);
    int r = rawR;          // body radius used for label offset below
    int labelR = 4;

    switch (p.type) {
      case SolarSystem::POIType::Star: {
        // Star is the biggest object in the system — twice its physical
        // radius in world units so it dominates the view. The icosphere
        // path kicks in early (sr ≥ 4) so it stops looking flat as soon
        // as it's more than a pixel cluster.
        float worldR = (float)p.radius * StarVisualScale;
        // Distance check first: the player can fly *inside* the visual
        // sphere while still alive (the fatal heat radius sits well
        // inside it). In there every face is a backface and the sun
        // would vanish — instead the photosphere fills the whole view.
        float ddx = (float)p.x - state.px;
        float ddy = (float)p.y - state.py;
        float ddz = (float)p.z - state.pz;
        float dist = sqrtf(ddx*ddx + ddy*ddy + ddz*ddz);
        if (dist < worldR + 2.0f * NearZ) {
          g.fillRect(Config::ViewX, Config::ViewY,
                     Config::ViewW, Config::ViewH, 0xFFFB);
          r = 160; labelR = 160;
          break;
        }
        int sr = (int)(worldR * FocalLen / cz);
        if (sr < 3) sr = 3;
        if (sr > 160) sr = 160;
        if (sr < 4) {
          g.fillCircle(sx, sy, sr, col);
          // Faint glow pixels so the distant star twinkles a little
          // brighter than a plain dot.
          g.drawPixel(sx - sr - 1, sy, 0x51A0);
          g.drawPixel(sx + sr + 1, sy, 0x51A0);
          g.drawPixel(sx, sy - sr - 1, 0x51A0);
          g.drawPixel(sx, sy + sr + 1, 0x51A0);
        } else {
          renderStar3D(g, (float)p.x, (float)p.y, (float)p.z, worldR);
        }
        r = sr; labelR = sr;
        break;
      }
      case SolarSystem::POIType::Planet: {
        // Planets render at their full physical radius (PlanetVisualScale
        // = 1.0). They start as a single bright cross when far enough
        // away to be sub-pixel and grow into a polygonal globe on
        // approach.
        float worldR = (float)p.radius * PlanetVisualScale;
        int pr = (int)(worldR * FocalLen / cz);
        if (pr > 64) pr = 64;
        // Ring + belt basis (same plane for both — visually consistent).
        float bux = 0, buy = 0, buz = 0, bvx = 0, bvy = 0, bvz = 0;
        bool hasRing = (p.flags & SolarSystem::PoiFlagRing) != 0;
        bool hasBelt = (p.flags & SolarSystem::PoiFlagBelt) != 0;
        if (hasRing || hasBelt) {
          planetRingBasis(p, bux, buy, buz, bvx, bvy, bvz);
        }
        if (pr < 1) {
          g.drawPixel(sx,     sy,     TFT_WHITE);
          g.drawPixel(sx + 1, sy,     col);
          g.drawPixel(sx - 1, sy,     col);
          g.drawPixel(sx,     sy + 1, col);
          g.drawPixel(sx,     sy - 1, col);
          r = 1; labelR = 3;
        } else if (pr < 5) {
          // Mid distance — flat shaded disk is cheaper and reads
          // cleanly when the icosphere would be only a few pixels.
          g.fillCircle(sx, sy, pr, col);
          g.drawPixel(sx, sy, TFT_WHITE);
          if (hasRing) {
            renderPlanetRing3D(g, (float)p.x, (float)p.y, (float)p.z,
                               worldR * 1.35f, worldR * 1.85f,
                               bux, buy, buz, bvx, bvy, bvz,
                               worldR, TFT_LIGHTGREY);
          }
          if (hasBelt) {
            renderPlanetBelt3D(g, (float)p.x, (float)p.y, (float)p.z,
                               (float)p.radius + 600.0f,
                               (float)p.radius + 2400.0f,
                               bux, buy, buz, bvx, bvy, bvz,
                               worldR,
                               TFT_DARKGREY,
                               (uint32_t)hits[k].i * 0x9E3779B1u);
          }
          r = pr; labelR = pr;
        } else {
          // Close approach — render a low-poly icosphere with Lambert
          // shading so the planet reads as a 3D body.
          renderPlanet3D(g, (float)p.x, (float)p.y, (float)p.z,
                         worldR, col);
          if (hasRing) {
            renderPlanetRing3D(g, (float)p.x, (float)p.y, (float)p.z,
                               worldR * 1.35f, worldR * 1.85f,
                               bux, buy, buz, bvx, bvy, bvz,
                               worldR, TFT_LIGHTGREY);
          }
          if (hasBelt) {
            renderPlanetBelt3D(g, (float)p.x, (float)p.y, (float)p.z,
                               (float)p.radius + 600.0f,
                               (float)p.radius + 2400.0f,
                               bux, buy, buz, bvx, bvy, bvz,
                               worldR,
                               TFT_DARKGREY,
                               (uint32_t)hits[k].i * 0x9E3779B1u);
          }
          r = pr; labelR = pr;
        }
        break;
      }
      case SolarSystem::POIType::Station:
        // Orbit stations removed — case kept only because the enum
        // value still exists; never spawned, never reached.
        break;
      case SolarSystem::POIType::JumpGate: {
        // 3D wireframe ring. The ring normal points back toward the
        // system origin so a player approaching from outside-system sees
        // the gate face-on. Vertices live in the plane spanned by
        // u = (-nz, 0, nx) and v = (0, 1, 0).
        float gx = (float)p.x, gy = (float)p.y, gz = (float)p.z;
        float gLen = sqrtf(gx * gx + gz * gz);
        float nxw = (gLen > 1.0f) ? -gx / gLen : 0.0f;
        float nzw = (gLen > 1.0f) ? -gz / gLen : 1.0f;
        float ux = -nzw, uz = nxw;       // ring plane horizontal axis
        constexpr int RingN = 12;
        int  rsx[RingN]; int rsy[RingN]; bool rvis[RingN];
        float ringR = (float)p.radius;
        for (int kk = 0; kk < RingN; kk++) {
          float ang = (float)kk * (6.2831853f / (float)RingN);
          float ca = cosf(ang), sa = sinf(ang);
          float wx = gx + ca * ringR * ux;
          float wy = gy + sa * ringR;     // ring centered on gate, world-up is v
          float wz = gz + ca * ringR * uz;
          float dpx = wx - state.px;
          float dpy = wy - state.py;
          float dpz = wz - state.pz;
          float cxx, cyy, czz;
          if (toCamera(dpx, dpy, dpz, cxx, cyy, czz)) {
            const int vxc = Config::ViewX + Config::ViewW / 2;
            const int vyc = Config::ViewY + Config::ViewH / 2;
            rsx[kk]  = vxc + (int)(cxx * FocalLen / czz);
            rsy[kk]  = vyc - (int)(cyy * FocalLen / czz);
            rvis[kk] = true;
          } else {
            rvis[kk] = false;
          }
        }
        // Outer ring in magenta — bright when close, dim when far.
        uint16_t rim = (cz < 4000.0f) ? TFT_MAGENTA : 0x4810;
        for (int kk = 0; kk < RingN; kk++) {
          int knx = (kk + 1) % RingN;
          if (rvis[kk] && rvis[knx]) {
            g.drawLine(rsx[kk], rsy[kk], rsx[knx], rsy[knx], rim);
          }
        }
        // Tiny inner cross at the gate's projected center — keeps the
        // gate spottable when it's a long way off and the ring is small.
        g.drawPixel(sx,     sy,     TFT_WHITE);
        g.drawPixel(sx - 1, sy,     rim);
        g.drawPixel(sx + 1, sy,     rim);
        g.drawPixel(sx,     sy - 1, rim);
        g.drawPixel(sx,     sy + 1, rim);

        int s = rawR < 4 ? 4 : rawR;
        if (s > 24) s = 24;
        r = s; labelR = s;
        break;
      }
    }

    // No floating labels in the viewport — the HUD shows the target
    // name top-left. Bodies and gates speak for themselves.
    (void)labelR;
  }

  // R16: NPCs drawn on top of POIs — they're small but should be visible
  // when they pass in front of a planet body.
  renderNPCShips(g);
  // Hit-spark particles ride on top of ship silhouettes so impacts read.
  renderParticles(g);
  // R20: laser flashes (player + NPC) overlay everything else.
  renderLasers(g);
  // R21: missile trails + lock bracket on the locked target.
  renderMissiles(g);
  renderTargetBracket(g);
  renderLockBracket(g);

  // Death overlay: tint the viewport red and pulse a "SHIP LOST"
  // banner while the death timer runs. Runs over everything else so
  // the player sees the fail state immediately.
  if (state.dying) {
    int vx0 = Config::ViewX;
    int vy0 = Config::ViewY;
    int vw  = Config::ViewW;
    int vh  = Config::ViewH;
    float prog = 1.0f - (state.deathTimer / DeathAnimTime);
    if (prog < 0.0f) prog = 0.0f; else if (prog > 1.0f) prog = 1.0f;
    // Striped red fill — quick & cheap, looks like a damage strobe.
    int stripe = 1 + (int)(prog * 5.0f);
    uint16_t col = 0xF800;
    for (int y = vy0; y < vy0 + vh; y += stripe + 1) {
      g.drawFastHLine(vx0, y, vw, col);
    }
    g.drawRect(vx0,     vy0,     vw,     vh,     col);
    g.drawRect(vx0 + 1, vy0 + 1, vw - 2, vh - 2, col);
    bool blink = ((millis() / 140u) & 1u) == 0u;
    if (blink) {
      const char* tag = "SHIP LOST";
      int tw = (int)strlen(tag) * 6;
      g.setTextSize(1);
      g.setTextColor(0xFFE0, TFT_BLACK);
      g.setCursor(vx0 + (vw - tw) / 2, vy0 + vh / 2 - 4);
      g.print(tag);
    }
  } else if (Combat::playerHitFlash > 0.0f) {
    int vx0 = Config::ViewX;
    int vy0 = Config::ViewY;
    int vw  = Config::ViewW;
    int vh  = Config::ViewH;
    g.drawRect(vx0,     vy0,     vw,     vh,     0xF800);
    g.drawRect(vx0 + 1, vy0 + 1, vw - 2, vh - 2, 0xF800);
    g.setTextSize(1);
    g.setTextColor(0xFFE0, TFT_BLACK);
    g.setCursor(vx0 + 4, vy0 + 4);
    g.print("HIT");
  }
}

// Cockpit-radar blips. Plots non-star POIs as colored dots on the
// elliptical 3D scanner that Cockpit::drawRadar3D already draws. POIs are
// placed by their direction relative to the player's heading (so the
// target ahead of you shows at the top of the scope), at a radial offset
// proportional to distance up to RadarRange. The little vertical line is
// the Elite-style altitude bar — positive cy (above plane) draws upward.
constexpr float RadarRange = 30000.0f;   // sysu, covers the full system cube
constexpr int   RadarCX    = Config::ScreenW / 2;
constexpr int   RadarCY    = Config::HudY + 14;
constexpr int   RadarRX    = 26;
constexpr int   RadarRY    = 11;

inline void renderRadarBlips(M5Canvas& g) {
  // Basis vectors (right = up × forward). Same transform as toCamera but
  // without the near-plane cull so POIs behind the ship show on the
  // bottom of the scope.
  float rxw = state.uy * state.fz - state.uz * state.fy;
  float ryw = state.uz * state.fx - state.ux * state.fz;
  float rzw = state.ux * state.fy - state.uy * state.fx;
  for (int i = 0; i < layout.numPOIs; i++) {
    const auto& p = layout.poi[i];
    if (p.type == SolarSystem::POIType::Star) continue;
    float dpx = (float)p.x - state.px;
    float dpy = (float)p.y - state.py;
    float dpz = (float)p.z - state.pz;
    float cx = dpx * rxw       + dpy * ryw       + dpz * rzw;
    float cy = dpx * state.ux  + dpy * state.uy  + dpz * state.uz;
    float cz = dpx * state.fx  + dpy * state.fy  + dpz * state.fz;

    float horiz = sqrtf(cx*cx + cz*cz);
    if (horiz < 1.0f) continue;
    float dNorm = horiz / RadarRange;
    if (dNorm > 1.0f) dNorm = 1.0f;
    float nx =  cx / horiz;
    float nz =  cz / horiz;
    int bx = RadarCX + (int)(nx * (RadarRX - 1) * dNorm);
    int by = RadarCY - (int)(nz * (RadarRY - 1) * dNorm);

    uint16_t col = (i == state.targetIdx) ? TFT_WHITE : poiColor(p);
    // Altitude bar — sysu per pixel.
    int v = (int)(cy / 1800.0f);
    if (v >  6) v =  6;
    if (v < -6) v = -6;
    if (v != 0) {
      int y0 = by, y1p = by - v;
      if (y0 > y1p) { int t = y0; y0 = y1p; y1p = t; }
      g.drawFastVLine(bx, y0, y1p - y0 + 1, col);
    }
    g.fillRect(bx - 1, by - 1, 3, 3, col);
  }

  // R16: NPC ships as 1-pixel blips in the ship's own color (no vert bar
  // — they're typically close to the player's altitude). Stays within
  // the radar ellipse via the same dNorm clamp.
  if (NPCShip::loadedSys == state.loadedSys) {
    for (int i = 0; i < NPCShip::MaxNPCs; i++) {
      const auto& sh = NPCShip::ships[i];
      if (!sh.active) continue;
      float dpx = sh.wx - state.px;
      float dpy = sh.wy - state.py;
      float dpz = sh.wz - state.pz;
      float cx = dpx * rxw      + dpy * ryw      + dpz * rzw;
      float cz = dpx * state.fx + dpy * state.fy + dpz * state.fz;
      float horiz = sqrtf(cx * cx + cz * cz);
      if (horiz < 1.0f) continue;
      float dNorm = horiz / RadarRange;
      if (dNorm > 1.0f) dNorm = 1.0f;
      int bx = RadarCX + (int)((cx / horiz) * (RadarRX - 1) * dNorm);
      int by = RadarCY - (int)((cz / horiz) * (RadarRY - 1) * dNorm);
      g.drawPixel(bx, by, sh.color);
      // R21: ring the locked NPC's blip so the player can spot it
      // even when the silhouette is off-screen.
      if (i == state.lockedNPC) {
        g.drawPixel(bx - 1, by, TFT_RED);
        g.drawPixel(bx + 1, by, TFT_RED);
        g.drawPixel(bx, by - 1, TFT_RED);
        g.drawPixel(bx, by + 1, TFT_RED);
      }
    }
  }

  // R21: in-flight missiles as their own blips so the player can see
  // incoming threats on the scope.
  for (int i = 0; i < Missile::MaxMissiles; i++) {
    const auto& m = Missile::pool[i];
    if (!m.active) continue;
    float dpx = m.wx - state.px;
    float dpy = m.wy - state.py;
    float dpz = m.wz - state.pz;
    float cx = dpx * rxw      + dpy * ryw      + dpz * rzw;
    float cz = dpx * state.fx + dpy * state.fy + dpz * state.fz;
    float horiz = sqrtf(cx * cx + cz * cz);
    if (horiz < 1.0f) continue;
    float dNorm = horiz / RadarRange;
    if (dNorm > 1.0f) dNorm = 1.0f;
    int bx = RadarCX + (int)((cx / horiz) * (RadarRX - 1) * dNorm);
    int by = RadarCY - (int)((cz / horiz) * (RadarRY - 1) * dNorm);
    g.drawPixel(bx, by, m.color);
  }
}

// Top-left HUD line: target POI name and distance. Color shifts to cyan
// during warp, plus a centered "WARP" banner so the player understands
// why their input isn't moving the ship.
inline void renderHUD(M5Canvas& g, const GameState& gs, float dist) {
  // Sun-heat overlay. Three escalating states tied to sunProximity():
  //   prox in [Warn, Damage)   — steady amber "HEAT" warning, no damage
  //   prox in [Damage, Fatal)  — blinking orange + double border, taking damage
  //   prox >= Fatal             — already triggered death; overlay sticks
  if (gs.hullHeat >= SunWarnFrac) {
    bool damaging = gs.hullHeat >= SunDamageFrac;
    bool show = damaging ? (((millis() / 120u) & 1u) == 0u) : true;
    if (show) {
      uint16_t col = damaging ? 0xFD20 /*orange*/ : 0xFCA0 /*amber*/;
      int vx0 = Config::ViewX;
      int vy0 = Config::ViewY;
      int vw  = Config::ViewW;
      int vh  = Config::ViewH;
      g.drawRect(vx0,     vy0,     vw,     vh,     col);
      if (damaging) g.drawRect(vx0 + 1, vy0 + 1, vw - 2, vh - 2, col);
      const char* tag = "HEAT";
      int tw = (int)strlen(tag) * 6;
      g.setTextSize(1);
      g.setTextColor(col, TFT_BLACK);
      g.setCursor(vx0 + (vw - tw) / 2, vy0 + 3);
      g.print(tag);
    }
  }

  if (state.targetIdx < 0 || state.targetIdx >= layout.numPOIs) return;
  const auto& p = layout.poi[state.targetIdx];
  char nm[20];
  SolarSystem::displayName(state.loadedSys, p, nm, sizeof(nm));

  g.setTextSize(1);
  g.setTextColor(state.warping ? TFT_CYAN : TFT_GREEN, TFT_BLACK);
  g.setCursor(3, 3);
  g.printf("> %s", nm);

  float k = dist / 1000.0f;
  char buf[20];
  if (k < 10.0f) snprintf(buf, sizeof(buf), "%.1fK", k);
  else           snprintf(buf, sizeof(buf), "%dK", (int)k);
  g.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  g.setCursor(3, 13);
  g.print(buf);

  // Top-right corner — ECM status only. Missile count now lives in the
  // HUD bar strip under SP, so it isn't repeated here.
  if (gs.ecm) {
    char arm[8];
    int  armLen;
    int  cd = (int)(gs.ecmCooldown + 0.99f);
    if (cd <= 0) armLen = snprintf(arm, sizeof(arm), "E");
    else         armLen = snprintf(arm, sizeof(arm), "%ds", cd);
    g.setTextColor(TFT_YELLOW, TFT_BLACK);
    g.setCursor(Config::ScreenW - armLen * 6 - 3, 3);
    g.print(arm);
  }

  // R21: lock readout below the missile line — name of the locked NPC.
  if (state.lockedNPC >= 0 && state.lockedNPC < NPCShip::MaxNPCs &&
      NPCShip::ships[state.lockedNPC].active) {
    const auto& sh = NPCShip::ships[state.lockedNPC];
    const char* tag =
        (sh.role == NPCShip::Role::Pirate) ? "LOCK PIRATE"
      : (sh.role == NPCShip::Role::Patrol) ? "LOCK PATROL"
                                           : "LOCK TRADER";
    int len = (int)strlen(tag);
    g.setTextColor(TFT_RED, TFT_BLACK);
    g.setCursor(Config::ScreenW - len * 6 - 3, 13);
    g.print(tag);
  }

  if (Missile::incomingToPlayer()) {
    // R21: an enemy missile is homing — overrides HOSTILE so the player
    // realizes ECM is the right answer, not just evasion.
    const char* tag = "INCOMING!";
    int w = 9 * 6;
    int x = Config::ViewX + (Config::ViewW - w) / 2;
    g.setTextColor(TFT_RED, TFT_BLACK);
    g.setCursor(x, Config::ViewY + 3);
    g.print(tag);
  } else if (NPCShip::anyPirateAttacking()) {
    // R19: pirate has us in cone + FireRange — top-of-stack red flash.
    const char* tag = "*HOSTILE*";
    int w = 9 * 6;
    int x = Config::ViewX + (Config::ViewW - w) / 2;
    g.setTextColor(TFT_RED, TFT_BLACK);
    g.setCursor(x, Config::ViewY + 3);
    g.print(tag);
  } else if (state.warping) {
    const char* tag = "WARP";
    int w = 4 * 6;
    int x = Config::ViewX + (Config::ViewW - w) / 2;
    g.setTextColor(TFT_CYAN, TFT_BLACK);
    g.setCursor(x, Config::ViewY + 3);
    g.print(tag);
  } else if (nearGate()) {
    // Flying through the gate auto-opens the galaxy chart — that's
    // the only way out of the system now.
    const char* tag = "ENTER GATE";
    int w = 10 * 6;
    int x = Config::ViewX + (Config::ViewW - w) / 2;
    g.setTextColor(TFT_MAGENTA, TFT_BLACK);
    g.setCursor(x, Config::ViewY + 3);
    g.print(tag);
  } else if (int hi = NPCShip::hailIdx(state.px, state.py, state.pz); hi >= 0) {
    // A hailable NPC is in range. Traders → trade; anything else has
    // been shield-cracked and offers free loot (Parkan-style).
    bool loot = NPCShip::ships[hi].role != NPCShip::Role::Trader;
    const char* tag = loot ? "H=LOOT" : "H=HAIL";
    int w = 6 * 6;
    int x = Config::ViewX + (Config::ViewW - w) / 2;
    g.setTextColor(loot ? TFT_RED : TFT_YELLOW, TFT_BLACK);
    g.setCursor(x, Config::ViewY + 3);
    g.print(tag);
  } else if (inBelt()) {
    // R14: tell the player they're inside an asteroid belt.
    const char* tag = "BELT";
    int w = 4 * 6;
    int x = Config::ViewX + (Config::ViewW - w) / 2;
    g.setTextColor(TFT_ORANGE, TFT_BLACK);
    g.setCursor(x, Config::ViewY + 3);
    g.print(tag);
  }

  // R24: promotion banner — drawn one row below the precedence stack so
  // it doesn't fight HOSTILE/WARP/etc. Fades over the last 0.6 s.
  if (Rank::toast > 0.0f) {
    char line[40];
    snprintf(line, sizeof(line), "PROMOTED: %s",
             Rank::nameFor((Rank::Id)Rank::toastRank));
    int len = (int)strlen(line);
    int w = len * 6;
    int x = Config::ViewX + (Config::ViewW - w) / 2;
    uint16_t col = Rank::colorFor((Rank::Id)Rank::toastRank);
    if (Rank::toast < 0.6f) {
      // Flicker the last fade by skipping odd frames.
      if (((int)(Rank::toast * 12.0f) & 1) == 0) {
        g.setTextColor(col, TFT_BLACK);
        g.setCursor(x, Config::ViewY + 14);
        g.print(line);
      }
    } else {
      g.setTextColor(col, TFT_BLACK);
      g.setCursor(x, Config::ViewY + 14);
      g.print(line);
    }
  }
}

// Return the POI index of the gate if the player is inside
// GateApproachRange of it, or -1. The outer loop pairs this with the
// chart-set target to decide whether J fires hyperspace or set-course warp.
inline int gateInRange() {
  for (int i = 0; i < layout.numPOIs; i++) {
    const auto& p = layout.poi[i];
    if (p.type != SolarSystem::POIType::JumpGate) continue;
    float dx = (float)p.x - state.px;
    float dy = (float)p.y - state.py;
    float dz = (float)p.z - state.pz;
    if (dx * dx + dy * dy + dz * dz <
        GateApproachRange * GateApproachRange) return i;
  }
  return -1;
}

inline bool nearGate() { return gateInRange() >= 0; }

// Called when the player cancels the galactic chart that auto-opened on
// gate contact. Pushes the ship a clear distance inward from the gate
// (toward the system origin) and faces it that way, so the next frame
// doesn't re-trigger gateContact() and re-open the chart.
inline void bumpOffGate() {
  int gateI = -1;
  for (int i = 0; i < layout.numPOIs; i++) {
    if (layout.poi[i].type == SolarSystem::POIType::JumpGate) {
      gateI = i;
      break;
    }
  }
  if (gateI < 0) return;
  const auto& g = layout.poi[gateI];
  float gx = (float)g.x, gy = (float)g.y, gz = (float)g.z;
  float glen = sqrtf(gx*gx + gz*gz);
  // Inward unit vector (toward the system origin) in XZ; fallback to -X
  // if the gate happens to sit on the axis.
  float ix = (glen > 1.0f) ? -gx / glen : -1.0f;
  float iz = (glen > 1.0f) ? -gz / glen :  0.0f;
  const float standoff = 1800.0f;   // well past GateContactRange (350) and
                                    // GateApproachRange (1500) so the
                                    // "ENTER GATE" prompt also clears
  state.px = gx + ix * standoff;
  state.py = gy;
  state.pz = gz + iz * standoff;
  // Face inward — gate sits behind the cockpit so a tap of accel pulls
  // the player into the system, not back into the gate.
  state.fx = ix; state.fy = 0.0f; state.fz = iz;
  state.ux = 0.0f; state.uy = 1.0f; state.uz = 0.0f;
  renormalize();
}

// True the moment the player physically touches the gate ring. The
// outer loop uses this to auto-open the chart — flying through a gate
// is the only way to leave a system.
inline bool gateContact() {
  for (int i = 0; i < layout.numPOIs; i++) {
    const auto& p = layout.poi[i];
    if (p.type != SolarSystem::POIType::JumpGate) continue;
    float dx = (float)p.x - state.px;
    float dy = (float)p.y - state.py;
    float dz = (float)p.z - state.pz;
    if (dx * dx + dy * dy + dz * dz <
        GateContactRange * GateContactRange) return true;
  }
  return false;
}

// Return the POI index of the nearest planet whose rendered body the
// cockpit is touching (distance ≤ visualR + LandingSkin), or -1. The
// motion integrator does the actual collision; this helper is kept so
// HUD code and post-update auto-land hand-off can query the same state.
inline int planetInLandingRange() {
  int best = -1;
  float bestD2 = 1e18f;
  for (int i = 0; i < layout.numPOIs; i++) {
    const auto& p = layout.poi[i];
    if (p.type != SolarSystem::POIType::Planet) continue;
    float dx = (float)p.x - state.px;
    float dy = (float)p.y - state.py;
    float dz = (float)p.z - state.pz;
    float d2 = dx*dx + dy*dy + dz*dz;
    float visualR = (float)p.radius * PlanetVisualScale;
    float touch   = visualR + LandingSkin;
    if (d2 < touch * touch && d2 < bestD2) {
      bestD2 = d2;
      best   = i;
    }
  }
  return best;
}

// Used by LandingScreen → LAUNCH to put the player back in the active
// system just outside the given POI, facing away from it. `standoff` is
// in sysu (planet radius + a small clearance).
inline void enterNearPOI(int sysIdx, int poiIdx, float standoff) {
  bool switched = (state.loadedSys != sysIdx);
  if (switched) {
    SolarSystem::layoutFor(sysIdx, layout);
    state.loadedSys = sysIdx;
  }
  activeBelt = -1;
  if (switched) {
    NPCShip::spawnFor(sysIdx, layout);
    Combat::resetFlashes();
    Missile::resetAll();
    state.lockedNPC = -1;
  }
  if (poiIdx < 0 || poiIdx >= layout.numPOIs) {
    enter(sysIdx, SpawnAt::AtGate);
    return;
  }
  const auto& p = layout.poi[poiIdx];
  // Radial direction away from the system origin so we don't drop the
  // player on top of the star side of the planet.
  float ox = (float)p.x, oz = (float)p.z;
  float len = sqrtf(ox*ox + oz*oz);
  float ux = (len > 0.5f) ? ox / len : 1.0f;
  float uz = (len > 0.5f) ? oz / len : 0.0f;
  state.px = (float)p.x + ux * standoff;
  state.py = (float)p.y;
  state.pz = (float)p.z + uz * standoff;
  // Face away from the planet — i.e. along the outward radial direction
  // so the body we just launched from sits behind the cockpit.
  float dx = state.px - (float)p.x;
  float dy = state.py - (float)p.y;
  float dz = state.pz - (float)p.z;
  float dlen = sqrtf(dx*dx + dy*dy + dz*dz);
  if (dlen < 1.0f) { state.fx = ux; state.fy = 0.0f; state.fz = uz; }
  else             { state.fx = dx/dlen; state.fy = dy/dlen; state.fz = dz/dlen; }
  state.ux = 0.0f; state.uy = 1.0f; state.uz = 0.0f;
  renormalize();
  state.targetIdx = poiIdx;
  state.warping = false;
  state.initialized = true;
}

// ---------- Asteroid belts (R14) ----------

// Player inside belt-flagged planet `poiIdx`'s toroidal volume?
inline bool isInBeltOf(int poiIdx) {
  if (poiIdx < 0 || poiIdx >= layout.numPOIs) return false;
  const auto& p = layout.poi[poiIdx];
  if (p.type != SolarSystem::POIType::Planet) return false;
  if ((p.flags & SolarSystem::PoiFlagBelt) == 0) return false;
  float dx = state.px - (float)p.x;
  float dy = state.py - (float)p.y;
  float dz = state.pz - (float)p.z;
  if (fabsf(dy) > BeltHalfHeight) return false;
  float r2 = dx * dx + dz * dz;
  float innerR = (float)p.radius + BeltInnerExtra;
  float outerR = innerR + BeltWidth;
  return r2 >= innerR * innerR && r2 <= outerR * outerR;
}

// Deterministically populate `rocks[]` around the given planet. Seed
// mixes the system seed, the planet index, and a salt so two systems'
// belts can't accidentally share the same rock layout.
inline void spawnRocksAround(int poiIdx) {
  const auto& p = layout.poi[poiIdx];
  uint32_t s = Galaxy::systemSubSeed(state.loadedSys, 0xB00Bu)
             + (uint32_t)poiIdx * 0xABCD1234u;
  float innerR = (float)p.radius + BeltInnerExtra;
  float outerR = innerR + BeltWidth;
  for (int i = 0; i < NumRocks; i++) {
    float baseAng = (float)i * (6.2831853f / (float)NumRocks);
    float jitter  = ((float)(lcg(s) & 0xFFFFu) / 65536.0f) * 0.45f;
    float ang     = baseAng + jitter;
    float t       = (float)(lcg(s) & 0xFFFFu) / 65536.0f;
    float radius  = innerR + t * (outerR - innerR);
    float yJit    = (((float)(lcg(s) & 0xFFFFu) / 65536.0f) * 2.0f - 1.0f)
                  * BeltHalfHeight;
    rocks[i].rx = (int16_t)(cosf(ang) * radius);
    rocks[i].rz = (int16_t)(sinf(ang) * radius);
    rocks[i].ry = (int16_t)yJit;
  }
}

// Detect the active belt this frame; (re)spawn rocks if it changed.
inline void updateBelt() {
  int found = -1;
  for (int i = 0; i < layout.numPOIs; i++) {
    if (isInBeltOf(i)) { found = i; break; }
  }
  if (found != activeBelt) {
    activeBelt = found;
    if (activeBelt >= 0) spawnRocksAround(activeBelt);
  }
}

inline bool inBelt() { return activeBelt >= 0; }

// Draw the cached rocks. Cheap — N is small and most miss the viewport.
inline void renderBelt(M5Canvas& g) {
  if (activeBelt < 0) return;
  const auto& p = layout.poi[activeBelt];
  float pcx = (float)p.x, pcy = (float)p.y, pcz = (float)p.z;
  for (int i = 0; i < NumRocks; i++) {
    float dpx = pcx + (float)rocks[i].rx - state.px;
    float dpy = pcy + (float)rocks[i].ry - state.py;
    float dpz = pcz + (float)rocks[i].rz - state.pz;
    float cx, cy, cz;
    if (!toCamera(dpx, dpy, dpz, cx, cy, cz)) continue;
    const int vx = Config::ViewX + Config::ViewW / 2;
    const int vy = Config::ViewY + Config::ViewH / 2;
    int sx = vx + (int)(cx * FocalLen / cz);
    int sy = vy - (int)(cy * FocalLen / cz);
    if (sx < Config::ViewX || sx >= Config::ViewX + Config::ViewW) continue;
    if (sy < Config::ViewY || sy >= Config::ViewY + Config::ViewH) continue;
    int r = (int)(140.0f / cz);
    if (r < 1) r = 1;
    if (r > 3) r = 3;
    uint16_t col = (cz < 1500.0f) ? TFT_LIGHTGREY : 0x6B4D; // dim grey-brown
    if (r == 1) g.drawPixel(sx, sy, col);
    else        g.fillCircle(sx, sy, r, col);
  }
}

// R16: render NPC traders by projecting their world position through the
// shared camera transform, then handing off to Ship3D::render with a
// model-space yaw / pitch derived from the ship's world heading.
inline void renderNPCShips(M5Canvas& g) {
  if (NPCShip::loadedSys != state.loadedSys) return;
  // Camera basis (right = up × forward) — reused per ship to transform
  // each NPC's world axes into camera space.
  float rxw = state.uy * state.fz - state.uz * state.fy;
  float ryw = state.uz * state.fx - state.ux * state.fz;
  float rzw = state.ux * state.fy - state.uy * state.fx;

  // A ship is hidden when the camera→ship segment passes through a
  // star/planet body — without this, silhouettes draw on top of the
  // globe they're behind.
  auto hiddenByBody = [&](float dpx, float dpy, float dpz,
                          float shipDist) -> bool {
    for (int b = 0; b < layout.numPOIs; b++) {
      const auto& p = layout.poi[b];
      float scale = (p.type == SolarSystem::POIType::Star)   ? StarVisualScale
                  : (p.type == SolarSystem::POIType::Planet) ? PlanetVisualScale
                  : 0.0f;
      if (scale <= 0.0f) continue;
      float ox = (float)p.x - state.px;
      float oy = (float)p.y - state.py;
      float oz = (float)p.z - state.pz;
      float t = (ox * dpx + oy * dpy + oz * dpz) / shipDist;
      if (t < 0.0f || t > shipDist) continue;   // body not between us
      float closest2 = ox*ox + oy*oy + oz*oz - t * t;
      float R = (float)p.radius * scale;
      if (closest2 < R * R) return true;
    }
    return false;
  };

  for (int i = 0; i < NPCShip::MaxNPCs; i++) {
    const auto& sh = NPCShip::ships[i];
    if (!sh.active) continue;
    float dpx = sh.wx - state.px;
    float dpy = sh.wy - state.py;
    float dpz = sh.wz - state.pz;
    float cxx, cyy, czz;
    if (!toCamera(dpx, dpy, dpz, cxx, cyy, czz)) continue;
    if (czz < 60.0f) continue;          // tighter than NearZ — avoid huge sprites
    float shipDist = sqrtf(dpx*dpx + dpy*dpy + dpz*dpz);
    if (shipDist > 1.0f && hiddenByBody(dpx, dpy, dpz, shipDist)) continue;

    // NPC's world-space forward picks up pitch + yaw so dives and climbs
    // are visible; world-space up is fixed (0,1,0). Both go through the
    // camera basis to land in camera space.
    float Cp = cosf(sh.pitch), Sp = sinf(sh.pitch);
    float Sy = sinf(sh.yaw),   Cy = cosf(sh.yaw);
    float wfx = Sy * Cp;
    float wfy = Sp;
    float wfz = Cy * Cp;
    float cfx = wfx * rxw      + wfy * ryw       + wfz * rzw;
    float cfy = wfx * state.ux + wfy * state.uy  + wfz * state.uz;
    float cfz = wfx * state.fx + wfy * state.fy  + wfz * state.fz;
    float cux = ryw;
    float cuy = state.uy;
    float cuz = state.fy;
    Ship3D::renderBasis(g, Ship3D::byId(sh.modelId),
                        cxx, cyy, czz,
                        cfx, cfy, cfz,
                        cux, cuy, cuz,
                        Ship3D::scaleFor(sh.modelId), sh.color);
  }
}

// Hit-spark particles. World-space points projected through the same
// camera as ships; size shrinks and color fades as the particle ages so
// the burst reads as a quick puff rather than a static splatter.
inline void renderParticles(M5Canvas& g) {
  const int vxc = Config::ViewX + Config::ViewW / 2;
  const int vyc = Config::ViewY + Config::ViewH / 2;
  const int x0 = Config::ViewX + 1;
  const int y0 = Config::ViewY + 1;
  const int x1 = Config::ViewX + Config::ViewW - 2;
  const int y1 = Config::ViewY + Config::ViewH - 2;
  for (int i = 0; i < Particles::MaxParticles; i++) {
    const auto& p = Particles::pool[i];
    if (!p.alive) continue;
    float cx, cy, cz;
    if (!toCamera(p.wx - state.px, p.wy - state.py, p.wz - state.pz,
                  cx, cy, cz)) continue;
    int sx = vxc + (int)(cx * FocalLen / cz);
    int sy = vyc - (int)(cy * FocalLen / cz);
    if (sx < x0 || sx > x1 || sy < y0 || sy > y1) continue;
    float frac = (p.life0 > 0.0f) ? (p.life / p.life0) : 0.0f;
    // Young particle: bright yellow flash. Then weapon-tier color. Then
    // dim red ember just before it dies.
    uint16_t col = (frac > 0.55f) ? 0xFFE0
                 : (frac > 0.25f) ? p.color
                 :                  0xC800;
    // Radius scales with both age (younger = bigger) and distance
    // (closer = bigger). With cz up to a few thousand sysu, a 600/cz
    // term keeps nearby sparks chunky and distant ones at least 2px.
    int rBase = (frac > 0.55f) ? 4
              : (frac > 0.25f) ? 3
              :                  2;
    int rDist = (int)(600.0f / cz);
    int r = rBase + rDist;
    if (r < 2) r = 2;
    if (r > 6) r = 6;
    g.fillCircle(sx, sy, r, col);
    // Bright core on the youngest sparks so the flash punches through.
    if (frac > 0.55f && r > 2) {
      g.fillCircle(sx, sy, r - 2, 0xFFFF);
    }
  }
}

// Laser-bolt visuals. Each shot is animated as a fast bullet: a short
// bright segment travels from the gun toward the target across the
// flash lifetime, instead of holding a static line.
inline void renderLasers(M5Canvas& g) {
  const int reticleX = Config::ViewX + Config::ViewW / 2;
  const int reticleY = Config::ViewY + Config::ViewH / 2;

  // Helper — paint a moving bullet on the line from (sx,sy) to (ex,ey).
  // `prog` is 0..1 (0 = at start, 1 = at target). The trailing segment
  // is a short length behind the head, brightening as it ages.
  auto drawBullet = [&](int sx, int sy, int ex, int ey,
                        float prog, uint16_t col) {
    if (prog < 0.0f) prog = 0.0f;
    if (prog > 1.0f) prog = 1.0f;
    constexpr float TailLen = 0.22f;
    float tprog = prog - TailLen;
    if (tprog < 0.0f) tprog = 0.0f;
    int hx = sx + (int)((ex - sx) * prog);
    int hy = sy + (int)((ey - sy) * prog);
    int tx = sx + (int)((ex - sx) * tprog);
    int ty = sy + (int)((ey - sy) * tprog);
    g.drawLine(tx, ty, hx, hy, col);
    // Bullet head: bright white core for a sense of speed.
    g.fillCircle(hx, hy, 1, 0xFFFF);
    g.drawPixel(hx, hy, 0xFFFF);
  };

  // --- Player ---
  if (Combat::playerFlash.t > 0.0f) {
    int tx = reticleX;
    int ty = reticleY;
    if (Combat::playerFlash.shipIdx >= 0) {
      const auto& sh = NPCShip::ships[Combat::playerFlash.shipIdx];
      float dpx = sh.wx - state.px;
      float dpy = sh.wy - state.py;
      float dpz = sh.wz - state.pz;
      float cxx, cyy, czz;
      if (toCamera(dpx, dpy, dpz, cxx, cyy, czz)) {
        const int vxc = Config::ViewX + Config::ViewW / 2;
        const int vyc = Config::ViewY + Config::ViewH / 2;
        tx = vxc + (int)(cxx * FocalLen / czz);
        ty = vyc - (int)(cyy * FocalLen / czz);
      }
    }
    uint16_t col = Combat::playerFlash.color;
    int leftX  = Config::ViewX + 8;
    int rightX = Config::ViewX + Config::ViewW - 8;
    int gunY   = Config::ViewY + Config::ViewH - 3;
    float prog = 1.0f - (Combat::playerFlash.t / Combat::FlashLifetime);
    drawBullet(leftX,  gunY, tx, ty, prog, col);
    drawBullet(rightX, gunY, tx, ty, prog, col);
  }

  // --- NPCs ---
  for (int i = 0; i < NPCShip::MaxNPCs; i++) {
    if (Combat::npcFlash[i].t <= 0.0f) continue;
    const auto& sh = NPCShip::ships[i];
    if (!sh.active) continue;
    float dpx = sh.wx - state.px;
    float dpy = sh.wy - state.py;
    float dpz = sh.wz - state.pz;
    float cxx, cyy, czz;
    if (!toCamera(dpx, dpy, dpz, cxx, cyy, czz)) continue;
    const int vxc = Config::ViewX + Config::ViewW / 2;
    const int vyc = Config::ViewY + Config::ViewH / 2;
    int sx = vxc + (int)(cxx * FocalLen / czz);
    int sy = vyc - (int)(cyy * FocalLen / czz);
    float prog = 1.0f - (Combat::npcFlash[i].t / Combat::FlashLifetime);
    drawBullet(sx, sy, reticleX, reticleY, prog, Combat::npcFlash[i].color);
  }
}

// R21: project missiles as short streaks — a head dot plus a tail
// segment pointing back along the velocity vector. Player missiles render
// yellow, NPC missiles red (set at spawn time).
inline void renderMissiles(M5Canvas& g) {
  for (int i = 0; i < Missile::MaxMissiles; i++) {
    const auto& m = Missile::pool[i];
    if (!m.active) continue;

    float dpx = m.wx - state.px;
    float dpy = m.wy - state.py;
    float dpz = m.wz - state.pz;
    float cxx, cyy, czz;
    if (!toCamera(dpx, dpy, dpz, cxx, cyy, czz)) continue;
    const int vxc = Config::ViewX + Config::ViewW / 2;
    const int vyc = Config::ViewY + Config::ViewH / 2;
    int hx = vxc + (int)(cxx * FocalLen / czz);
    int hy = vyc - (int)(cyy * FocalLen / czz);
    if (hx < Config::ViewX - 8 || hx > Config::ViewX + Config::ViewW + 8) continue;
    if (hy < Config::ViewY - 8 || hy > Config::ViewY + Config::ViewH + 8) continue;

    // Tail = head minus a tiny step back along velocity in world space.
    float tx = m.wx - m.vx * 0.05f - state.px;
    float ty = m.wy - m.vy * 0.05f - state.py;
    float tz = m.wz - m.vz * 0.05f - state.pz;
    float tcxx, tcyy, tczz;
    if (toCamera(tx, ty, tz, tcxx, tcyy, tczz)) {
      int tlx = vxc + (int)(tcxx * FocalLen / tczz);
      int tly = vyc - (int)(tcyy * FocalLen / tczz);
      g.drawLine(hx, hy, tlx, tly, m.color);
    }
    // Hot pixel at the head so the missile stays visible even at range.
    g.drawPixel(hx, hy, TFT_WHITE);
  }
}

// R21: draw a small bracket around the locked NPC so the player can
// confirm the lock without consulting the radar.
// Four L-shaped corner brackets around (sx, sy). `r` is the half-size of
// the bracket in pixels.
inline void drawBracket(M5Canvas& g, int sx, int sy, int r, uint16_t col) {
  int arm = r / 2;
  g.drawLine(sx - r, sy - r, sx - r + arm, sy - r,       col);
  g.drawLine(sx - r, sy - r, sx - r,       sy - r + arm, col);
  g.drawLine(sx + r, sy - r, sx + r - arm, sy - r,       col);
  g.drawLine(sx + r, sy - r, sx + r,       sy - r + arm, col);
  g.drawLine(sx - r, sy + r, sx - r + arm, sy + r,       col);
  g.drawLine(sx - r, sy + r, sx - r,       sy + r - arm, col);
  g.drawLine(sx + r, sy + r, sx + r - arm, sy + r,       col);
  g.drawLine(sx + r, sy + r, sx + r,       sy + r - arm, col);
}

// Draw a marker for a world-space point. On-screen → bracket square that
// shrinks with distance; off-screen or behind the camera → arrowhead
// pasted on the viewport edge pointing toward the world position.
inline void drawWorldMarker(M5Canvas& g,
                            float wx, float wy, float wz,
                            uint16_t color) {
  float dpx = wx - state.px;
  float dpy = wy - state.py;
  float dpz = wz - state.pz;
  // Inline camera transform so we can still produce a screen direction
  // when the target is behind the cockpit.
  float rx = state.uy * state.fz - state.uz * state.fy;
  float ry = state.uz * state.fx - state.ux * state.fz;
  float rz = state.ux * state.fy - state.uy * state.fx;
  float cxx = dpx * rx       + dpy * ry       + dpz * rz;
  float cyy = dpx * state.ux + dpy * state.uy + dpz * state.uz;
  float czz = dpx * state.fx + dpy * state.fy + dpz * state.fz;

  const int vxc = Config::ViewX + Config::ViewW / 2;
  const int vyc = Config::ViewY + Config::ViewH / 2;
  const int xL  = Config::ViewX + 6;
  const int xR  = Config::ViewX + Config::ViewW  - 7;
  const int yT  = Config::ViewY + 6;
  const int yB  = Config::ViewY + Config::ViewH  - 7;

  if (czz > NearZ) {
    int sx = vxc + (int)(cxx * FocalLen / czz);
    int sy = vyc - (int)(cyy * FocalLen / czz);
    bool inView = (sx >= xL && sx <= xR && sy >= yT && sy <= yB);
    if (inView) {
      int r = (int)(900.0f * FocalLen / czz);
      if (r < 7)  r = 7;
      if (r > 20) r = 20;
      drawBracket(g, sx, sy, r, color);
      return;
    }
    // In front but off-screen — fall through to edge arrow.
  }

  // Off-screen or behind — direction from viewport center toward target.
  // When the target is behind, flip the camera-space x,y signs so the
  // arrow points to the rear of the screen.
  float dx = cxx, dy = cyy;
  if (czz <= NearZ) { dx = -dx; dy = -dy; }
  // Screen-space direction (sy axis flipped vs cy in camera).
  float sxd = dx;
  float syd = -dy;
  float len = sqrtf(sxd*sxd + syd*syd);
  if (len < 1e-3f) return;
  sxd /= len; syd /= len;

  // Find which viewport edge the ray hits first.
  float t = 1e9f;
  if (sxd > 0.0f) { float tt = (float)(xR - vxc) / sxd; if (tt < t) t = tt; }
  if (sxd < 0.0f) { float tt = (float)(xL - vxc) / sxd; if (tt < t) t = tt; }
  if (syd > 0.0f) { float tt = (float)(yB - vyc) / syd; if (tt < t) t = tt; }
  if (syd < 0.0f) { float tt = (float)(yT - vyc) / syd; if (tt < t) t = tt; }
  if (t <= 0.0f || t > 1e8f) return;

  int ex = vxc + (int)(sxd * t);
  int ey = vyc + (int)(syd * t);
  // Arrowhead triangle. Tip at (ex, ey), base 6 px back along -direction.
  float bxc = ex - sxd * 6.0f;
  float byc = ey - syd * 6.0f;
  float perpx = -syd, perpy = sxd;
  int b1x = (int)(bxc + perpx * 4.0f);
  int b1y = (int)(byc + perpy * 4.0f);
  int b2x = (int)(bxc - perpx * 4.0f);
  int b2y = (int)(byc - perpy * 4.0f);
  g.fillTriangle(ex, ey, b1x, b1y, b2x, b2y, color);
}

// Bracket / arrow for the locked NPC (red).
inline void renderLockBracket(M5Canvas& g) {
  if (state.lockedNPC < 0 || state.lockedNPC >= NPCShip::MaxNPCs) return;
  const auto& sh = NPCShip::ships[state.lockedNPC];
  if (!sh.active) return;
  drawWorldMarker(g, sh.wx, sh.wy, sh.wz, TFT_RED);

  // HP readout: shield + hull bars floating just above the locked ship.
  // Projected through the same camera so they track the target on screen.
  float cxx, cyy, czz;
  if (!toCamera(sh.wx - state.px, sh.wy - state.py, sh.wz - state.pz,
                cxx, cyy, czz)) return;
  const int vxc = Config::ViewX + Config::ViewW / 2;
  const int vyc = Config::ViewY + Config::ViewH / 2;
  int sx = vxc + (int)(cxx * FocalLen / czz);
  int sy = vyc - (int)(cyy * FocalLen / czz);
  const int barW = 28, barH = 2;
  int bx = sx - barW / 2;
  int by = sy - 16;                          // above the bracket
  const int x0 = Config::ViewX + 1;
  const int y0 = Config::ViewY + 1;
  const int x1 = Config::ViewX + Config::ViewW - 2;
  const int y1 = Config::ViewY + Config::ViewH - 2;
  if (bx < x0) bx = x0;
  if (bx + barW > x1) bx = x1 - barW;
  if (by < y0) by = y0;
  if (by + barH * 2 + 1 > y1) by = y1 - barH * 2 - 1;

  auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
  float sFill = clamp01(sh.shields);
  float hFill = clamp01(sh.hull);
  // Shield bar (cyan on dark grey background).
  g.fillRect(bx, by, barW, barH, 0x2104);
  g.fillRect(bx, by, (int)(barW * sFill + 0.5f), barH, 0x07FF);
  // Hull bar (green→yellow→red depending on remaining %).
  uint16_t hullCol = (hFill > 0.66f) ? 0x07E0   // green
                   : (hFill > 0.33f) ? 0xFFE0   // yellow
                   :                   0xF800;  // red
  g.fillRect(bx, by + barH + 1, barW, barH, 0x2104);
  g.fillRect(bx, by + barH + 1, (int)(barW * hFill + 0.5f), barH, hullCol);
}

// Bracket / arrow for the current target POI (cyan). Skipped while
// warping — the WARP banner is feedback enough.
inline void renderTargetBracket(M5Canvas& g) {
  if (state.warping) return;
  if (state.targetIdx < 0 || state.targetIdx >= layout.numPOIs) return;
  const auto& p = layout.poi[state.targetIdx];
  if (p.type == SolarSystem::POIType::Star) return;   // pointing at the sun is daft
  drawWorldMarker(g, (float)p.x, (float)p.y, (float)p.z, TFT_CYAN);
}

} // namespace SystemFlight
