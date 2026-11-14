// Read-only collection audit. Writes a report only when an output is supplied.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {unwrapBoard,parseRivalBoard,playableCells} from '../src/rival-board.mjs';

const [first,second,output]=process.argv.slice(2);
if(!first||!second)throw Error('Usage: node tools/audit-collection.mjs <Boards folder> <copy folder> [report.json]');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function walk(root,dir=root) {
  return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(root,path.join(dir,e.name)):[path.relative(root,path.join(dir,e.name))]).sort();
}
function inspect(root) {
  const files=walk(root),hashes=new Map(files.map(f=>[f.toLowerCase(),sha(fs.readFileSync(path.join(root,f)))]));
  const results=[];
  for(const file of files.filter(f=>/\.bd[sz]$/i.test(f))) {
    const row={file,sha256:hashes.get(file.toLowerCase())};results.push(row);
    try {
      const bytes=fs.readFileSync(path.join(root,file)),p=unwrapBoard(bytes),version=p.readUInt32LE(4);
      let boards,title,brick,background,mirrored=false;
      if(version===7) {
        const s=parseRivalBoard(bytes);
        ({title,mirrored}=s);brick=s.brickFile;background=s.backgroundFile;
        row.mirroredBoardCount=s.mirroredBoardCount;
        row.family='Rival';row.layoutFlag=s.legacyFields[1];
        boards=s.boards.map((b,i)=>({background:b.background,cells:playableCells(s,i)}));
        row.storedUpperHalfOccupied=s.boards.reduce((n,b)=>n+b.cells.reduce((n,id,i)=>n+(i%20<10&&id!==0?1:0),0),0);
      } else {
        if(![2,4].includes(version))throw Error(`Unaudited legacy format ${version}`);
        let at=12;
        function string(){const n=p.readUInt32LE(at);at+=4;if(n>1048576||at+n>p.length)throw Error('Invalid string');const s=p.toString('latin1',at,at+n).replace(/\0+$/,'');at+=n;return s;}
        brick=path.win32.basename(string());background=path.win32.basename(string());title=string();string();string();
        const count=p.readUInt32LE(8);if(p.length-at!==count*1604)throw Error('Legacy grid length mismatch');
        boards=Array.from({length:count},()=>{const bg=p.readUInt32LE(at);at+=4;const cells=Int32Array.from({length:400},()=>{const n=p.readInt32LE(at);at+=4;return n;});return {background:bg,cells};});
        row.family='Legacy DX-Ball';
      }
      Object.assign(row,{version,title,boardCount:boards.length,mirrored,
        // Anniversary's selection filter at VA 0x494f98 skips source versions < 7.
        menuEligible:version>=7,assets:[]});
      const capacities={};
      for(const [kind,name] of [['bricks',brick],['backgrounds',background]]) {
        const full=path.join(path.dirname(path.join(root,file)),name);
        const asset={kind,file:name,present:fs.existsSync(full)};row.assets.push(asset);
        if(!asset.present)throw Error(`Missing ${kind}: ${name}`);
        const b=fs.readFileSync(full);if(b.toString('ascii',0,4)!=='IMST'||b.readUInt32LE(4)!==1)throw Error(`Unrecognised image set: ${name}`);
        asset.frames=b.readUInt32LE(8);capacities[kind]=asset.frames;
      }
      row.maxBrickId=0;row.occupiedCells=0;row.negativeCells=0;row.referenceIssues=[];
      boards.forEach((b,i)=>{
        if(b.background>=capacities.backgrounds)row.referenceIssues.push({board:i+1,background:b.background});
        for(const id of b.cells) {
          if(id!==0)row.occupiedCells++;
          if(id<0)row.negativeCells++;
          row.maxBrickId=Math.max(row.maxBrickId,id);
          // Brick IDs are one-based; zero is an empty cell. Background indices
          // are zero-based. The last brick ID may equal the image frame count.
          if(id>capacities.bricks)row.referenceIssues.push({board:i+1,brick:id});
        }
      });
      row.valid=row.referenceIssues.length===0;
    } catch(e) {row.valid=false;row.error=e.message;}
  }
  const changed=files.filter(f=>hashes.get(f.toLowerCase())!==sha(fs.readFileSync(path.join(root,f))));
  return {root,files:files.length,boardFiles:results.length,boards:results.reduce((n,r)=>n+(r.boardCount??0),0),
    rivalSets:results.filter(r=>r.family==='Rival').length,rivalBoards:results.filter(r=>r.family==='Rival').reduce((n,r)=>n+(r.boardCount??0),0),
    mirroredSets:results.filter(r=>r.mirrored).length,mirroredBoards:results.reduce((n,r)=>n+(r.mirroredBoardCount??0),0),
    legacySets:results.filter(r=>r.family==='Legacy DX-Ball').length,legacyBoards:results.filter(r=>r.family==='Legacy DX-Ball').reduce((n,r)=>n+(r.boardCount??0),0),
    invalid:results.filter(r=>!r.valid).length,filesChangedDuringAudit:changed,results,hashes};
}
const a=inspect(path.resolve(first)),b=inspect(path.resolve(second));
const names=new Set([...a.hashes.keys(),...b.hashes.keys()]);
const differences=[...names].filter(n=>a.hashes.get(n)!==b.hashes.get(n));
delete a.hashes;delete b.hashes;
const report={folders:[a,b],copyDifferences:differences,
  limits:'Native parsing and layout checks do not establish gameplay completion. Legacy v2/v4 payloads parse but the current game selection filter excludes versions below 7.'};
if(output)fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({folders:report.folders.map(({results,...s})=>s),copyDifferences:differences,issues:report.folders.flatMap(f=>f.results.filter(r=>!r.valid).map(r=>({root:f.root,file:r.file,error:r.error,referenceIssueCount:r.referenceIssues?.length,examples:r.referenceIssues?.slice(0,3)})))},null,2));
if(a.invalid||b.invalid||differences.length||a.filesChangedDuringAudit.length||b.filesChangedDuringAudit.length)process.exitCode=1;
