// Renders the device's Wisp frames with the GAME's own art engine, so the box matches the app exactly.
// Usage: NODE_PATH=<dir with @resvg/resvg-js> node tools/export_device_art.js <path to WispKids/prototypes>
// Adds device-only faces (blink, drowsy, yawn) built from the game's own eye and mouth shapes.
const fs = require('fs'), path = require('path'), vm = require('vm');
const { Resvg } = require('@resvg/resvg-js');
const proto = path.resolve(process.argv[2] || '../wispkids/prototypes');
const EXTRA = `
  drowsy: { eyes: 'half', mouth: 'flat', blush: 1 },
  yawn: { eyes: 'closed', mouth: 'yawn', blush: 1 },
  b_neutral: { eyes: 'sleep', mouth: 'smile', blush: 1 },
  b_happy: { eyes: 'sleep', mouth: 'big', blush: 1 },
  b_shy: { eyes: 'sleep', mouth: 'wavy', blush: 2 },
  b_curious: { eyes: 'sleep', brows: ['', 'up'], mouth: 'small-o' },
  look_r: { eyes: 'side', mouth: 'smile', blush: 1 },
  look_l: { eyes: 'sidel', mouth: 'smile', blush: 1 },
  look_u: { eyes: 'up', mouth: 'small-o', blush: 1 },
`;
// a left-looking copy of the game's 'side' eye (mirror of the pupil and highlight offsets)
const SIDEL = "case 'sidel': return '<ellipse cx=\"' + (ex - 3 * s) + '\" cy=\"' + (ey + 2 * s) + '\" rx=\"' + 5 * s + '\" ry=\"' + 6.5 * s + '\" fill=\"' + INK + '\"/><circle cx=\"' + (ex - 1.5 * s) + '\" cy=\"' + (ey - 1 * s) + '\" r=\"' + 1.8 * s + '\" fill=\"#fff\"/>';\n    ";
const FRAMES = ['neutral', 'happy', 'laugh', 'excited', 'silly', 'shy', 'curious', 'drowsy', 'yawn', 'sleepy',
                'b_neutral', 'b_happy', 'b_shy', 'b_curious', 'kind', 'look_l', 'look_r', 'look_u'];
const ctx = { Math, console }; ctx.window = ctx; vm.createContext(ctx);
const art = fs.readFileSync(path.join(proto, 'season-one/art-characters.js'), 'utf8');
if (!art.includes('var EXPR = {') || !art.includes("case 'side':")) throw new Error('art-characters.js changed: update this script');
vm.runInContext(art.replace('var EXPR = {', 'var EXPR = {' + EXTRA).replace("case 'side':", SIDEL + "case 'side':"), ctx);
vm.runInContext(fs.readFileSync(path.join(proto, 'season-one-v2/src/wisp2.js'), 'utf8'), ctx);
vm.runInContext(`var WISP_COLOURS = { purple: ['#7B4DFF', '#C9B6FF'] };
var state = { wisp: { stage: 1, colour: 'purple', path: null } }; function esc(s){return s}
` + fs.readFileSync(path.join(proto, 'season-one-v2/src/champion.js'), 'utf8').split('/* Evolution step')[0], ctx);
const out = path.resolve(__dirname, '..', 'art'); fs.mkdirSync(out, { recursive: true });
for (const f of FRAMES) {
  const svg = ctx.wispSVG(1, 'purple', null, 'x', f).replace('<svg ', '<svg xmlns="http://www.w3.org/2000/svg" width="800" height="800" ');
  fs.writeFileSync(path.join(out, `spark_${f}.png`), new Resvg(svg).render().asPng());
}
console.log(`${FRAMES.length} frames rendered`);
