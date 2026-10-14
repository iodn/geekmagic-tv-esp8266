"""Ensure dashboard swatches use the actual firmware preset colors."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
source=(root/'src/display.cpp').read_text()
array=re.search(r'const ThemePalette kThemes\[.*?\] = \{([\s\S]*?)\n};',source).group(1)
colors=[tuple(map(int,c)) for c in re.findall(r'rgb565\((\d+), (\d+), (\d+)\)',array)]
web=(root/'src/webui.h').read_text()
labels=re.search(r'const themeLabels = \[([\s\S]*?)\n];',web).group(1)
rows=re.findall(r'value: (\d+), label: "([^"]+)", tones: \[([^\]]+)\]',labels)
assert len(colors)==len(rows)*10
for index,name,tones in rows:
 expected=['#%02X%02X%02X'%colors[int(index)*10+role] for role in [0,1,3]]
 actual=re.findall(r'#[0-9A-Fa-f]{6}',tones)
 assert actual==expected,(name,actual,expected)
print('Theme palette checks passed: dashboard swatches match device colors')
