"""Capture RF power steps through Saleae Logic 2 MCP and the firmware test CLI.

Requires pyserial and Logic 2's MCP server. Output contains raw CSV, .sal files,
and JSON width histograms. Capture widths have the 100 MS/s sample resolution.
The last requested power stays active; pass 0 last to leave the output off.
"""
import json, urllib.request, sys
from pathlib import Path

def rpc(method, params):
    req=urllib.request.Request('http://127.0.0.1:10530', data=json.dumps({'jsonrpc':'2.0','id':1,'method':method,'params':params}).encode(),headers={'Content-Type':'application/json','Accept':'application/json, text/event-stream'})
    result=json.load(urllib.request.urlopen(req,timeout=30))
    if 'error' in result: raise RuntimeError(result)
    return result['result']

def call(name, **args):
    result=rpc('tools/call',{'name':name,'arguments':args})
    if result.get('isError'): raise RuntimeError(result)
    return result

import csv, json, sys, time, serial, statistics, collections
from pathlib import Path

def measure(path):
 with open(path) as f:
  reader=csv.reader(f); next(reader); rows=[(float(r[0]),int(r[1])) for r in reader]
 edges=[rows[0]]+[b for a,b in zip(rows,rows[1:]) if a[1]!=b[1]]
 pulses=[]
 for i in range(1,len(edges)-1):
  t,v=edges[i]
  if v==1 and edges[i+1][1]==0:
   pulses.append({'t':t,'width':(edges[i+1][0]-t)*1e9,'gap':(t-edges[i-1][0])*1e9})
 starts=[i for i,p in enumerate(pulses) if p['gap']>5000]
 first=[pulses[i]['width'] for i in starts]
 regular=[p['width'] for p in pulses if p['gap']<5000]
 def hist(v):return dict(collections.Counter(round(x,1) for x in v))
 return {'pulses':len(pulses),'first_ns':hist(first),'other_ns':hist(regular),'burst_pulses':hist([b-a for a,b in zip(starts,starts[1:])]),'burst_period_ns':hist([(pulses[b]['t']-pulses[a]['t'])*1e9 for a,b in zip(starts,starts[1:])]),'gap_ns':hist([pulses[i]['gap'] for i in starts])}

def capture(label,powers):
 out={}
 with serial.Serial(PORT,115200,timeout=.3) as s:
  time.sleep(.5)
  for index,power in enumerate(powers):
   s.reset_input_buffer(); s.write(f'power {power}\n'.encode()); time.sleep(.2)
   reply=s.read(8192).decode(errors='replace')
   if f'OK: RF power requested: {power}%' not in reply: raise RuntimeError(repr(reply))
   p=ROOT/f'{label}-{index:03d}-{power}';p.mkdir(exist_ok=True)
   cid=call('start_capture',deviceId=DEVICE,logicDeviceConfiguration={'logicChannels':{'digitalChannels':[0]},'digitalSampleRate':100000000},captureConfiguration={'timedCaptureMode':{'durationSeconds':.003}})['structuredContent']['captureId']
   call('wait_capture',captureId=cid)
   call('export_raw_data_csv',captureId=cid,directory=str(p),analogDownsampleRatio=1)
   call('save_capture',captureId=cid,filepath=str(p/'capture.sal'))
   result=measure(p/'digital.csv');out[f"{index:03d}-{power}"]=result
   print(power,json.dumps(result),flush=True)
   call('close_capture',captureId=cid)
 (ROOT/f'{label}.json').write_text(json.dumps(out,indent=2))

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--device', required=True, help='Physical Saleae device ID')
    parser.add_argument('--output', type=Path, default=Path('.pio/rmt-captures'))
    parser.add_argument('label')
    parser.add_argument('powers', nargs='+', type=int, choices=range(101))
    args = parser.parse_args()
    if not args.label.replace('-', '').replace('_', '').isalnum():
        parser.error('label must contain only letters, numbers, hyphens or underscores')
    ROOT = args.output.resolve()
    ROOT.mkdir(parents=True, exist_ok=True)
    PORT, DEVICE = args.port, args.device
    devices = call('get_devices')['structuredContent']['devices']
    if not any(d['deviceId'] == DEVICE and not d['isSimulation'] for d in devices):
        parser.error('requested physical Saleae device is not connected')
    if any(ROOT.glob(args.label + '-*')) or (ROOT / (args.label + '.json')).exists():
        parser.error('label already exists in output directory; choose a new label')
    capture(args.label, args.powers)
