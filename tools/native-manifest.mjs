// Generate an exact-build, operand-level patch manifest from dumpbin output.
// This tool never edits the supplied executable.
import fs from 'node:fs';
import crypto from 'node:crypto';

export const GAME_SHA256='1b607921dce666ac5174c9e4cbad3787846ae33611de3c85ef84cb8272fdbd5d';
export const OLD_LAYER=0x2094, NEW_LAYER=0x2e54, DELTA=NEW_LAYER-OLD_LAYER;

export function instructions(text) {
  const result=[];
  for(const line of text.split(/\r?\n/)) {
    const match=/^\s+([0-9A-F]{8}): ((?:[0-9A-F]{2} )+)\s+(\S.*)$/.exec(line);
    if(match) result.push({va:parseInt(match[1],16),bytes:Buffer.from(match[2].replaceAll(' ',''),'hex'),asm:match[3]});
    else if(/^\s{12}[0-9A-F]{2}(?: [0-9A-F]{2})*\s*$/.test(line) && result.length) {
      result.at(-1).bytes=Buffer.concat([result.at(-1).bytes,Buffer.from(line.trim().replaceAll(' ',''),'hex')]);
    }
  }
  return result;
}

export function makeManifest(binary, listing) {
  const hash=crypto.createHash('sha256').update(binary).digest('hex');
  if(hash!==GAME_SHA256) throw Error('Unsupported executable SHA-256: '+hash);
  const code=instructions(listing), patches=[];
  function operand(ins,oldValue,newValue,width,reason) {
    const old=Buffer.alloc(width),updated=Buffer.alloc(width);
    if(width===4){old.writeUInt32LE(oldValue>>>0);updated.writeUInt32LE(newValue>>>0);}
    else {old[0]=oldValue;updated[0]=newValue;}
    const at=ins.bytes.indexOf(old);
    if(at<0 || ins.bytes.indexOf(old,at+1)>=0)throw Error('Ambiguous operand: '+ins.va.toString(16));
    patches.push({rva:ins.va-0x400000+at,before:old.toString('hex'),after:updated.toString('hex'),reason, instruction:ins.asm});
  }
  // Stack temporaries end at the same address after growing downwards. Keep
  // their upper metadata/cookies in place, move their start and lower locals.
  const frames=[
    {start:0x452680,end:0x4526cc,allocation:0x2094,boundary:0},
    {start:0x452790,end:0x452874,allocation:0x20a0,boundary:-16},
    {start:0x4558d0,end:0x4559cd,allocation:0x209c,boundary:-20},
    {start:0x455ce0,end:0x456675,allocation:0x21b0,boundary:-228,handlerCookie:0x549aee},
    {start:0x4587c0,end:0x458a6d,allocation:0x20ac,boundary:-32},
    {start:0x491c60,end:0x491ca3,allocation:0x2098,boundary:-4},
  ];
  // The board loader's exception handler checks an EH cookie below the stack
  // allocation. Move that check with the enlarged frame; otherwise a corrupt
  // board triggers a security fast-fail before its exception can be caught.
  for(const frame of frames)if(frame.handlerCookie) {
    const ins=code.find(ins=>ins.va===frame.handlerCookie);
    if(!ins||!/^mov\s+ecx,dword ptr \[edx\+/.test(ins.asm))throw Error('Unexpected stack exception cookie check');
    operand(ins,-frame.allocation-0x10,-frame.allocation-0x10-DELTA,4,'expanded stack exception cookie');
  }
  for(const ins of code) {
    const text=ins.asm;
    const has=n=>new RegExp(`(?<![0-9A-F])0*${n.toString(16)}h`,'i').test(text);
    if(has(OLD_LAYER))operand(ins,OLD_LAYER,NEW_LAYER,4,'layer stride/size');
    if(has(0x3edd40b5))operand(ins,0x3edd40b5,Math.ceil(2**43/NEW_LAYER),4,'layer-count division');
    if(has(0x3edd4))operand(ins,0x3edd4,Math.floor(0x7fffffff/NEW_LAYER),4,'vector size limit');
    if(has(0x7dba8))operand(ins,0x7dba8,Math.floor(0xffffffff/NEW_LAYER),4,'unsigned vector size limit');
    for(const field of [0x2084,0x2088,0x208c,0x2090]) {
      if(has(field))operand(ins,field,field+DELTA,4,'layer metadata offset');
    }
    if(has(0x208) && ins.va<0x492000)operand(ins,520,740,4,'cell capacity');
    if(has(0x2080))operand(ins,0x2080,740*16,4,'cell storage bytes');
    if(has(0xffffdf6c) && !/\[ebp/.test(text))operand(ins,0xffffdf6c,(-NEW_LAYER)>>>0,4,'negative layer stride');
    if(ins.va>=0x452f40 && ins.va<0x45a000) {
      if(has(0x19)) {
        // The cell accessors use 25 as their hard upper bound on x.
        // All full imm32 forms in this region are x clamps; byte immediates
        // are included only for cmp instructions, after checking the listing.
        if(ins.bytes.length>=5 && /^(mov|cmp)\s/.test(text))operand(ins,25,36,text.startsWith('cmp')&&ins.bytes.at(-1)===25?1:4,'column clamp');
      }
      if(has(0x1a) && /^mov\s/.test(text))operand(ins,26,37,4,'unowned layer capacity');
    }
    const frame=frames.find(f=>ins.va>=f.start&&ins.va<f.end);
    if(frame) {
      const disp=/\[ebp(?:\+[^\]]*\+)?\+?(FFFF[0-9A-F]{4})h\]/.exec(text);
      if(disp) {
        const old=parseInt(disp[1],16)|0;
        if(old<frame.boundary)operand(ins,old,old-DELTA,4,'expanded stack temporary');
      }
      if(text===`mov         eax,${frame.allocation.toString(16).toUpperCase()}h` && frame.allocation!==OLD_LAYER) {
        operand(ins,frame.allocation,frame.allocation+DELTA,4,'expanded stack frame');
      }
    }
  }
  patches.sort((a,b)=>a.rva-b.rva);
  for(let i=1;i<patches.length;i++)if(patches[i-1].rva+patches[i-1].before.length/2>patches[i].rva)throw Error('Overlapping patches');
  return {sha256:hash,oldLayerBytes:OLD_LAYER,newLayerBytes:NEW_LAYER,patches};
}

if(process.argv[1]?.endsWith('native-manifest.mjs')) {
  const [binary,listing,output]=process.argv.slice(2);
  if(!binary||!listing||!output)throw Error('Usage: node tools/native-manifest.mjs DXBall2.exe dumpbin.asm output.json');
  const manifest=makeManifest(fs.readFileSync(binary),fs.readFileSync(listing,'utf8'));
  fs.writeFileSync(output,JSON.stringify(manifest,null,2)+'\n');
  const header=output.replace(/\.json$/i,'.h');
  fs.writeFileSync(header,'// Generated for the executable hash in native-manifest.mjs.\nstatic const OperandPatch kGridPatches[] = {\n'+manifest.patches.map(p=>{
    if(p.before.length!==8)throw Error('Expected DWORD patch');
    return `  {0x${p.rva.toString(16)}, 0x${Buffer.from(p.before,'hex').readUInt32LE().toString(16)}, 0x${Buffer.from(p.after,'hex').readUInt32LE().toString(16)}}, // ${p.reason}`;
  }).join('\n')+'\n};\n');
  console.log(`Wrote ${manifest.patches.length} operand patches to ${output}`);
}
