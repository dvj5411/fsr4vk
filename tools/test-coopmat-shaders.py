#!/usr/bin/env python3
"""Validate every rewritten shipping shader; this is not a GPU accuracy test."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--rewrite',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parent.parent;a.output.mkdir(parents=True,exist_ok=False)
seen={};rows=[]
for folder in ('general','portable','fsr411/general','fsr411/colors'):
    for source in sorted((root/'assets'/folder).rglob('pass-*.spv')):
        digest=hashlib.sha256(source.read_bytes()).hexdigest()
        if digest not in seen:
            target=a.output/(digest+'.spv')
            extra=['--prepass-quads'] if folder.startswith('fsr411/') and source.name=='pass-01.spv' else []
            count=int(subprocess.check_output([str(a.rewrite.resolve()),str(source),str(target)]+extra,text=True))
            for env in ('vulkan1.1','vulkan1.3'):
                subprocess.run([str(a.validator.resolve()),'--target-env',env,str(target)],check=True,capture_output=True)
            seen[digest]=count
        rows.append(dict(source=str(source.relative_to(root)),blocks=seen[digest]))
(a.output/'report.json').write_text(json.dumps(rows,indent=2)+'\n')
for folder in ('general','portable','fsr411/general','fsr411/colors'):
    selection=[r for r in rows if r['source'].startswith('assets/'+folder+'/')]
    print(folder,'shaders',len(selection),'rewritten',sum(r['blocks']>0 for r in selection),'blocks',sum(r['blocks'] for r in selection),flush=True)
