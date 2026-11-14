// Small read-only PE inspector for locating loader strings and references.
const fs = require('node:fs');
const b = fs.readFileSync(process.argv[2]);
const pe = b.readUInt32LE(0x3c);
const optional = pe + 24;
const base = b.readUInt32LE(optional + 28);
const sections = [];
for (let i = 0; i < b.readUInt16LE(pe + 6); i++) {
  const p = optional + b.readUInt16LE(pe + 20) + i * 40;
  sections.push({name:b.toString('ascii',p,p+8).replace(/\0/g,''), rva:b.readUInt32LE(p+12), size:b.readUInt32LE(p+16), raw:b.readUInt32LE(p+20)});
}
function va(offset) {const s=sections.find(s=>offset>=s.raw && offset<s.raw+s.size);return s ? base+s.rva+offset-s.raw : null;}
console.log(JSON.stringify({base:base.toString(16),sections},null,2));
const pattern = new RegExp(process.argv[3] || 'boardset|corrupted|bricksfull|backgroundsfull|\\.bdz','i');
for(const m of b.toString('latin1').matchAll(/[ -~]{5,}/g)) {
  if(!pattern.test(m[0]))continue;
  const address=va(m.index); if(address===null)continue;
  const needle=Buffer.alloc(4);needle.writeUInt32LE(address);
  const refs=[];
  for(let p=b.indexOf(needle);p!==-1;p=b.indexOf(needle,p+1))refs.push(va(p)?.toString(16));
  console.log(JSON.stringify({offset:m.index.toString(16),va:address.toString(16),text:m[0],refs}));
}
