import dgram from 'node:dgram';
export function createGameBridge(onStatus,{port=3211,statusPort=3212}={}) {
  let socket,sequence=0;
  function start(){
    if(socket)return;
    socket=dgram.createSocket('udp4');
    socket.on('error',error=>onStatus({type:'game',error:error.message}));
    socket.on('message',(raw,remote)=>{
      if(remote.address!=='127.0.0.1' || remote.port!==port)return;
      try{const data=JSON.parse(raw);if(data.type==='game'||data.type==='streamFrame')onStatus(data);}catch{}
    });
    socket.bind(statusPort,'127.0.0.1');socket.unref();
  }
  return {
    send(action,q=[0,0,0,1]){
      start();
      const packet=Buffer.alloc(28);packet.writeUInt32LE(0x31525947);packet.writeUInt32LE((++sequence)>>>0,4);packet.writeUInt32LE(action,8);
      q.forEach((v,i)=>packet.writeFloatLE(v,12+i*4));
      socket.send(packet,port,'127.0.0.1');
    },
    close(){if(socket){socket.close();socket=null;}}
  };
}
