// Verify preset selection actually applies the selected profile instead of retaining custom overrides.
const fs=require('fs'),vm=require('vm'),assert=require('assert').strict;
const source=require('./read-webui.cjs')();
function extract(name){const start=source.search(new RegExp(`^function ${name}\\(`,'m'));assert(start>=0);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');end++;}return source.slice(start,end);}
class Element {
 constructor(){this.children=[];this.dataset={};this.style={};this.attributes={};this.checked=false;this.value='0';this.listeners={};this.classes=new Set();this.classList={toggle:(name,on)=>on?this.classes.add(name):this.classes.delete(name)};}
 appendChild(child){this.children.push(child);}
 addEventListener(name,fn){this.listeners[name]=fn;}
 setAttribute(name,value){this.attributes[name]=value;}
 querySelectorAll(){return this.children;}
}
const nodes={};
for(const id of ['themePicker','nightThemePicker','themePreset','nightThemePreset','customThemeEnabled','nightCustomThemeEnabled','customThemeFields','nightCustomThemeFields','nightModeEnabled','nightThemeEnabled','nightThemePresetWrap','nightCustomThemeToggle'])nodes[id]=new Element();
nodes.customThemeFields.children=[new Element()];nodes.nightCustomThemeFields.children=[new Element()];
nodes.customThemeEnabled.checked=nodes.nightCustomThemeEnabled.checked=true;
nodes.nightModeEnabled.checked=nodes.nightThemeEnabled.checked=true;
const document={createElement:()=>new Element(),getElementById:id=>nodes[id],querySelectorAll:selector=>nodes[selector.split(' ')[0].slice(1)].children};
let scheduled=0;
const context=vm.createContext({document,themeLabels:[{value:0,label:'Blue',tones:['#000000']},{value:1,label:'Amber',tones:['#ffffff']}],scheduleLiveSync:()=>scheduled++});
vm.runInContext(['populateThemeButtons','updateThemeButtons','updateCustomThemeUI','updateNightThemeUI'].map(extract).join('\n'),context);
context.populateThemeButtons('themePicker','themePreset');context.populateThemeButtons('nightThemePicker','nightThemePreset');
nodes.themePicker.children[1].listeners.click();
assert.equal(nodes.themePreset.value,'1');assert.equal(nodes.customThemeEnabled.checked,false);
assert(nodes.customThemeFields.classes.has('hidden'));assert(nodes.customThemeFields.children[0].disabled);
assert.equal(nodes.nightCustomThemeEnabled.checked,true);assert.equal(nodes.themePicker.children[1].attributes['aria-pressed'],'true');
nodes.nightThemePicker.children[1].listeners.click();
assert.equal(nodes.nightThemePreset.value,'1');assert.equal(nodes.nightCustomThemeEnabled.checked,false);
assert(nodes.nightCustomThemeFields.classes.has('hidden'));assert(nodes.nightCustomThemeFields.children[0].disabled);
assert.equal(scheduled,2);
console.log('Theme picker passed: preset applies, custom overrides disabled, profiles independent, live preview scheduled');
