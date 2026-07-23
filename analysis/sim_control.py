import math, random
KN2MS=0.514444; DEG2RAD=0.0174532925
TURN_ON_LATACC=1.5; TURN_OFF_LATACC=1.0; TURN_DEBOUNCE_MS=800.0
POS_DEADBAND_MS=120.0; MIN_RELAY_ON_MS=100.0
SETTLE_AFTER_MOVE_MS=1500.0; SETTLE_BIG_ERR_MS=500.0
CTRL_STEP_DT=0.50; BIG_ERR_DEG=3.0; MAX_PULSE_MS=4000.0

class P:
    speedOnKn=17.0; speedOffKn=14.0; rollSetpointDeg=0.0; rollDeadbandDeg=1.5
    kP=0.06; fullStrokeMs=5000.0; maxDeployFrac=0.8; neutralFrac=0.0

class Control:
    # fix: krev reell yaw-rate over gulv for aa arme; maks-tid i sving; slipp raskere
    def __init__(s, fixed=False):
        s.posL=0.0; s.posR=0.0; s.trimFrac=0.0; s.settleMs=0.0
        s.hLU=s.hLD=s.hRU=s.hRD=0.0
        s.inTurn=False; s.turnOffMs=0.0; s.inTurnMs=0.0; s.fixed=fixed
    def integ(s,dt,lu,ld,ru,rd,p):
        dms=dt*1000.0
        if ld: s.posL+=dms
        if lu: s.posL-=dms
        if rd: s.posR+=dms
        if ru: s.posR-=dms
        s.posL=min(max(s.posL,0.0),p.fullStrokeMs); s.posR=min(max(s.posR,0.0),p.fullStrokeMs)
    def update(s,dt,rollDeg,yawRateDps,sogKn,p):
        lu=ld=ru=rd=False
        latAcc=abs(sogKn*KN2MS)*abs(yawRateDps*DEG2RAD)
        if not s.fixed:
            if latAcc>TURN_ON_LATACC:
                s.inTurn=True; s.turnOffMs=TURN_DEBOUNCE_MS
            elif s.inTurn and latAcc<TURN_OFF_LATACC:
                s.turnOffMs-=dt*1000.0
                if s.turnOffMs<=0: s.inTurn=False
        else:
            # --- FORESLAATT FIKS ---
            YAW_ON=12.0; YAW_OFF=7.0        # dps-gulv (uavhengig av fart)
            DEB=500.0; MAXTURN=4000.0        # kortere debounce, hard maks-tid
            arm = (yawRateDps and abs(yawRateDps)>YAW_ON) and (latAcc>TURN_ON_LATACC)
            if arm:
                s.inTurn=True; s.turnOffMs=DEB
            elif s.inTurn and (abs(yawRateDps)<YAW_OFF or latAcc<TURN_OFF_LATACC):
                s.turnOffMs-=dt*1000.0
                if s.turnOffMs<=0: s.inTurn=False
            if s.inTurn:
                s.inTurnMs+=dt*1000.0
                if s.inTurnMs>MAXTURN: s.inTurn=False; s.inTurnMs=0.0
            else:
                s.inTurnMs=0.0
        if s.inTurn:
            s.trimFrac=0.0; s.settleMs=0.0
            neut=min(max(p.neutralFrac,0.0),1.0)*p.fullStrokeMs
            if s.posL>neut+POS_DEADBAND_MS: lu=True
            if s.posR>neut+POS_DEADBAND_MS: ru=True
        else:
            s.settleMs-=dt*1000.0
            if s.settleMs<0: s.settleMs=0
            holdActive=(s.hLU>0 or s.hLD>0 or s.hRU>0 or s.hRD>0)
            if not (s.settleMs>0 or holdActive):
                e=rollDeg-p.rollSetpointDeg; eMag=0.0
                if e> p.rollDeadbandDeg: eMag=-(e-p.rollDeadbandDeg)
                elif e< -p.rollDeadbandDeg: eMag=-(e+p.rollDeadbandDeg)
                if eMag==0: s.settleMs=0
                else:
                    s.trimFrac+=p.kP*eMag*CTRL_STEP_DT
                    s.trimFrac=min(max(s.trimFrac,-p.maxDeployFrac),p.maxDeployFrac)
                    tgtL=max(0.0,s.trimFrac)*p.fullStrokeMs; tgtR=max(0.0,-s.trimFrac)*p.fullStrokeMs
                    bigErr=abs(e)>BIG_ERR_DEG
                    effSettle=SETTLE_BIG_ERR_MS if bigErr else SETTLE_AFTER_MOVE_MS
                    def SET(v,delta): return max(v,min(max(delta,MIN_RELAY_ON_MS),MAX_PULSE_MS))
                    retr=p.fullStrokeMs*0.10; need=False
                    if tgtL>POS_DEADBAND_MS and s.posR>retr: s.hRU=SET(s.hRU,s.posR); need=True
                    elif tgtR>POS_DEADBAND_MS and s.posL>retr: s.hLU=SET(s.hLU,s.posL); need=True
                    else:
                        if s.posL<tgtL-POS_DEADBAND_MS: s.hLD=SET(s.hLD,tgtL-s.posL); need=True
                        elif s.posL>tgtL+POS_DEADBAND_MS: s.hLU=SET(s.hLU,s.posL-tgtL); need=True
                        if s.posR<tgtR-POS_DEADBAND_MS: s.hRD=SET(s.hRD,tgtR-s.posR); need=True
                        elif s.posR>tgtR+POS_DEADBAND_MS: s.hRU=SET(s.hRU,s.posR-tgtR); need=True
                    if need: s.settleMs=effSettle
        hdt=dt*1000.0
        if lu: s.hLD=0
        if ld: s.hLU=0
        if ru: s.hRD=0
        if rd: s.hRU=0
        if lu and s.hLU<=0: s.hLU=MIN_RELAY_ON_MS
        if ld and s.hLD<=0: s.hLD=MIN_RELAY_ON_MS
        if ru and s.hRU<=0: s.hRU=MIN_RELAY_ON_MS
        if rd and s.hRD<=0: s.hRD=MIN_RELAY_ON_MS
        if s.hLU>0: lu=True; s.hLU-=hdt
        if s.hLD>0: ld=True; s.hLD-=hdt
        if s.hRU>0: ru=True; s.hRU-=hdt
        if s.hRD>0: rd=True; s.hRD-=hdt
        s.integ(dt,lu,ld,ru,rd,p)
        return lu,ld,ru,rd

FLAP_GAIN=10.0; TAU=2.0
def run(name, dist_fn, yaw_fn, sog=30.0, T=60.0, fixed=False):
    p=P(); c=Control(fixed=fixed); dt=0.05; roll=0.0
    n=int(T/dt); inturn=0; last=[]; out_db=0; mx=0.0
    for i in range(n):
        t=i*dt; dist=dist_fn(t); yaw=yaw_fn(t)
        eff=(c.posL-c.posR)/p.fullStrokeMs
        roll+=((dist+FLAP_GAIN*eff)-roll)*dt/TAU
        c.update(dt, roll, yaw, sog, p)
        if c.inTurn: inturn+=1
        mx=max(mx,abs(roll))
        if t>T-15:
            last.append(roll)
            if abs(roll)>p.rollDeadbandDeg: out_db+=1
    rms=math.sqrt(sum(r*r for r in last)/max(1,len(last)))
    mean=sum(last)/max(1,len(last)); fo=100*out_db/max(1,len(last))
    print(f"{name:46s} | inTurn {100*inturn/n:5.1f}% | roll {mean:+5.2f}° RMS {rms:4.2f} | utenfor dodband {fo:5.1f}% | maks {mx:4.1f}°")

def wave_yaw(amp, spikes=()):
    r=random.Random(7)
    def f(t):
        for (t0,t1,val) in spikes:
            if t0<=t<t1: return val
        base=amp*math.sin(2*math.pi*t/6.0)+amp*0.5*math.sin(2*math.pi*t/2.3)
        return base+r.uniform(-1,1)*amp*0.4
    return f

def yawD(t):
    if 7<t<7.5: return 20.0
    return 5.0+2.0*math.sin(2*math.pi*t/5)
def yawE(t):
    if 10.0<=t<10.1: return 120.0
    return 2.0*math.sin(2*math.pi*t/6)

for tag,fx in (("=== NAAVAERENDE FIRMWARE ===",False),("\n=== MED FORESLAATT FIKS ===",True)):
    print(tag)
    run("A  +6° slagside, blank sjo (ingen yaw)", lambda t:6.0, lambda t:0.0, fixed=fx)
    run("B  +6° slagside, boelge ±4dps + kast @8s", lambda t:6.0, wave_yaw(4.0,[(8.0,8.4,20.0)]), fixed=fx)
    run("C  +6° slagside, boelge ±4dps + kast, 36kn", lambda t:6.0, wave_yaw(4.0,[(8.0,8.4,20.0)]), sog=36.0, fixed=fx)
    run("D  +6° slagside, kast @7s + lang 5dps sving", lambda t:6.0, yawD, sog=34.0, fixed=fx)
    run("E  +6° slagside, sensor-spike @10s", lambda t:6.0, yawE, sog=34.0, fixed=fx)

print("\nTerskler i yaw-rate for NAAVAERENDE logikk (dps):")
for kn in (18,26,30,36):
    on=TURN_ON_LATACC/(kn*KN2MS)/DEG2RAD; off=TURN_OFF_LATACC/(kn*KN2MS)/DEG2RAD
    print(f"  {kn:2d} kn: arm > {on:4.1f} dps, slipp foerst naar < {off:4.1f} dps")
