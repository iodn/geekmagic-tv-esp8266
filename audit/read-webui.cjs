// Resolve the same palette placeholder as the firmware build for source-based UI fixtures.
const fs=require('fs'),path=require('path');
module.exports=function(){
 const root=path.join(__dirname,'..');
 const palettes=JSON.parse(fs.readFileSync(path.join(root,'src/device-themes.json'),'utf8')).palettes;
 const displayHex=color=>{const rgb=[1,3,5].map(i=>parseInt(color.slice(i,i+2),16));return '#'+rgb.map((v,i)=>Math.floor((v>>(i===1?2:3))*255/(i===1?63:31)).toString(16).padStart(2,'0')).join('').toUpperCase();};
 const labels=palettes.map(p=>({value:p.id,label:p.label,family:p.family,mode:p.mode,tones:['background','surface','accent'].map(role=>displayHex(p.colors[role])),text:displayHex(p.colors.text)}));
 return fs.readFileSync(path.join(root,'src/webui.h'),'utf8').replace('/*DEVICE_THEME_PRESETS*/ []',JSON.stringify(labels).replace(/</g,'\\u003c'));
};
