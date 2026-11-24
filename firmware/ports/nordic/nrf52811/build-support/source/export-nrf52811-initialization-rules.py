"""Freeze existing screen validation data for the no-Python Windows tools."""
from pathlib import Path
import hashlib,importlib.util,json,sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
source=ROOT/'../../../../tools/platforms/nordic/nrf52811/programmer/internal/programmer_codec/generated_commands_v2.py'
spec=importlib.util.spec_from_file_location('nrf11_rules',source)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
omitted={0x11,0x12,0x21,0x2f,0x32,0x40,0x41}
rows={}
for (driver,recipe),rule in module.SCREEN_RULES_V2.items():
    if driver in omitted:continue
    row=dict(rule,driver=int(driver),recipe=int(recipe),name=module.ScreenControllerV2(driver).name)
    if driver==0x22:row['colors']=sorted(set(row['colors'])-{4})
    rows[f'{driver}:{recipe}']=row
target=ROOT/'../../../../tools/platforms/nordic/nrf52811/programmer/internal/initialization-screen-rules.json'
target.write_text(json.dumps({'schemaVersion':1,'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'rules':rows},default=lambda x:sorted(x),indent=2)+'\n',encoding='utf-8',newline='\n')
print('Generated',len(rows),'existing Free screen rules')
