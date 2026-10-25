// The LAN battle as the sketch actually wires it: what squad you fight with,
// what the guest is allowed to do, and whether a result off the wire lands on
// the right creature. The protocol itself is link_test's job; this is the half
// that lives in TamaDigi.ino and that link_test cannot reach.
//
// The radio is still untested -- it needs two boards. Everything on either side
// of it is checked here.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "link.h"
#include <cstdio>
uint32_t g_seed=5; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}

void setup(); void render(); void battleTap(int16_t,int16_t);
extern Pet pet;
extern Link lan;
extern bool battleOpen, btlLink, btlLinkHost, btlOver, btlWon, lanOpen;
extern Combatant btlYou, btlFoe;
extern Combatant btlSquad[];
extern uint8_t btlSquadN, btlSquadAt, btlFoeAt, btlMenu, btlMsgCount;
extern uint8_t btlMyAct, btlFoeSquadN;
extern int8_t btlSwapWho;
extern Combatant btlFoeSquad[];
extern uint16_t squadMask;
extern bool pickOpen, lanWantHost;
extern uint8_t pickTrainer, pickPage;
extern bool pickHard;
void startLinkBattle();
void pickTap(int16_t x, int16_t y);
void pickDefault(uint8_t cap);
uint8_t squadCap(uint8_t, bool);
uint8_t pickChosen();
#define PICK_LAN 0xFF

// The sketch never sets a transport itself -- linkNowBegin does, and there is
// no radio here -- so one is wired in to see what the UI actually sends.
static int sent=0; static uint8_t lastPkt[64]; static uint8_t lastLen=0;
static void capture(void*, const uint8_t*b, uint8_t n){
  sent++; lastLen = n < sizeof(lastPkt) ? n : sizeof(lastPkt); memcpy(lastPkt,b,lastLen);
}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static LinkMon mon(int16_t dex, uint8_t lvl, const char *nm){
  Pet p; p.dbgHatchAs(dex,false);
  p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=25;
  p.ageMinutes=(uint32_t)(lvl-1)*MINUTES_PER_LEVEL;
  p.relearnFromLevel();
  Combatant c; combatantFromPet(c,p);
  snprintf(c.name,sizeof(c.name),"%s",nm);
  LinkMon m; linkMonFrom(m,c); return m;
}

int main(){
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  // a party that is deliberately NOT the squad we advertise
  for (int i=0;i<PARTY_SLOTS;i++){ PartyMon m; m.dex=20+i; m.level=30;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; party.replaceAt(i,m); }
  squadMask = 0xFFFF;

  lan.begin(true,"HOST");
  snprintf(lan.peerName,sizeof(lan.peerName),"%s","RIVAL");
  lan.addMon(mon(9,50,"SHELL"));
  lan.addMon(mon(21,50,"SNORE"));
  lan.theirs[0]=mon(6,50,"BLAZE");
  lan.theirs[1]=mon(4,50,"SPOON");
  lan.theirsN=2;
  lan.state=LINK_READY;
  lan.send=capture;

  startLinkBattle();
  ck(battleOpen && btlLink && btlLinkHost, "the host starts a linked battle");
  // The party above is full of level-30 nobodies and the live pet is level 1.
  // If the squad were rebuilt from squadMask we would fight with those instead
  // of what the peer was told we have -- which is exactly the desync to avoid.
  ck(btlSquadN==2, "the squad is the advertised one, not the current party");
  ck(btlYou.dex==9 && btlYou.level==50, "and leads with what we sent");
  ck(!strcmp(btlSquad[1].name,"SNORE"), "including the rest of it");
  ck(btlFoe.dex==6 && btlFoe.hp==btlFoe.maxHp, "the rival leads with theirs, at full health");

  // --- the peer's team is LIVE, not rebuilt each time it is looked at
  ck(btlFoeSquadN==2, "the peer's whole team is held as combatants");
  btlFoeSquad[1].hp = 7;
  ck(btlFoeSquad[1].hp==7 && btlFoeSquad[1].maxHp>7,
     "so a creature that switches out can stay hurt");

  // --- the guest ASKS to switch; it never switches on its own, because the
  // host is the only side that may spend a turn
  btlLinkHost = false;
  lan.state = LINK_READY;
  btlMenu = 2;                       // the DIGIMON list
  uint8_t was = btlSquadAt;
  sent = 0;
  battleTap(69 + 168 + 10, 286 + 10);   // cell 1 of the switch grid
  ck(btlSquadAt==was, "the guest does not switch locally");
  ck(sent==1 && lastPkt[0]==LM_ACT, "it sends the request instead");
  ck(LINK_ACT_IS_SWITCH(lastPkt[3]) && LINK_ACT_SLOT(lastPkt[3])==1,
     "and the request names the slot it wants");

  // --- the host LATCHES its own action instead of throwing it away
  btlLinkHost = true;
  btlLink = true;
  btlOver = false;
  btlMsgCount = 0;
  lan.pendingAct = 0;
  btlMyAct = 0;
  btlMenu = 1;                       // the move grid
  uint16_t hpWas = btlFoe.hp;
  battleTap(69 + 10, 286 + 10);      // move slot 0
  ck(btlMyAct != 0, "the host latches its move while the rival is still choosing");
  ck(btlFoe.hp == hpWas, "and nothing is resolved yet");
  lan.pendingAct = LINK_ACT_MOVE(0);
  render();                          // btlLinkPoll spots that both are in
  ck(btlMyAct == 0 && !lan.pendingAct,
     "and the turn goes as soon as the rival's action lands");

  // --- the guest is sent the state at the END of the turn
  //
  // Burn and poison chip after both have acted. The host shipped the result
  // BEFORE applying it, so the guest's numbers were a step behind every turn --
  // and a creature the chip finished was still standing on the guest's screen,
  // up to and including the fight ending under it.
  {
    const Combatant keepYou = btlYou, keepFoe = btlFoe;
    btlOver = false; btlMsgCount = 0; btlSwapWho = -1;
    lan.pendingAct = 0; btlMyAct = 0; lan.state = LINK_READY;
    for (int i=0;i<MOVE_SLOTS;i++) btlYou.moves[i] = btlFoe.moves[i] = 0;
    btlYou.moves[0] = btlFoe.moves[0] = MV_GUARD;     // cannot miss, deals nothing
    btlYou.ailment = AIL_NONE; btlYou.confuseTurns = 0; btlYou.recharge = false; btlYou.charging = 0;
    btlFoe.confuseTurns = 0; btlFoe.recharge = false; btlFoe.charging = 0;
    btlFoe.ailment = AIL_POISON; btlFoe.hp = 1;       // the chip is what finishes it
    btlYou.hp = btlYou.maxHp;                         // whatever the turn above did to it
    sent = 0;
    btlMenu = 1;
    battleTap(69 + 10, 286 + 10);                     // our move slot 0
    lan.pendingAct = LINK_ACT_MOVE(0);
    render();
    ck(btlFoe.fainted(), "the poison chip finishes the rival's creature on the host");
    LinkResult got{};
    memcpy(&got, lastPkt + 3, sizeof(got));           // [type][len][turn] then the result
    ck(sent >= 1 && lastPkt[0] == LM_RESULT && got.guestHp == 0,
       "and the result the guest is sent already shows it down");
    btlYou = keepYou; btlFoe = keepFoe; btlSwapWho = -1; btlMsgCount = 0;
  }

  // --- a faint brings on whoever is STILL STANDING, not merely the next index
  //
  // Both squads that can switch assumed the next index was next in line. A
  // manual switch breaks that: send your last creature in early and its faint
  // ended a fight you still had a fighter for; a rival in the same position left
  // the host facing a fainted creature with no swap and no ending at all.
  {
    const Combatant keepYou = btlYou, keepFoe = btlFoe;
    const uint8_t keepAt = btlSquadAt, keepFoeAt = btlFoeAt;
    auto quiet = [](Combatant &c) {
      for (int i=0;i<MOVE_SLOTS;i++) c.moves[i] = 0;
      c.moves[0] = MV_GUARD; c.ailment = AIL_NONE; c.confuseTurns = 0;
      c.recharge = false; c.charging = 0; c.hp = c.maxHp;
    };
    auto turn = []() {
      btlOver = false; btlMsgCount = 0; btlSwapWho = -1;
      lan.pendingAct = 0; btlMyAct = 0; lan.state = LINK_READY;
      btlMenu = 1;
      battleTap(69 + 10, 286 + 10);
      lan.pendingAct = LINK_ACT_MOVE(0);
      render();
    };
    auto dismiss = []() { for (int i=0;i<12 && btlMsgCount;i++) battleTap(233, 300); };

    // our side: the LAST member is out, the first is fine, and the chip drops it
    for (uint8_t i=0;i<btlSquadN;i++) quiet(btlSquad[i]);
    btlSquadAt = (uint8_t)(btlSquadN - 1);
    btlYou = btlSquad[btlSquadAt];
    btlYou.ailment = AIL_POISON; btlYou.hp = 1;
    btlFoe = btlFoeSquad[btlFoeAt]; quiet(btlFoe);
    turn();
    ck(btlYou.fainted(), "our last member goes down to the chip (the set-up holds)");
    ck(!btlOver && btlSwapWho == 0,
       "and the fight goes on, because the first member is still standing");
    dismiss();
    ck(btlSquadAt == 0 && !btlYou.fainted(), "it is the first member that steps in");

    // the rival's side: their last is out, their first is fine
    for (uint8_t i=0;i<btlFoeSquadN;i++) quiet(btlFoeSquad[i]);
    btlYou = btlSquad[btlSquadAt]; quiet(btlYou);
    btlFoeAt = (uint8_t)(btlFoeSquadN - 1);
    btlFoe = btlFoeSquad[btlFoeAt];
    btlFoe.ailment = AIL_POISON; btlFoe.hp = 1;
    turn();
    ck(btlFoe.fainted(), "the rival's last member goes down to the chip");
    ck(!btlOver && btlSwapWho == 1, "and the host does not call the fight won early");
    dismiss();
    ck(btlFoeAt == 0 && !btlFoe.fainted() && btlSwapWho < 0,
       "their first member is brought on, so the fight can take another turn");

    btlYou = keepYou; btlFoe = keepFoe; btlSquadAt = keepAt; btlFoeAt = keepFoeAt;
    btlSwapWho = -1; btlMsgCount = 0; btlOver = false;
  }

  // --- a peer that goes quiet ends the fight rather than hanging on it
  btlOver = false; btlMsgCount = 0;
  lan.state = LINK_LOST;
  render();
  ck(btlOver, "a lost peer ends the battle instead of waiting forever");
  lan.state = LINK_READY;
  btlOver = false; btlMsgCount = 0;

  // --- a result off the wire moves the guest's battle, and only once
  btlLink = true; btlLinkHost = false; btlMenu = 0; btlMsgCount = 0;
  btlYou = btlSquad[btlSquadAt];
  LinkResult r{};
  r.hostHp = btlFoe.maxHp/2; r.guestHp = btlYou.maxHp/4;
  r.hostMove = 1; r.guestMove = 1; r.hostDmg = 5; r.guestDmg = 7;
  r.hostIdx = 0; r.guestIdx = 0;
  r.guestAil = AIL_BURN;
  memcpy(lan.result,&r,sizeof(r)); lan.resultN=sizeof(r); lan.resultNew=true;
  render();
  ck(btlYou.hp==r.guestHp && btlFoe.hp==r.hostHp, "the guest takes the host's numbers");
  ck(btlYou.ailment==AIL_BURN, "and the ailment it was handed");
  ck(!lan.resultNew, "a result is consumed once");
  uint16_t hpNow = btlYou.hp;
  render();
  ck(btlYou.hp==hpNow, "so a second frame does not replay the turn");

  // --- the rival's next creature arrives when the host says so
  r.hostIdx = 1; r.hostHp = 60;
  memcpy(lan.result,&r,sizeof(r)); lan.resultNew=true;
  render();
  ck(btlFoeAt==1 && btlFoe.dex==4, "the rival's next creature comes off the wire");

  // --- and the guest never decides the ending
  lan.youWon = true; lan.state = LINK_DONE;
  render();
  ck(btlOver && btlWon, "the guest takes the ending from the host");

  // a short or empty result must be dropped, not read past the end
  btlOver=false; lan.state=LINK_READY;
  uint16_t before = btlYou.hp;
  lan.resultN = 3; lan.resultNew = true;
  render();
  ck(!lan.resultNew && btlYou.hp==before, "a runt result is dropped, not half-applied");

  // --- the team you pick is the team that gets offered
  {
    lanOpen = false; battleOpen = false; btlLink = false;
    for (int i=0;i<PARTY_SLOTS;i++){ PartyMon m; m.dex=30+i*5; m.level=45;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; party.replaceAt(i,m); }
    lanWantHost = true;
    pickTrainer = PICK_LAN; pickHard = false; pickPage = 0;
    pickDefault(squadCap(PICK_LAN, false));
    ck(squadCap(PICK_LAN,false)==TRAINER_TEAM_MAX,
       "a LAN battle is uncapped: bring what you like");
    pickOpen = true;

    // keep only the live pet and party slot 1 (bits 0 and 2)
    squadMask = (1 << 0) | (1 << 2);
    ck(pickChosen()==2, "two chosen");
    lan.begin(true,"T");
    pickTap(300, 366);                    // FIGHT (right of BACK now)
    ck(!pickOpen && lanOpen, "confirming the team opens the LAN screen");
    ck(lan.mineN==2, "and offers exactly what was chosen, not the whole party");
    ck(lan.mine[1].dex==35, "including the right party member");
    // the emulator has no radio, so the offer cannot go anywhere -- but the
    // squad must still have been built, which is the half that matters here
    ck(lan.state==LINK_REFUSED, "with no radio it says so rather than hanging");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
