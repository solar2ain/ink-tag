"""Model the documented J-Link close-as-Go default; no USB or optical claim."""
class CoreState:
    def __init__(self):
        self.halted=True;self.context='app';self.connected=False
        self.implicit_go=0;self.app_starts=0;self.resets=0
        self.first_marker=True;self.refresh_inflight=False;self.interrupted_refresh=0
        self.events=[]
    def begin(self):
        assert not self.connected
        self.restart_on_close=True
    def resume(self,implicit=False):
        self.halted=False
        if implicit:self.implicit_go+=1
        if self.context=='app':
            self.app_starts+=1
            if self.first_marker:self.first_marker=False;self.refresh_inflight=True
        self.events.append(('implicit-go' if implicit else 'explicit-go',self.context))
    def observe(self,line):
        if line=='Exec SetRestartOnClose = 0':self.restart_on_close=False
        elif line=='connect':self.connected=True
        elif line=='halt':assert self.connected;self.halted=True
        elif line.startswith('loadfile '):assert self.halted;self.context='app'
        elif line=='SetPC 0x20000000':assert self.halted;self.context='worker'
        elif line=='Go':self.resume()
        elif line=='WaitHalt 30000':
            assert self.context=='worker';self.halted=True;self.events.append(('worker-bkpt','worker'))
        elif line=='Reset':
            assert self.connected
            if self.refresh_inflight:self.interrupted_refresh+=1
            self.refresh_inflight=False;self.halted=True;self.context='app';self.resets+=1
        elif line.startswith(('loadbin ','SaveBin ','VerifyBin ','WReg ','W4 ')):assert self.halted
        elif line=='qc':self.close()
    def close(self):
        if self.connected:
            if self.restart_on_close:self.resume(implicit=True)
            self.connected=False
