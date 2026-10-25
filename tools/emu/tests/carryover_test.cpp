// What one creature must NOT leave behind for the next, and what a companion
// must not do while it is frozen.
//
// Every case here is per-creature state that lived in RAM next to the player's
// and was simply never reset, so it leaked across a hatch, a reboot or a revive:
//
//   * evoDeclinedLv -- "keep form" on an Ultimate at level 50 meant the NEXT
//     creature's Baby I was not offered its digivolution until level 51. It sat
//     as a BOTAMON for two days with nothing on screen to say why.
//   * farDeclinedAge -- "stay together" re-offers the farewell a day later, but
//     that age was never cleared, so the next creature's farewell slipped too.
//   * a frozen companion is documented as not ageing, and tick() honoured that
//     while syncClock() did not: a night with the board off levelled it up.
//   * the same companion's ageMinutes never moves, and every cadence in tick()
//     was `ageMinutes % n`. Stuck on a multiple of 60, all of them fired EVERY
//     minute -- weight, the sleeping drain, and the level-up jingle.
//   * retirePending is persisted but the ceremony it belongs to is not, so a
//     power cut during an early retire left a living creature flagged as
//     retired: its eventual real farewell then banked nothing and charged the
//     next egg a day.
//   * opponents are built as scratch Pets, and hatch() played the hatch jingle
//     for every one of them -- at the start of every fight and again for each
//     replacement a boss sent out.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "audio.h"
#include <cstdio>
#ifndef TAMA_NATIVE_TEST
uint32_t g_seed=57; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
static int gSfx[SFX_COUNT] = { 0 };
void sfxPlay(uint8_t id){ if (id < SFX_COUNT) gSfx[id]++; }
#endif
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static void cared(Pet &p){ p.fullness = p.joy = p.energy = p.hygiene = 100; p.sleeping = false; }
static void atLevel(Pet &p, uint8_t lvl){ p.ageMinutes = (uint32_t)(lvl - 1) * MINUTES_PER_LEVEL; }

int main(){
  // --- a declined digivolution belongs to the creature that declined it
  {
    Pet p; p.begin();
    p.dbgHatchAs(5, false);                 // GREYMON, a Champion
    cared(p);
    atLevel(p, 50);
    p.wins = 5;                             // enough for its Ultimate branch
    ck(p.wantEvolveButton(), "a Champion with its wins is offered a digivolution");
    p.declineEvolve();
    ck(!p.wantEvolveButton(), "and 'keep form' hides it until the next level");

    p.newEgg();
    p.dbgHatchAs(1, false);                 // BOTAMON, Baby I
    cared(p);
    atLevel(p, 2);                          // Baby II's entry level
    ck(p.canEvolveNow(), "the next creature's Baby I is ready at level 2");
    ck(p.wantEvolveButton(),
       "and is OFFERED it -- the previous creature's 'keep form' at 50 does not follow it");
  }

  // --- a declined farewell belongs to the creature that declined it
  {
    Pet p; p.begin();
    p.dbgHatchAs(5, false);
    cared(p);
    p.ageMinutes = FAREWELL_AGE_MIN + 600;
    ck(p.wantFarewellButton(), "a Champion past three days is offered its farewell");
    p.declineFarewell();
    ck(!p.wantFarewellButton(), "'stay together' puts it off by a day");

    p.newEgg();
    p.dbgHatchAs(5, false);
    cared(p);
    p.ageMinutes = FAREWELL_AGE_MIN;
    ck(p.wantFarewellButton(),
       "the NEXT creature is offered its farewell at three days, not at the old one's deadline");
  }

  // --- a frozen companion does not age while the board is off
  {
    Pet p; p.begin();
    PartyMon m; m.dex = 5; m.level = 30;
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
    p.reviveFrom(m);
    ck(p.frozen && p.level() == 30, "a revived companion comes back frozen at its banked level");
    const uint32_t t0 = CLOCK_EPOCH_FLOOR + 10UL * 86400 + 12UL * 3600;
    p.dbgSetSeen(t0);
    p.syncClock(t0 + 10UL * 3600);          // ten hours off
    ck(p.level() == 30, "ten hours offline do not level a frozen companion");
    ck(p.ageMinutes == (uint32_t)29 * MINUTES_PER_LEVEL, "its age is exactly what it was banked with");
  }

  // --- ...and its cadences run at their real rate while it is on
  {
    Pet p; p.begin();
    PartyMon m; m.dex = 5; m.level = 30;
    p.reviveFrom(m);
    cared(p);
    p.weight = 60;
#ifndef TAMA_NATIVE_TEST
    const int levelUps = gSfx[SFX_LEVEL];
#endif
    for (int i = 0; i < 30; i++) { p.hygiene = 100; p.poops = 0; p.dbgTick(); }
    ck(p.weight >= 48 && p.weight <= 52,
       "awake, a frozen companion sheds weight every third minute, not every minute");
#ifndef TAMA_NATIVE_TEST
    ck(gSfx[SFX_LEVEL] == levelUps, "and a level it never gains plays no level-up jingle");
#endif
    p.sleeping = true; p.sleepAuto = SLEEP_PLAYER;
    p.fullness = 100;
    for (int i = 0; i < 20; i++) p.dbgTick();
    ck(p.fullness >= 89 && p.fullness <= 91,
       "asleep, its food drains one every other minute, the same as any creature");
  }

  // --- a retire that never finished is not a retire
  {
    Pet p; p.begin();
    p.newEgg();                             // the store still holds the companion above
    p.dbgHatchAs(3, false);                 // AGUMON, nowhere near three days
    cared(p);
    atLevel(p, 21);
    p.saveNow();
    p.startRetire();
    ck(p.retireIsEarly(), "an early retire marks itself as one");
    // Power cut mid-ceremony: the ceremony is RAM only, the flag is not.
    Pet q; q.begin();
    ck(q.speciesId == 3 && q.ceremony == CER_NONE, "after the cut the creature is still alive");
    ck(!q.retireIsEarly(), "and is no longer flagged as retired");
    q.newEgg();
    ck(q.evoPenalty() == 0, "so the next egg owes nothing for a retire that never happened");
  }

#ifndef TAMA_NATIVE_TEST
  // --- opponents are scratch pets and must be silent
  {
    for (int &n : gSfx) n = 0;
    Pet live; live.begin();
    live.dbgHatchAs(1, false);
    ck(gSfx[SFX_HATCH] == 1, "the live pet hatching plays the jingle (the counter works)");
    Pet foe;                                // never begin()-ed: an opponent
    foe.dbgHatchAs(17, false);              // a Mega, which also earns MED_FINAL
    ck(gSfx[SFX_HATCH] == 1, "building an opponent plays no hatch jingle");
    ck(gSfx[SFX_MEDAL] == 0, "nor a medal fanfare for its final form");
  }
#endif

  printf(bad ? "FAILURES\n" : "ALL PASS\n");
  return bad ? 1 : 0;
}
