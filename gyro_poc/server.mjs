import http from 'node:http';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import { WebSocketServer, WebSocket } from 'ws';
import QRCode from 'qrcode';
import { playbackTime, positionAt, DURATION } from './public/path.js';
import { createGameBridge } from './game-bridge.mjs';
import path from 'node:path';

export function createApp({ recordingDirectory = new URL('./recordings/',import.meta.url), gameBridgeFactory = createGameBridge, streamDirectory = path.join(process.env.LOCALAPPDATA??'', 'FiveM/FiveM.app/plugins/RockstarEditorPlus/GyroStream') } = {}) {
  const rooms = new Map();
  let publicOrigin;
  let gameRoom=null;
  let readingFrame=false;
  const gameBridge=gameBridgeFactory(async data=>{
    if(data.type==='streamFrame'){
      const owner=gameRoom;
      if(!owner || readingFrame || !Number.isInteger(data.slot) || data.slot<0 || data.slot>2)return;
      readingFrame=true;
      try{
        const jpeg=await readFile(path.join(streamDirectory,`frame${data.slot}.jpg`));
        if(owner!==gameRoom || jpeg.length<4 || jpeg.length>16*1024*1024 || jpeg[0]!==255 || jpeg[1]!==216 || jpeg.at(-2)!==255 || jpeg.at(-1)!==217)return;
        for(const ws of owner.sockets.values())if(ws.readyState===WebSocket.OPEN && ws.bufferedAmount===0)ws.send(jpeg,{binary:true});
      }catch{
        for(const ws of owner.sockets.values())if(ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify({type:'streamError',error:'Could not read the game capture frame.'}));
      }finally{readingFrame=false;}
      return;
    }
    if(gameRoom)for(const ws of gameRoom.sockets.values())if(ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify(data));
  });
  const takeData = room => ({version:1,id:room.take.id,createdAt:room.take.createdAt,coordinates:'right-handed; Y up; camera forward -Z; quaternion [x,y,z,w]',path:'arc-v1',fov:55,duration:room.take.samples.at(-1)?.t??0,samples:room.take.samples.slice()});
  function saveTake(room) {
    if(!room.take || room.take.samples.length<2)return;
    const data=takeData(room),json=JSON.stringify(data,null,2);
    room.save = (room.save??Promise.resolve()).catch(()=>{}).then(async()=>{
      if(recordingDirectory){await mkdir(recordingDirectory,{recursive:true});await writeFile(new URL(`${data.id}.json`,recordingDirectory),json);}
      for(const ws of room.sockets.values())if(ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify({type:'take',samples:data.samples.length,duration:data.duration,saved:!!recordingDirectory}));
    }).catch(()=>{for(const ws of room.sockets.values())if(ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify({type:'take',samples:data.samples.length,duration:data.duration,saved:false,error:'Disk save failed; download this take before closing.'}));});
  }
  const files = { '/': ['public/index.html', 'text/html'], '/app.js': ['public/app.js', 'text/javascript'], '/orientation.js': ['public/orientation.js', 'text/javascript'], '/path.js': ['public/path.js', 'text/javascript'], '/scene.js': ['public/scene.js', 'text/javascript'], '/style.css': ['public/style.css', 'text/css'], '/vendor/three.module.js': ['node_modules/three/build/three.module.js', 'text/javascript'], '/vendor/three.core.js': ['node_modules/three/build/three.core.js', 'text/javascript'] };
  const server = http.createServer(async (req, res) => {
    res.setHeader('Cache-Control', 'no-store');
    res.setHeader('X-Content-Type-Options', 'nosniff');
    try {
      const url = new URL(req.url, 'http://localhost');
      if(req.method==='GET' && url.pathname==='/session'){
        const id=url.searchParams.get('room'),room=rooms.get(id);
        if(!room || room.viewer!==url.searchParams.get('token')){res.writeHead(404);return res.end('Session expired.');}
        const phoneUrl=`${publicOrigin}/#room=${id}&token=${room.phone}&role=phone`;
        res.setHeader('Content-Type','application/json');
        return res.end(JSON.stringify({id,token:room.viewer,phoneUrl,qr:await QRCode.toDataURL(phoneUrl,{width:240,margin:2})}));
      }
      if(req.method==='GET' && url.pathname==='/take'){
        const room=rooms.get(url.searchParams.get('room')),token=url.searchParams.get('token');
        if(!room || (token!==room.viewer && token!==room.phone)){res.writeHead(403);return res.end();}
        if(!room.take || room.take.samples.length<2){res.writeHead(404);return res.end('No take yet.');}
        res.setHeader('Content-Type','application/json');
        return res.end(JSON.stringify(takeData(room)));
      }
      if(req.method==='GET' && url.pathname==='/take.js'){
        res.setHeader('Content-Type','text/javascript');return res.end(await readFile(new URL('./public/take.js',import.meta.url)));
      }
      if (req.method === 'POST' && url.pathname === '/session') {
        if (!publicOrigin) { res.writeHead(503); return res.end('HTTPS tunnel is starting. Refresh in a moment.'); }
        if (rooms.size >= 100) { res.writeHead(429); return res.end('Too many sessions.'); }
        const id = randomUUID(), viewer = randomUUID(), phone = randomUUID();
        const room = { viewer, phone, updated: Date.now(), sockets: new Map(), playback: {offset:0,startedAt:0,playing:false}, q:null };
        rooms.set(id, room);
        const phoneUrl = `${publicOrigin}/#room=${id}&token=${phone}&role=phone`;
        const qr = await QRCode.toDataURL(phoneUrl, { width: 240, margin: 2 });
        res.setHeader('Content-Type', 'application/json');
        return res.end(JSON.stringify({ id, token: viewer, qr, phoneUrl }));
      }
      if (req.method !== 'GET' || !files[url.pathname]) { res.writeHead(404); return res.end(); }
      const [file, type] = files[url.pathname];
      res.setHeader('Content-Type', `${type}; charset=utf-8`);
      res.end(await readFile(new URL(`./${file}`, import.meta.url)));
    } catch { res.writeHead(500); res.end('Request failed.'); }
  });
  const wss = new WebSocketServer({ noServer: true, maxPayload: 1024 });
  server.on('upgrade', (req, socket, head) => {
    const url = new URL(req.url, 'http://localhost');
    const room = rooms.get(url.searchParams.get('room'));
    const role = url.searchParams.get('role');
    if (url.pathname !== '/socket' || !room || !['viewer', 'phone'].includes(role) || room[role] !== url.searchParams.get('token')) {
      socket.end('HTTP/1.1 403 Forbidden\r\n\r\n'); return;
    }
    wss.handleUpgrade(req, socket, head, ws => {
      const previous=room.sockets.get(role);
      room.sockets.set(role, ws);
      if(previous)previous.close(4001,'Connection moved to a refreshed tab');
      room.updated = Date.now();
      const send = (peer, value) => { if (peer?.readyState === WebSocket.OPEN) peer.send(JSON.stringify(value)); };
      const status = () => send(room.sockets.get('viewer'), { type: 'phone', connected: room.sockets.has('phone') });
      const broadcast = value => { for (const peer of room.sockets.values()) send(peer,value); };
      const playback = () => ({type:'playback',...room.playback,serverTime:Date.now()});
      status();
      send(ws,{type:'gameTarget',enabled:gameRoom===room});
      send(ws,playback());
      if (room.q) send(ws,{type:'orientation',q:room.q});
      if(room.take)send(ws,{type:'take',samples:room.take.samples.length,duration:room.take.samples.at(-1)?.t??0});
      ws.on('message', raw => {
        room.updated = Date.now();
        try {
          const data = JSON.parse(raw);
          if(data.type==='gameCommand' && Number.isInteger(data.action) && data.action>=0 && data.action<=12){
            if(data.action===0){
              if(gameRoom===room){gameBridge.send(2);gameRoom=null;broadcast({type:'gameTarget',enabled:false});}
            }else if(!gameRoom || gameRoom===room || data.takeover===true || !gameRoom.sockets.has('phone') || Date.now()-(gameRoom.gameLastSample??0)>2500){
              if(gameRoom && gameRoom!==room){
                gameBridge.send(2);
                for(const peer of gameRoom.sockets.values())send(peer,{type:'gameTarget',enabled:false});
              }
              gameRoom=room;broadcast({type:'gameTarget',enabled:true});
              if(room.gameQ)gameBridge.send(0,room.gameQ);
              gameBridge.send(data.action,room.gameQ??[0,0,0,1]);
            }else send(ws,{type:'game',code:'CONTROL_OWNED',error:'Another active phone owns game control. Use Take game control to switch to this pairing.'});
            return;
          }
          if (role === 'phone' && data.type === 'orientation' && Array.isArray(data.q) && data.q.length === 4 && data.q.every(v => Number.isFinite(v) && Math.abs(v) <= 1.001)) {
            const length = Math.hypot(...data.q);
            if (Math.abs(length - 1) < 0.01) {
              room.q = data.q;
              if(Array.isArray(data.gameQ)&&data.gameQ.length===4&&data.gameQ.every(Number.isFinite)&&Math.abs(Math.hypot(...data.gameQ)-1)<.01){
                room.gameQ=data.gameQ;
                room.gameLastSample=Date.now();
                if(gameRoom===room)gameBridge.send(0,data.gameQ);
              }
              send(room.sockets.get('viewer'), { type: 'orientation', q: data.q });
              if(room.playback.playing && room.playback.mode!=='take' && room.take && Number.isFinite(data.t) && data.t>=0 && data.t<=DURATION && data.t<=playbackTime(room.playback,Date.now())+.5 && room.take.samples.length<2200){
                const previous=room.take.samples.at(-1);
                if(!previous || data.t>previous.t)room.take.samples.push({t:data.t,position:positionAt(data.t),q:data.q});
              }
            }
          } else if (data.type === 'sync' && Number.isFinite(data.sent)) {
            send(ws,{type:'sync',sent:data.sent,serverTime:Date.now()});
          } else if (data.type === 'playback' && ['play','pause','restart','watch'].includes(data.action)) {
            const now = Date.now();
            const offset = playbackTime(room.playback,now);
            if(data.action==='watch'){
              if(!room.take || room.take.samples.length<2) return;
              saveTake(room);
              room.playback={offset:0,startedAt:now,playing:true,mode:'take',end:room.take.samples.at(-1).t};
            }else{
              if(data.action!=='play' && room.playback.mode!=='take')saveTake(room);
              const start=data.action==='restart' || room.playback.mode==='take' || (data.action==='play' && offset>=DURATION);
              const next=start?0:offset;
              if(data.action==='play' && (next===0 || !room.take)){
                saveTake(room);
                room.take={id:randomUUID(),createdAt:new Date().toISOString(),samples:room.q?[{t:0,position:positionAt(0),q:room.q}]:[]};
              }
              room.playback={offset:next,startedAt:now,playing:data.action==='play',mode:'live'};
            }
            broadcast(playback());
          } else if (data.type === 'center') send(room.sockets.get(role === 'phone' ? 'viewer' : 'phone'), { type: 'center' });
        } catch { /* Ignore malformed samples. */ }
      });
      ws.on('error', () => {});
      ws.on('close', () => {
        if(room.sockets.get(role)!==ws)return;
        room.sockets.delete(role);
        if (role === 'phone') {
          if(gameRoom===room){gameBridge.send(2);gameRoom=null;broadcast({type:'gameTarget',enabled:false});}
          if(room.playback.mode!=='take')saveTake(room);
          room.playback = {...room.playback,offset:playbackTime(room.playback,Date.now()),startedAt:Date.now(),playing:false};
          broadcast(playback());
        }
        status();
      });
    });
  });
  const finish = setInterval(()=>{
    for(const room of rooms.values())if(room.playback.playing && playbackTime(room.playback,Date.now())>=(room.playback.end??DURATION)){
      room.playback={...room.playback,offset:room.playback.end??DURATION,startedAt:Date.now(),playing:false};
      if(room.playback.mode!=='take')saveTake(room);
      for(const ws of room.sockets.values())if(ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify({type:'playback',...room.playback,serverTime:Date.now()}));
    }
  },100);
  finish.unref();
  const cleanup = setInterval(() => {
    for (const [id, room] of rooms) if (Date.now() - room.updated > 3600000) {
      for (const ws of room.sockets.values()) ws.close();
      rooms.delete(id);
    }
    for (const ws of wss.clients) {
      if (ws.dead) ws.terminate();
      else { ws.dead = true; ws.once('pong', () => { ws.dead = false; }); ws.ping(); }
    }
  }, 30000);
  cleanup.unref();
  return {
    server,
    setPublicOrigin(value) { publicOrigin = value; },
    async close() { clearInterval(cleanup);clearInterval(finish);if(gameRoom){gameBridge.send(2);gameRoom=null;}for (const room of rooms.values())if(room.playback.mode!=='take')saveTake(room);for (const ws of wss.clients) ws.terminate(); wss.close(); await new Promise(resolve => server.close(resolve));await Promise.all([...rooms.values()].map(room=>room.save));gameBridge.close(); }
  };
}
