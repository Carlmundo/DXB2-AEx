import test from 'node:test';
import assert from 'node:assert/strict';
import {parseRivalBoard,unwrapBoard,cellAt,boardSvg,playableCells,RIVAL_BOARD_BYTES} from '../src/rival-board.mjs';

function u32(n) { const b=Buffer.alloc(4);b.writeUInt32LE(n>>>0);return b; }
function string(s) {const b=Buffer.from(s+'\0','latin1');return Buffer.concat([u32(b.length),b]);}
function fixture() {
  const cells=Buffer.alloc(740*4);cells.writeInt32LE(7,(36*20+19)*4);cells.writeInt32LE(-3,(4*20+6)*4);
  const extra=Buffer.alloc(256);extra[255]=91;
  return Buffer.concat([Buffer.from('BDST'),u32(7),u32(123456),u32(0),u32(1),
    string('X:\\original\\bricksfull.img'),string('X:\\original\\backgroundsfull.img'),
    string('<Test & boards>'),string('Author'),string(''),u32(2),cells,extra]);
}
function wrap(payload) {
  let b=Buffer.from(payload);
  for(let pass=0;pass<7;pass++) {
    const key=Buffer.from(Array.from({length:7},(_,i)=>pass*9+i));
    b=Buffer.from(b);
    for(let i=0;i<b.length;i++) b[i]^=key[i%key.length];
    b=Buffer.concat([b,key,Buffer.from([0xa7])]);
  }
  return Buffer.concat([u32((b.length+4)^0xabbafad5),b]);
}

test('preserves all 740 cells, column order, signed values and opaque metadata',()=>{
  const payload=fixture(),set=parseRivalBoard(payload);
  assert.equal(set.title,'<Test & boards>');assert.equal(set.brickFile,'bricksfull.img');
  assert.equal(set.backgroundFile,'backgroundsfull.img');assert.equal(set.boards[0].background,2);
  assert.equal(cellAt(set.boards[0],36,19),7);assert.equal(cellAt(set.boards[0],4,6),-3);
  assert.equal(set.boards[0].legacyData[255],91);
  assert.equal(set.payloadBytes-set.dataOffset,RIVAL_BOARD_BYTES);
  assert.throws(()=>cellAt(set.boards[0],37,0),RangeError);
});
test('unwraps legacy envelopes without mutating the input',()=>{
  const plain=fixture(),encoded=wrap(plain),before=Buffer.from(encoded);
  assert.deepEqual(unwrapBoard(encoded),plain);assert.deepEqual(encoded,before);
  assert.equal(parseRivalBoard(encoded).boards.length,1);
});
for(const networkFlag of [0,1])test(`reconstructs an empty upper half with network flag ${networkFlag}, preserving IDs and source`,()=>{
  const payload=fixture();payload.writeUInt32LE(networkFlag,12);
  const firstGrid=payload.length-256-740*4;
  payload.fill(0,firstGrid,firstGrid+740*4);
  for(const [x,y,id] of [[0,19,7],[36,10,-3],[18,14,218]]) payload.writeInt32LE(id,firstGrid+(x*20+y)*4);
  const before=Buffer.from(payload),set=parseRivalBoard(payload),grid=playableCells(set);
  assert.equal(set.mirrored,true);
  assert.equal(set.networkCompatible,networkFlag===1);
  for(const [x,y,id] of [[0,19,7],[36,10,-3],[18,14,218]]) {
    assert.equal(cellAt(set.boards[0],x,19-y),0);
    assert.equal(grid[x*20+y],id);
    assert.equal(grid[x*20+19-y],id);
  }
  assert.equal(grid.reduce((n,id)=>n+(id!==0),0),6);
  assert.deepEqual(payload,before);
  assert.ok(boardSvg(set).includes('x=36, y=9, id=-3'));
  assert.throws(()=>playableCells(set,1),RangeError);
});
test('network eligibility does not overwrite an occupied upper half',()=>{
  const payload=fixture();payload.writeUInt32LE(1,12);
  const set=parseRivalBoard(payload);
  assert.equal(set.networkCompatible,true);assert.equal(set.mirrored,false);
  assert.deepEqual(playableCells(set),set.boards[0].cells);
});
test('reflection is decided separately for each board in a mixed set',()=>{
  const full=fixture(),start=full.length-RIVAL_BOARD_BYTES;
  const half=Buffer.from(full.subarray(start));half.fill(0,4,4+740*4);half.writeInt32LE(17,4+(36*20+19)*4);
  const payload=Buffer.concat([full.subarray(0,start),half,full.subarray(start)]);payload.writeUInt32LE(2,16);
  const set=parseRivalBoard(payload);
  assert.equal(set.mirroredBoardCount,1);
  assert.equal(playableCells(set,0)[36*20],17);
  assert.deepEqual(playableCells(set,1),set.boards[1].cells);
});
test('ordinary sets keep their asymmetric layout and playable grids do not alias raw cells',()=>{
  const set=parseRivalBoard(fixture()),grid=playableCells(set);
  assert.equal(set.mirrored,false);assert.deepEqual(grid,set.boards[0].cells);
  assert.equal(grid[36*20],0);grid[36*20+19]=99;
  assert.equal(cellAt(set.boards[0],36,19),7);
});
test('rejects Anniversary version 7 rather than misidentifying it as Rival Ball',()=>{
  const modern=Buffer.concat([Buffer.from('BDST'),u32(7),u32(25),u32(20),u32(0),u32(16),Buffer.alloc(16)]);
  assert.throws(()=>parseRivalBoard(modern),/count/);
});
test('rejects truncated payloads, trailing bytes, unsupported versions and invalid strings',()=>{
  assert.throws(()=>parseRivalBoard(fixture().subarray(0,-1)),/length/);
  assert.throws(()=>parseRivalBoard(Buffer.concat([fixture(),Buffer.from([0])])),/length/);
  const version=fixture();version.writeUInt32LE(9,4);assert.throws(()=>parseRivalBoard(version),/version 7/);
  const bad=fixture();bad.writeUInt32LE(0xffffffff,20);assert.throws(()=>parseRivalBoard(bad),/string length/);
  const noTerm=fixture();noTerm[24+noTerm.readUInt32LE(20)-1]=65;
  assert.throws(()=>parseRivalBoard(noTerm),/terminal NUL/);
});
test('rejects malformed envelope sizes and keys',()=>{
  const wrong=wrap(fixture());wrong[0]^=1;assert.throws(()=>unwrapBoard(wrong),/file length/);
  const key=wrap(fixture());key[key.length-1]=0;assert.throws(()=>unwrapBoard(key),/pass count/);
  const truncated=Buffer.alloc(8);truncated.writeUInt32LE((8^0xabbafad5)>>>0);truncated[7]=15;
  assert.throws(()=>unwrapBoard(truncated),/key/);
});
test('SVG escapes labels, includes the last source column, and validates the board index',()=>{
  const set=parseRivalBoard(fixture()),svg=boardSvg(set);
  assert.ok(svg.includes('&lt;Test &amp; boards&gt;'));
  assert.ok(svg.includes('x=36, y=19, id=7'));
  assert.throws(()=>boardSvg(set,1),RangeError);
});
