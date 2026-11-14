// The original files are only read. No game assets are bundled with this project.
export const RIVAL_COLUMNS = 37;
export const RIVAL_ROWS = 20;
export const RIVAL_CELLS = RIVAL_COLUMNS * RIVAL_ROWS;
export const RIVAL_BOARD_BYTES = 4 + RIVAL_CELLS * 4 + 256;
const WRAPPER_KEY = 0xabbafad5;
const MAX_FILE_BYTES = 16 * 1024 * 1024;

export class BoardFormatError extends Error {
  constructor(message) { super(message); this.name = 'BoardFormatError'; }
}

// Shared legacy/Anniversary envelope. The low nibble of the final byte gives
// the number of XOR passes. Each pass stores its key at the end of the data.
export function unwrapBoard(input) {
  const source = Buffer.from(input);
  if (source.length < 8 || source.length > MAX_FILE_BYTES) {
    throw new BoardFormatError('Board file size is outside the supported range.');
  }
  if (source.toString('ascii', 0, 4) === 'BDST') return source;
  const expectedLength = (source.readUInt32LE(0) ^ WRAPPER_KEY) >>> 0;
  if (expectedLength !== source.length) {
    throw new BoardFormatError('Unrecognized board envelope or incorrect file length.');
  }
  const data = Buffer.from(source.subarray(4));
  let end = data.length;
  const passes = data[end - 1] & 15;
  if (passes < 7 || passes > 15) {
    throw new BoardFormatError('Invalid board envelope pass count.');
  }
  for (let pass = 0; pass < passes; pass++) {
    if (end < 1) throw new BoardFormatError('Truncated board envelope.');
    const length = data[--end] & 15;
    if (length < 7 || length > 15 || length > end) {
      throw new BoardFormatError('Invalid or truncated board envelope key.');
    }
    end -= length;
    const key = Buffer.from(data.subarray(end, end + length));
    for (let i = 0; i < end; i++) data[i] ^= key[i % length];
  }
  const payload = data.subarray(0, end);
  if (payload.toString('ascii', 0, 4) !== 'BDST') {
    throw new BoardFormatError('Decoded file does not contain a BDST board set.');
  }
  return payload;
}

class Reader {
  constructor(buffer) { this.buffer = buffer; this.offset = 0; }
  take(length) {
    if (!Number.isSafeInteger(length) || length < 0 || length > this.buffer.length - this.offset) {
      throw new BoardFormatError(`Truncated board data at byte ${this.offset}.`);
    }
    const value = this.buffer.subarray(this.offset, this.offset + length);
    this.offset += length;
    return value;
  }
  u32() { return this.take(4).readUInt32LE(0); }
  string() {
    const length = this.u32();
    if (length < 1 || length > 1024) throw new BoardFormatError('Invalid legacy string length.');
    const bytes = this.take(length);
    if (bytes[length - 1] !== 0 || bytes.subarray(0, -1).includes(0)) {
      throw new BoardFormatError('Legacy strings must have one terminal NUL.');
    }
    // These Windows-era assets use an ANSI encoding, not UTF-8.
    return new TextDecoder('windows-1252').decode(bytes.subarray(0, -1));
  }
}

function assetName(value) {
  const name = value.replaceAll('\\', '/').split('/').at(-1);
  if (!name || name === '.' || name === '..' || /[:\x00-\x1f]/.test(name) || !/\.img$/i.test(name)) {
    throw new BoardFormatError('Invalid image-set filename.');
  }
  return name;
}

export function parseRivalBoard(input) {
  const payload = unwrapBoard(input);
  const reader = new Reader(payload);
  if (reader.take(4).toString('ascii') !== 'BDST') throw new BoardFormatError('Missing BDST signature.');
  const version = reader.u32();
  if (version !== 7) throw new BoardFormatError(`Expected Rival Ball version 7; found ${version}.`);
  // These two legacy fields are NOT the Anniversary board count and width.
  // Field B controls network eligibility, not the per-board reflection rule.
  const legacyFields = [reader.u32(), reader.u32()];
  const count = reader.u32();
  if (count < 1 || count > 128) throw new BoardFormatError('Invalid Rival Ball board count.');
  const brickPath = reader.string();
  const backgroundPath = reader.string();
  const title = reader.string();
  const author = reader.string();
  const legacyText = reader.string();
  const brickFile = assetName(brickPath);
  const backgroundFile = assetName(backgroundPath);
  const dataOffset = reader.offset;
  if (payload.length - dataOffset !== count * RIVAL_BOARD_BYTES) {
    throw new BoardFormatError('Payload length does not match the Rival Ball v7 layout.');
  }
  const boards = [];
  for (let i = 0; i < count; i++) {
    const background = reader.u32();
    const cells = new Int32Array(RIVAL_CELLS);
    for (let j = 0; j < cells.length; j++) cells[j] = reader.take(4).readInt32LE(0);
    // The old loader copies this block unchanged; its semantics need further work.
    const legacyData = Buffer.from(reader.take(256));
    let upperSum=0;
    for(let x=0;x<RIVAL_COLUMNS;x++)for(let y=0;y<RIVAL_ROWS/2;y++)upperSum=(upperSum+cells[x*RIVAL_ROWS+y])>>>0;
    boards.push({background, cells, legacyData, mirrored:upperSum===0});
  }
  return {version, title, author, brickFile, backgroundFile, brickPath, backgroundPath,
    legacyFields, networkCompatible:legacyFields[1]===1,
    mirrored:boards.some(board=>board.mirrored), mirroredBoardCount:boards.filter(board=>board.mirrored).length,
    legacyText, columns:RIVAL_COLUMNS, rows:RIVAL_ROWS,
    dataOffset, payloadBytes:payload.length, boards};
}

export function cellAt(board, x, y) {
  if (!Number.isInteger(x) || !Number.isInteger(y) || x < 0 || x >= RIVAL_COLUMNS || y < 0 || y >= RIVAL_ROWS) {
    throw new RangeError('Cell coordinates are outside the Rival Ball grid.');
  }
  return board.cells[x * RIVAL_ROWS + y];
}

// Keep parsing lossless: raw cells remain exactly as stored on disk. Consumers
// that display or play the board use this separate reconstructed grid.
export function playableCells(set, index = 0) {
  if (!Number.isInteger(index) || !set.boards[index]) throw new RangeError('Board number is outside this set.');
  const cells = new Int32Array(set.boards[index].cells);
  if (set.boards[index].mirrored) for (let x=0; x<RIVAL_COLUMNS; x++) for (let y=0; y<RIVAL_ROWS/2; y++) {
    cells[x*RIVAL_ROWS+y] = cells[x*RIVAL_ROWS+RIVAL_ROWS-1-y];
  }
  return cells;
}

export function summarizeBoardSet(set) {
  return {title:set.title, author:set.author, version:set.version,
    columns:set.columns, rows:set.rows, mirrored:set.mirrored, mirroredBoardCount:set.mirroredBoardCount,
    networkCompatible:set.networkCompatible, boardCount:set.boards.length,
    brickFile:set.brickFile, backgroundFile:set.backgroundFile,
    payloadBytes:set.payloadBytes, dataOffset:set.dataOffset,
    boards:set.boards.map((board, i) => ({number:i+1, background:board.background, mirrored:board.mirrored,
      occupiedCells:board.cells.reduce((n, id) => n + (id !== 0 ? 1 : 0), 0),
      playableOccupiedCells:playableCells(set,i).reduce((n,id)=>n+(id!==0 ? 1 : 0),0),
      distinctCellValues:[...new Set(board.cells)].sort((a,b)=>a-b),
      legacyDataNonzeroBytes:board.legacyData.reduce((n, byte)=>n+(byte!==0 ? 1 : 0),0)}))};
}

function xml(value) {
  return String(value).replace(/[&<>"']/g, c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[c]));
}

// A geometry-only view. Colours identify cell values; they are not the artwork.
export function boardSvg(set, index = 0) {
  if (!Number.isInteger(index) || !set.boards[index]) throw new RangeError('Board number is outside this set.');
  const cells = playableCells(set,index);
  const size = 16, margin = 24, top = 86;
  const width = RIVAL_COLUMNS * size + margin * 2;
  const height = RIVAL_ROWS * size + top + 36;
  const parts = [`<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">`,
    '<rect width="100%" height="100%" fill="#111827"/>',
    `<text x="24" y="30" fill="#f8fafc" font-family="sans-serif" font-size="18">${xml(set.title)} · Board ${index+1}/${set.boards.length}</text>`,
    `<text x="24" y="53" fill="#94a3b8" font-family="sans-serif" font-size="12">${xml(set.author)} · 37 × 20 · geometry only, colours represent cell IDs</text>`];
  for (let x=0; x<RIVAL_COLUMNS; x++) for (let y=0; y<RIVAL_ROWS; y++) {
    const id = cells[x*RIVAL_ROWS+y];
    const fill = id===0 ? '#1e293b' : `hsl(${((Math.abs(id)*137.508)%360).toFixed(2)},65%,62%)`;
    parts.push(`<rect x="${margin+x*size}" y="${top+y*size}" width="15" height="15" fill="${fill}"><title>x=${x}, y=${y}, id=${id}</title></rect>`);
  }
  parts.push('</svg>'); return parts.join('\n');
}
