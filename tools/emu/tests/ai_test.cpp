// Does the hard-mode AI actually play better? A "smart" chooser that does not
// beat random is not smart, so this measures it head to head rather than
// asserting the code merely runs.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "trainers.h"
#include "dex.h"
#include "types.h"
#include <cstdio>
#ifndef TAMA_NATIVE_TEST
uint32_t g_seed=4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
#endif

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static void mk(Combatant &c,int16_t dex,uint8_t lvl){
  Pet p; p.dbgHatchAs(dex,false);
  p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=31;
  p.ageMinutes=(uint32_t)(lvl-1)*MINUTES_PER_LEVEL;
  p.relearnFromLevel(); combatantFromPet(c,p);
}
// returns true if side A won
static bool duel(int16_t da,int16_t db,uint8_t lvl,bool aSmart,bool bSmart){
  Combatant A,B; mk(A,da,lvl); mk(B,db,lvl);
  TurnLog lg; int t=0;
  while(!A.fainted()&&!B.fainted()&&t<300){
    t++;
    uint8_t ma=aiChooseMove(A,B,aSmart), mb=aiChooseMove(B,A,bSmart);
    Combatant *f=&A,*s=&B; uint8_t mf=ma,ms=mb;
    if(!battleMovesFirst(A,ma,B,mb)){f=&B;s=&A;mf=mb;ms=ma;}
    battleAct(*f,*s,mf,lg);
    if(!s->fainted()) battleAct(*s,*f,ms,lg);
    battleEndTurn(A,lg); battleEndTurn(B,lg);
  }
  return !A.fainted();
}
int main(){
  // mirror matches: same Digimon both sides, so only the choosing differs
  const int16_t roster[]={6,9,3,58,41,27,23,7};
  int smartWins=0,total=0;
  for(int i=0;i<8;i++)
    for(int r=0;r<25;r++){
      if(duel(roster[i],roster[i],50,true,false)) smartWins++;
      total++;
    }
  int pct=smartWins*100/total;
  printf("     mirror matches, smart vs random: %d/%d = %d%%\n",smartWins,total,pct);
  ck(pct>60,"the hard AI beats the random one in a mirror match");

  // it must take a kill when one is available
  Combatant me,foe; mk(me,6,50); mk(foe,3,50);
  foe.hp=6;                         // one hit from fainting
  // This block used to set the stage and then assert nothing at all.
  {
    uint8_t pick=aiChooseMove(me,foe,true);
    ck(pick && MOVE_TBL[pick].cat!=MC_STATUS &&
       battleDamage(me,foe,pick,false,217)>=foe.hp,
       "with the foe one hit from fainting, it attacks for the kill");
  }

  // A score is an int32 and can be tens of thousands: damage is clamped to
  // 65535, a kill adds 1000, and a stacked attacker against a stripped defender
  // is sixteen times the base. The running best was an int16_t, so a score past
  // 32767 wrapped NEGATIVE and the next, weaker move beat it.
  {
    Combatant s,f; mk(s,6,50); mk(f,3,50);
    f.maxHp=f.hp=65535;             // nothing here is a kill: pure damage scores
    f.base[SI_DEF]=f.base[SI_SPD]=10;   // a defender whose guard is gone
    s.moves[0]=MV_HEADBUTT; s.moves[1]=MV_TACKLE; s.moves[2]=s.moves[3]=0;
    // Find an attack stat that puts the strong move's real damage between
    // 40000 and 60000 -- through battleDamage() itself, not a copy of it.
    uint16_t lo=1, hi=65535;
    while(lo<hi){
      uint16_t mid=(uint16_t)(lo+(hi-lo)/2);
      s.base[SI_ATK]=mid;
      if(battleDamage(s,f,MV_HEADBUTT,false,236)<40000) lo=(uint16_t)(mid+1); else hi=mid;
    }
    s.base[SI_ATK]=lo;
    uint16_t big=battleDamage(s,f,MV_HEADBUTT,false,236);
    uint16_t small=battleDamage(s,f,MV_TACKLE,false,236);
    ck(big>32767 && big<60000 && small<32768 && small<big,
       "the overflow case is set up (a score past int16, a weaker one below it)");
    ck(aiChooseMove(s,f,true)==MV_HEADBUTT,
       "a score past 32767 still beats a weaker move instead of wrapping negative");
  }
  uint8_t pick=aiChooseMove(me,foe,true);
  uint16_t dmg=battleDamage(me,foe,pick,false,236);
  printf("     foe at 6hp: picks %s for ~%u\n",MOVE_TBL[pick].name,dmg);
  ck(dmg>=foe.hp,"it takes the kill when one is on the table");

  // it must not throw a move the target is immune to (there are none in the
  // element chart, so this guards against one ever being added)
  Combatant norm,ghost; mk(norm,27,50); mk(ghost,7,50);
  bool everImmune=false;
  for(int i=0;i<40;i++){
    uint8_t p2=aiChooseMove(norm,ghost,true);
    if(MOVE_TBL[p2].cat!=MC_STATUS && typeEffVsDex(MOVE_TBL[p2].type,ghost.dex)==0) everImmune=true;
  }
  ck(!everImmune,"it never picks a move the target is immune to");

  // and easy mode must stay dumb, or hard mode means nothing
  Combatant e1,e2; mk(e1,6,50); mk(e2,3,50);
  bool varied=false; uint8_t first=aiChooseMove(e1,e2,false);
  for(int i=0;i<40 && !varied;i++) if(aiChooseMove(e1,e2,false)!=first) varied=true;
  ck(varied,"easy mode still picks at random");

  printf("%s\n",bad?"FAILURES":"all good");
  return bad?1:0;
}
