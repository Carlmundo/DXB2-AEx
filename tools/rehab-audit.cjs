// Read-only comparison of the original 1.3.2 and Anniversary 2.3 binaries.
const fs=require('node:fs'),crypto=require('node:crypto'),assert=require('node:assert/strict');
function inspect(path,hash,flag,speed,expectedRefs) {
  const b=fs.readFileSync(path),pe=b.readUInt32LE(60),opt=pe+24;
  assert.equal(crypto.createHash('sha256').update(b).digest('hex'),hash,'Unsupported executable');
  const base=b.readUInt32LE(opt+28),sections=[];
  for(let i=0;i<b.readUInt16LE(pe+6);++i){const p=opt+b.readUInt16LE(pe+20)+40*i;sections.push({rva:b.readUInt32LE(p+12),size:b.readUInt32LE(p+16),raw:b.readUInt32LE(p+20)});}
  const offset=a=>{const s=sections.find(s=>a-base>=s.rva&&a-base<s.rva+s.size);assert(s);return s.raw+a-base-s.rva;};
  const word=a=>b.readUInt32LE(offset(a)),float=a=>b.readFloatLE(offset(a));
  const refs=a=>{const needle=Buffer.alloc(4);needle.writeUInt32LE(a);const out=[];for(let p=b.indexOf(needle);p>=0;p=b.indexOf(needle,p+1)){const s=sections.find(s=>p>=s.raw&&p<s.raw+s.size);if(s)out.push(base+s.rva+p-s.raw);}return out;};
  assert.deepEqual(refs(flag),expectedRefs,'RehabMode reference inventory changed');
  assert.equal(float(speed),100,'Initial selected speed');
  return {path,sha256:hash,flag:'0x'+flag.toString(16),flagOperandReferences:refs(flag).map(a=>'0x'+a.toString(16)),initialSpeed:float(speed),b,offset,word,float};
}
const [original,anniversary,output]=process.argv.slice(2);
if(!output)throw new Error('Usage: node tools/rehab-audit.cjs original.exe anniversary.exe report.json');
const old=inspect(original,'981d0cdf275f60175bfd67d48fd5119120d7621cc46945af59172190ac547c2b',0x450d28,0x43d03c,[0x40250f,0x4046a6,0x404a50,0x405cd2,0x406bcb,0x408fde,0x40ccf9,0x40f09a]);
const modern=inspect(anniversary,'1b607921dce666ac5174c9e4cbad3787846ae33611de3c85ef84cb8272fdbd5d',0x5b91dc,0x5a40c4,[0x45d6d5,0x462475,0x462e0c,0x483c77,0x48a67d,0x48b85f,0x491496,0x4960bb]);
const keys=[];
for(let i=0;i<10;++i){
  const vk=i===9?0x30:0x31+i;
  const oldIndex=old.b[old.offset(0x402f2c)+vk-0x1b];
  const newIndex=modern.b[modern.offset(0x44e6b8)+vk-0x1b];
  assert.equal(old.word(0x402ecc+4*oldIndex),0x402a37+17*i,'Original keyboard dispatch');
  assert.equal(modern.word(0x44e65c+4*newIndex),0x44e164+15*i,'Anniversary keyboard dispatch');
  const legacy=old.float(0x42f53c-4*i),current=modern.float(0x44e164+15*i+6);
  assert.equal(legacy,25*(i+1));assert.equal(current,legacy);
  keys.push({key:i===9?'0':String(i+1),speed:legacy});
}
assert.equal(old.b.toString('ascii',old.offset(0x434cd8),old.offset(0x434cd8)+11),' Rehab Mode');
assert.equal(modern.word(0x48f16f),1,'Original save eligibility assignment changed');
const clean=({b,offset,word,float,...rest})=>rest;
fs.writeFileSync(output,JSON.stringify({original:clean(old),anniversary:clean(modern),keys,
  effects:['Skip paddle decrement on death','Reduced fresh-ball launch speed','Selected speed applied to moving balls','Selected speed used for eight-ball spawns','Selected speed caps speed adjustments','Rehab Mode menu label','Reject local high-score name entry'],
  anniversaryAdditions:['Native score-entry gate also prevents reaching Steam post-score prompt','Persist saved-game score ineligibility across restarts with RehabMode disabled'],
  method:'Exact fingerprints, complete direct flag operand inventory, speed constants and native-code execution tests. No actual scores submitted.'},null,2)+'\n');
console.log('PASS: complete direct flag-reference inventory for 1.3.2 and 2.3; matching ten numeric speeds; initial speed 100; original menu label; save eligibility gap.');
