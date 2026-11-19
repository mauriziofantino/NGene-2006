/**
 * @file webApiPages.h
 *
 * @author Maurizio Fantino
 *
 * @date 19/11/2026
 *
 * @brief Pages served by the web user interface: summary, channels and position.
 *
 * @note The pages are offered as constants of the executable, so the interface needs no file of the
 *       installation, and what the browser receives is what the build produced (see utility/webApi.c).
 * @note The scripts read only the documents published by the main thread, about one answer per second,
 *       so the view of the user cannot slow the elaboration of the samples (see utility/webApi.h).
 */
#ifndef UTILITY_WEB_API_PAGES_H_INCLUDED
#define UTILITY_WEB_API_PAGES_H_INCLUDED
/** @brief Style of the three pages of the interface, and of the graph drawn on the channels page. */
#define WEB_UI_STYLE                                                                                                  \
    "<style>" \
    ":root{--bg:#0b0f14;--panel:#141b23;--line:#26313d;--ink:#dbe4ee;--dim:#8b98a8;--ok:#3fb950;" \
    "--warn:#f2b134;--err:#ff6b6b;--info:#6cb2ff}" \
    "body{margin:0;background:var(--bg);color:var(--ink);padding:14px;display:grid;gap:12px;" \
    "grid-template-columns:repeat(auto-fit,minmax(20rem,1fr));align-content:start;" \
    "font:13px/1.5 ui-sans-serif,system-ui,'Segoe UI',sans-serif}" \
    "h1{grid-column:1/-1;margin:0;font-size:19px;font-weight:600}" \
    "h2{margin:0 0 6px;font-size:14px;font-weight:600;color:var(--info)}" \
    ".c{background:var(--panel);border:1px solid var(--line);border-radius:11px;padding:10px 12px;" \
    "overflow:auto}" \
    "table{width:100%;border-collapse:collapse;font:12px/1.45 ui-monospace,'Cascadia Mono'," \
    "Consolas,monospace}" \
    "th{color:var(--dim);text-align:left;padding:3px 6px}" \
    "td{padding:3px 6px;border-top:1px solid var(--line);white-space:nowrap;font-variant-numeric:" \
    "tabular-nums}" \
    ".k{color:var(--dim)}.err{color:var(--err)}.warn{color:var(--warn)}.ok{color:var(--ok)}" \
    ".wide{grid-column:1/-1}.log{white-space:pre-wrap;font-size:12px;line-height:1.45}" \
    ".cmd{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:6px}" \
    "button{font:inherit;color:var(--ink);background:#1b242f;border:1px solid var(--line);" \
    "border-radius:8px;padding:5px 9px;cursor:pointer}button:hover{background:#22303e}" \
    ".bar{height:7px;border-radius:3px;background:var(--info);margin-top:8px}" \
    "svg{width:100%;height:auto;display:block}text{fill:var(--dim)}" \
    "</style>"
/**
 * @brief Script that publishes the documents read by the pages, and the commands asked of the
 *        receiver (see utility/webApi.h).
 */
#define WEB_UI_SCRIPT                                                                                                  \
    "<script>" \
    "var D={channels:[],cycles:0,paused:0},P={},L=[],H={channel:0,points:[]};" \
    "function n(x,f){if(x===null||x===undefined)return '-';return Number(x).toFixed(f)}" \
    "function jget(u,f){fetch(u).then(function(r){return r.json()}).then(function(j){f(j)})}" \
    "function ask(t){fetch('/command?text='+encodeURIComponent(t),{method:'POST'})}" \
    "function poll(){jget('/status/channels.json',function(j){D=j;if(j.paused===undefined)" \
    "D.paused=0;draw()});jget('/status/pvt.json',function(j){P=j;draw()});" \
    "jget('/status/log.json',function(j){L=j.rows||[];draw()});" \
    "jget('/status/history.json?channel='+H.channel,function(j){H=j;draw()})}" \
    "setInterval(poll,900);poll();" \
    "</script>"
/** @brief Page served on the root address: the summary of the receiver and the log. */
#define WEB_UI_PAGE_MAIN                                                                                               \
    "<!DOCTYPE html><html><head><meta charset='utf-8'><title>NGene Classic</title>" WEB_UI_STYLE \
    "</head><body><h1>NGene Classic</h1>" \
    "<section class='c'><h2>Position, velocity, time</h2><table><tbody>" \
    "<tr><td class='k'>state</td><td class='ok' id='pstate'>-</td></tr>" \
    "<tr><td class='k'>latitude d</td><td id='lat'>-</td></tr>" \
    "<tr><td class='k'>longitude d</td><td id='lon'>-</td></tr>" \
    "<tr><td class='k'>h WGS84 m</td><td id='alt'>-</td></tr>" \
    "<tr><td class='k'>speed m/s</td><td id='spd'>-</td></tr>" \
    "<tr><td class='k'>GDOP</td><td id='gdop'>-</td></tr>" \
    "<tr><td class='k'>bias GPS m</td><td id='bias'>-</td></tr>" \
    "<tr><td class='k'>drift m/s</td><td id='drift'>-</td></tr>" \
    "<tr><td class='k'>channels used</td><td id='used'>-</td></tr>" \
    "</tbody></table></section>" \
    "<section class='c'><h2>Elaboration</h2><table><tbody>" \
    "<tr><td class='k'>cycles</td><td id='cycles'>-</td></tr>" \
    "<tr><td class='k'>paused</td><td id='paused'>-</td></tr>" \
    "</tbody></table><div class='cmd' id='cmds'></div></section>" \
    "<section class='c wide'><h2>Log</h2><div class='log' id='log'></div></section>" \
    "<script>var done=0;" \
    "function draw(){document.getElementById('cycles').textContent=D.cycles;" \
    "document.getElementById('paused').textContent=D.paused?'yes':'no';" \
    "document.getElementById('pstate').textContent=P.valid?'fix available':'no fix';" \
    "document.getElementById('pstate').className=P.valid?'ok':'warn';" \
    "document.getElementById('lat').textContent=n(P.lat,7);" \
    "document.getElementById('lon').textContent=n(P.lon,7);" \
    "document.getElementById('alt').textContent=n(P.alt,1);" \
    "document.getElementById('spd').textContent=n(P.speed,2);" \
    "document.getElementById('gdop').textContent=n(P.gdop,2);" \
    "document.getElementById('bias').textContent=n(P.biasGps,3);" \
    "document.getElementById('drift').textContent=n(P.drift,4);" \
    "document.getElementById('used').textContent=P.usedForPvt;" \
    "document.getElementById('log').textContent=L.map(function(r){return String(r.text)}).join('\\n');" \
    "if(!done){jget('/status/commands.json',function(j){var b=document.getElementById('cmds'),i;" \
    "for(i=0;i<j.commands.length;i++){var x=document.createElement('button');" \
    "x.textContent=j.commands[i].name;x.title=j.commands[i].help;" \
    "x.onclick=(function(nm){return function(){ask(nm)}})(j.commands[i].name);b.appendChild(x)}" \
    "done=1})}" \
    "}</script>" WEB_UI_SCRIPT "</body></html>"
/** @brief Page served on /channels: the table of the channels and the graph of one of them. */
#define WEB_UI_PAGE_CHANNELS                                                                                              \
    "<!DOCTYPE html><html><head><meta charset='utf-8'><title>NGene Classic channels</title>" WEB_UI_STYLE \
    "</head><body><h1>Channels</h1>" \
    "<section class='c wide'><h2>State of every channel</h2><table><thead><tr>" \
    "<th>#</th><th>PRN</th><th>state</th><th>tracking</th><th>Doppler Hz</th><th>carrier Hz</th>" \
    "<th>prompt</th><th>for PVT</th><th>sample</th></tr></thead><tbody id='body'></tbody></table>" \
    "<div class='cmd'>channel of the graph:" \
    "<select id='picker' onchange='H.channel=Number(this.value);poll()'></select></div></section>" \
    "<section class='c wide'><h2>Channel <span id='gt'>0</span>: Doppler and prompt correlation</h2>" \
    "<svg id='graph' viewBox='0 0 720 200'></svg></section>" \
    "<script>" \
    "function draw(){var t=document.getElementById('body'),h='',i,ch,sel,k;" \
    "for(i=0;i<D.channels.length;i++){ch=D.channels[i];" \
    "h+='<tr><td>'+ch.index+'</td><td>'+ch.name+'</td><td>'+ch.state+'</td><td>'+ch.tracking+'</td>'+" \
    "'<td>'+n(ch.doppler,3)+'</td><td>'+n(ch.carrier,3)+'</td><td>'+n(ch.prompt,1)+'</td>'+" \
    "'<td>'+(ch.usedForPvt?'yes':'-')+'</td><td>'+ch.sample+'</td></tr>'}" \
    "t.innerHTML=h;" \
    "sel=document.getElementById('picker');" \
    "if(sel.options.length!==D.channels.length){sel.innerHTML='';" \
    "for(i=0;i<D.channels.length;i++){var o=document.createElement('option');" \
    "o.value=i;o.textContent=i+' '+(D.channels[i].name||'-');sel.appendChild(o)}}" \
    "if(!H.points||!H.points.length)return;" \
    "var s=document.getElementById('graph'),a=H.points[0][0],b=H.points[H.points.length-1][0],i2," \
    "d0=1e300,d1=-1e300,c0=1e300,c1=-1e300,pd='',pc='',x,y;" \
    "for(i2=0;i2<H.points.length;i2++){d0=Math.min(d0,H.points[i2][1]);d1=Math.max(d1,H.points[i2][1]);" \
    "c0=Math.min(c0,H.points[i2][2]);c1=Math.max(c1,H.points[i2][2])}" \
    "for(i2=0;i2<H.points.length;i2++){x=8+(H.points[i2][0]-a)/((b-a)||1)*700;" \
    "y=100-(H.points[i2][1]-d0)/((d1-d0)||1e-9)*84;" \
    "pd+=(i2?'L':'M')+x.toFixed(1)+' '+y.toFixed(1)}" \
    "for(i2=0;i2<H.points.length;i2++){x=8+(H.points[i2][0]-a)/((b-a)||1)*700;" \
    "y=192-(H.points[i2][2]-c0)/((c1-c0)||1e-9)*76;" \
    "pc+=(i2?'L':'M')+x.toFixed(1)+' '+y.toFixed(1)}" \
    "s.innerHTML='<line x1=\\'8\\' y1=\\'104\\' x2=\\'712\\' y2=\\'104\\' stroke=\\'#26313d\\'/>'+" \
    "'<path d=\\''+pd+'\\' fill=\\'none\\' stroke=\\'#6cb2ff\\' stroke-width=\\'1.7\\'/>'+" \
    "'<path d=\\''+pc+'\\' fill=\\'none\\' stroke=\\'#3fb950\\' stroke-width=\\'1.4\\'/>'+" \
    "'<text x=\\'10\\' y=\\'16\\' font-size=\\'11\\'>Doppler Hz</text>'+" \
    "'<text x=\\'10\\' y=\\'120\\' font-size=\\'11\\'>prompt correlation</text>';" \
    "}" \
    "</script>" WEB_UI_SCRIPT "</body></html>"
/** @brief Page served on /pvt: the estimation of the position, of the velocity and of the time. */
#define WEB_UI_PAGE_PVT                                                                                                \
    "<!DOCTYPE html><html><head><meta charset='utf-8'><title>NGene Classic position</title>" WEB_UI_STYLE \
    "</head><body><h1>Position, velocity and time</h1>" \
    "<section class='c'><h2>Fix</h2><table><tbody>" \
    "<tr><td class='k'>state</td><td class='ok' id='st'>-</td></tr>" \
    "<tr><td class='k'>lat d</td><td id='la'>-</td></tr>" \
    "<tr><td class='k'>lon d</td><td id='lo'>-</td></tr>" \
    "<tr><td class='k'>h WGS84 m</td><td id='hh'>-</td></tr>" \
    "<tr><td class='k'>sample</td><td id='rs'>-</td></tr>" \
    "</tbody></table></section>" \
    "<section class='c'><h2>Time and geometry</h2><table><tbody>" \
    "<tr><td class='k'>bias GPS m</td><td id='b'>-</td></tr>" \
    "<tr><td class='k'>bias Gal m</td><td id='bg'>-</td></tr>" \
    "<tr><td class='k'>drift m/s</td><td id='dr'>-</td></tr>" \
    "<tr><td class='k'>GDOP</td><td id='gd'>-</td></tr>" \
    "<tr><td class='k'>used</td><td id='uc'>-</td></tr>" \
    "</tbody></table><div class='bar' id='bar'></div></section>" \
    "<section class='c'><h2>Channels in the estimation</h2><table><thead><tr>" \
    "<th>PRN</th><th>state</th><th>tracking</th></tr></thead><tbody id='ub'></tbody></table></section>" \
    "<script>" \
    "function draw(){document.getElementById('st').textContent=P.valid?'available':'no fix yet';" \
    "document.getElementById('st').className=P.valid?'ok':'warn';" \
    "document.getElementById('la').textContent=n(P.lat,7);" \
    "document.getElementById('lo').textContent=n(P.lon,7);" \
    "document.getElementById('hh').textContent=n(P.alt,1);" \
    "document.getElementById('rs').textContent=P.sample;" \
    "document.getElementById('b').textContent=n(P.biasGps,3);" \
    "document.getElementById('bg').textContent=n(P.biasGalileo,3);" \
    "document.getElementById('dr').textContent=n(P.drift,4);" \
    "document.getElementById('gd').textContent=n(P.gdop,2);" \
    "document.getElementById('uc').textContent=P.usedForPvt;" \
    "document.getElementById('bar').style.width=Math.max(4,Math.min(100,100-Number(P.gdop||0)*8))+'%';" \
    "var h='',i,ch;" \
    "for(i=0;i<D.channels.length;i++){ch=D.channels[i];" \
    "if(ch.usedForPvt){h+='<tr><td>'+ch.name+'</td><td>'+ch.state+'</td><td>'+ch.tracking+'</td></tr>'}}" \
    "document.getElementById('ub').innerHTML=h||'<tr><td class=k>none in this estimation</td></tr>';" \
    "}" \
    "</script>" WEB_UI_SCRIPT "</body></html>"
#endif /* UTILITY_WEB_API_PAGES_H_INCLUDED */
