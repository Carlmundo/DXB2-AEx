import {readFile, readdir, stat, writeFile} from 'node:fs/promises';
import path from 'node:path';
import {parseRivalBoard, summarizeBoardSet, boardSvg} from '../src/rival-board.mjs';

async function main() {
  const [command, source, ...args] = process.argv.slice(2);
  if (!['inspect','scan','preview'].includes(command) || !source) {
    throw new Error('Usage:\n  node tools/boards.mjs inspect <file.bdz>\n  node tools/boards.mjs scan <Boards directory>\n  node tools/boards.mjs preview <file.bdz> <board number> <output.svg>');
  }
  if (command==='scan') {
    if (args.length) throw new Error('scan accepts only a directory.');
    const results=[];
    for (const name of (await readdir(source)).filter(n=>/\.bdz$/i.test(n)).sort()) {
      if (!(await stat(path.join(source,name))).isFile()) continue;
      try {
        const set=parseRivalBoard(await readFile(path.join(source,name)));
        const assets=[];
        for(const asset of [set.brickFile,set.backgroundFile]) {
          // Windows filename matching is case insensitive; preserve the stored name.
          try { assets.push({file:asset,present:(await stat(path.join(source,asset))).isFile()}); }
          catch(error) { if(error.code!=='ENOENT') throw error; assets.push({file:asset,present:false}); }
        }
        results.push({file:name,supported:true,title:set.title,author:set.author,boards:set.boards.length,columns:set.columns,rows:set.rows,assets});
      } catch(error) { results.push({file:name,supported:false,error:error.message}); }
    }
    const report={sets:results.length,supported:results.filter(r=>r.supported).length,
      boards:results.reduce((n,r)=>n+(r.boards??0),0),results};
    console.log(JSON.stringify(report,null,2));
    if(results.some(r=>!r.supported || r.assets.some(a=>!a.present))) process.exitCode=2;
    return;
  }
  const set=parseRivalBoard(await readFile(source));
  if(command==='inspect') {
    if(args.length) throw new Error('inspect accepts only a board filename.');
    console.log(JSON.stringify(summarizeBoardSet(set),null,2)); return;
  }
  if(args.length!==2 || !/^[1-9]\d*$/.test(args[0]) || !/\.svg$/i.test(args[1])) {
    throw new Error('preview requires a positive board number and an output.svg path.');
  }
  await writeFile(args[1],boardSvg(set,Number(args[0])-1),{flag:'wx'});
  console.log(`Created ${path.resolve(args[1])}`);
}
main().catch(error=>{console.error(error.message);process.exitCode=1;});
