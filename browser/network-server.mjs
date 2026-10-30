// Deterministic classic relay and local services. No public internet/DNS dependencies.
// Wire tags match wasixv2-vnet-web/codec.rs. Fidelity v1 is explicitly selected.
import assert from "node:assert/strict";
import net from "node:net";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { fileURLToPath } from "node:url";
import dgram from "node:dgram";
import utils from "./node_modules/playwright-core/lib/utilsBundle.js";
const u32=v=>{const b=Buffer.alloc(4);b.writeUInt32LE(v);return b;};
const u64=v=>{const b=Buffer.alloc(8);b.writeBigUInt64LE(BigInt(v));return b;};
const blob=v=>Buffer.concat([u64(v.length),Buffer.from(v)]);
const ip=v=>{
    if(!v.includes(":"))return Buffer.concat([u32(0),Buffer.from(v.split(".").map(Number))]);
    const [head,tail]=v.split("::");const left=head?head.split(":"):[],right=tail?tail.split(":"):[];
    const words=tail===undefined?left:[...left,...Array(8-left.length-right.length).fill("0"),...right];
    assert.equal(words.length,8);const b=Buffer.alloc(16);words.forEach((w,i)=>b.writeUInt16BE(parseInt(w,16),i*2));return Buffer.concat([u32(1),b]);
};
const address=(v,port)=>{const b=Buffer.alloc(2);b.writeUInt16LE(port);return Buffer.concat([ip(v),b]);};
class Reader {
    constructor(bytes){this.bytes=bytes;this.offset=0;assert(bytes.length<=1024*1024);}
    take(n){assert(Number.isSafeInteger(n)&&n>=0&&this.offset+n<=this.bytes.length,"short relay record");const v=this.bytes.subarray(this.offset,this.offset+n);this.offset+=n;return v;}
    u32(){return this.take(4).readUInt32LE();} u64(){return this.take(8).readBigUInt64LE();}
    blob(){return this.take(Number(this.u64()));} bool(){return this.take(1)[0]!==0;}
    request(){return this.bool()?this.u64():null;}
    ip(){const v=this.u32();assert(v===0||v===1);if(v===0)return [...this.take(4)].join(".");const b=this.take(16);return Array.from({length:8},(_,i)=>b.readUInt16BE(i*2).toString(16)).join(":");}
    addr(){return {v:this.ip(),port:this.take(2).readUInt16LE()};} end(){assert.equal(this.offset,this.bytes.length);}
}
const listen=server=>new Promise((resolve,reject)=>{server.once("error",reject);server.listen(0,"127.0.0.1",()=>{server.removeListener("error",reject);resolve(server.address().port);});});
const bind=(socket,host)=>new Promise((resolve,reject)=>{socket.once("error",reject);socket.bind(0,host,()=>{socket.removeListener("error",reject);resolve(socket.address().port);});});
export async function serveNetwork(profile="fidelity") {
    const sockets=new Set(), resources=new Set(), servers=[], helpers=new Set();
    const stats={connect:0,udp:0,emptyUdp:0,dns:0,reverse:0,forward:0,fin:0,reset:0,urgent:0,sendErrors:0,accepted:0,maxBuffered:0,commands:{}};
    const env={};let failure;let udpControl;let peerReady=false;let peerWaiter;let finWaiter;const icmpWaiters=[];
    const tcp=async(name,handler,host="127.0.0.1")=>{
        const server=net.createServer({allowHalfOpen:true},socket=>{sockets.add(socket);socket.on("close",()=>sockets.delete(socket));socket.on("error",()=>{});handler(socket);});
        servers.push(server);
        const port=host==="127.0.0.1"?await listen(server):await new Promise((resolve,reject)=>{server.once("error",reject);server.listen(0,host,()=>resolve(server.address().port));});
        env[name]=String(port);return port;
    };
    await tcp("TCP_ECHO",s=>{s.on("data",b=>s.write(b));s.on("end",()=>s.end());});
    await tcp("TCP6_ECHO",s=>{s.on("data",b=>s.write(b));s.on("end",()=>s.end());},"::1");
    await tcp("TCP_FIN",s=>{s.end("fin");let got="";s.on("data",b=>got+=b);s.on("end",()=>{try{assert.equal(got,"after-fin");stats.fin++;finWaiter?.end("ok");}catch(e){failure=e;}});});
    await tcp("TCP_RESET",s=>s.once("data",()=>s.resetAndDestroy()));
    await tcp("TCP_SINK",s=>s.pause());
    const reserve=net.createServer();env.TCP_REFUSED=String(await listen(reserve));await new Promise(r=>reserve.close(r));
    for(const [name,type,host] of [["UDP_ECHO","udp4","127.0.0.1"],["UDP6_ECHO","udp6","::1"]]){
        const socket=dgram.createSocket(type);resources.add(socket);env[name]=String(await bind(socket,host));socket.on("message",(bytes,peer)=>socket.send(bytes,peer.port,peer.address));
    }
    for(const [name,type,host] of [["UDP_REFUSED","udp4","127.0.0.1"],["UDP6_REFUSED","udp6","::1"]]){
        const socket=dgram.createSocket(type);env[name]=String(await bind(socket,host));await new Promise(resolve=>socket.close(resolve));
    }
    const tcpReserve=net.createServer();env.PUBLISH_TCP=String(await listen(tcpReserve));await new Promise(r=>tcpReserve.close(r));
    const udpReserve=dgram.createSocket("udp4");env.PUBLISH_UDP=String(await bind(udpReserve,"127.0.0.1"));udpReserve.close();
    const client=dgram.createSocket("udp4");resources.add(client);await bind(client,"127.0.0.1");
    client.on("message",(data,peer)=>{try{assert.equal(peer.port,Number(env.PUBLISH_UDP));assert.equal(data.toString(),"udp-reply");stats.udpIngress=true;udpControl?.end("ok");}catch(e){failure=e;}});
    await tcp("TCP_CONTROL",s=>s.once("data",b=>{
        if(/^icmp-[0-9]+$/.test(b.toString())){const count=Number(b.toString().slice(5));if((stats.icmpErrors??0)>=count)s.end("ok");else icmpWaiters.push([count,s]);}
        else if(b.toString()==="fin-wait"){if(stats.fin)s.end("ok");else finWaiter=s;}
        else if(b.toString()==="peer-ready"){peerReady=true;peerWaiter?.end("ok");s.end("ok");}
        else if(b.toString()==="peer-wait"){if(peerReady)s.end("ok");else peerWaiter=s;}
        else if(b.toString()==="tcp"){
            const c=net.connect(Number(env.PUBLISH_TCP),"127.0.0.1",()=>c.write("ingress"));sockets.add(c);c.on("close",()=>sockets.delete(c));c.on("error",e=>{failure=e;});
            let reply="";c.on("data",b=>reply+=b);c.on("end",()=>{try{assert.equal(reply,"tcp-reply");stats.tcpIngress=true;s.end("ok");}catch(e){failure=e;}});
        }else if(b.toString()==="udp"){udpControl=s;client.send(Buffer.from("udp-ingress"),Number(env.PUBLISH_UDP),"127.0.0.1");}
    }));
    const server=new utils.wsServer({host:"127.0.0.1",port:0});
    await new Promise((resolve,reject)=>{server.once("listening",resolve);server.once("error",reject);});
    const url=`ws://127.0.0.1:${server.address().port}`;
    server.on("connection",ws=>{
        const flows=new Map();let chain=Promise.resolve();
        const send=(...parts)=>{if(ws.readyState===1)ws.send(Buffer.concat(parts));};
        const complete=(request,error)=>{if(request!==null)send(u32(0),u64(request),u32(error?1:0),...(error?[u32(error)]:[]));};
        const ext=(tag,...parts)=>send(u32(7),u32(1),u32(tag),...parts);
        const host=addr=>addr.v.includes(".")?"127.0.0.1":"::1";
        const accepted=(id,f)=>{if(f.children.length&&f.credits.length){const socket=f.children.shift(),child=f.credits.shift();flows.set(child,{socket});wire(child,socket);send(u32(5),u64(id),u64(child),address("127.0.0.1",socket.remotePort));stats.accepted++;socket.resume();}};
        const wire=(id,socket)=>{
            socket.on("data",data=>send(u32(1),u64(id),blob(data)));
            socket.on("end",()=>send(u32(6),u64(id)));
            // FIN and RST differ (Linux tcp_recvmsg_locked / tcp_reset), never infer EOF on error.
            socket.on("error",error=>{if(error.code==="ECONNRESET"){stats.reset++;ext(0,u64(id),u32(104));}else ext(0,u64(id),u32(103));});
        };
        ws.on("message",bytes=>{chain=chain.then(async()=>{
            const r=new Reader(bytes),tag=r.u32();const label=String(tag);stats.commands[label]=(stats.commands[label]??0)+1;
            if(tag===0){
                const op=r.u32();
                if(op===20){const name=r.blob().toString();assert(!r.bool());assert(!r.bool());const request=r.request();r.end();stats.dns++;assert.equal(name,"relay.test.");send(u32(0),u64(request),u32(5),u64(2),ip("192.0.2.1"),ip("2001:db8::1"));return;}
                const id=r.u64(),addr=r.addr();
                if(op===19){
                    const peer=r.addr(),request=r.request();r.end();stats.connect++;
                    if(profile==="disconnect"){complete(request);setTimeout(()=>ws.terminate(),20);return;}
                    if(profile==="overflow"){complete(request);for(let i=0;i<8;i++)send(u32(1),u64(id),blob(Buffer.alloc(1024*1024-20,7)));return;}
                    if(peer.port===9){
                        const child=spawn("python3",["-u",fileURLToPath(new URL("network-urgent.py",import.meta.url))],{stdio:["pipe","pipe","pipe"]});
                        helpers.add(child);const lines=createInterface({input:child.stdout});let ready=false;
                        child.stderr.on("data",data=>{failure??=new Error(data.toString().slice(0,2000));});
                        child.on("error",error=>{failure??=error;ws.terminate();});
                        child.on("exit",code=>{helpers.delete(child);if(code){failure??=new Error(`urgent helper exited ${code}`);ws.terminate();}});
                        flows.set(id,{urgent:child});
                        await new Promise((resolve,reject)=>{
                            child.once("error",reject);child.once("exit",code=>{if(!ready)reject(new Error(`urgent helper startup ${code}`));});
                            lines.on("line",line=>{try{
                                const event=JSON.parse(line);
                                if(event.ready){ready=true;complete(request);resolve();}
                                else if(event.error)throw new Error(event.error);
                                else if("sent" in event)send(u32(3),u64(id),u64(event.request),u64(event.sent));
                                else if(event.eof)send(u32(6),u64(id));
                                else {const bytes=Buffer.from(event.data,"base64");if(event.urgent){stats.urgent++;ext(3,u64(id),blob(bytes));}else send(u32(1),u64(id),blob(bytes));}
                            }catch(error){failure??=error;ws.terminate();}});
                        });return;
                    }
                    const socket=net.connect({host:host(peer),port:peer.port,allowHalfOpen:true});sockets.add(socket);socket.on("close",()=>sockets.delete(socket));
                    flows.set(id,{socket});
                    await new Promise(resolve=>{const failed=()=>{complete(request,9);resolve();};socket.once("connect",()=>{socket.removeListener("error",failed);wire(id,socket);complete(request);resolve();});socket.once("error",failed);});
                }else if(op===17){
                    r.bool();r.bool();const request=r.request();r.end();const socket=dgram.createSocket(addr.v.includes(".")?"udp4":"udp6");resources.add(socket);const flow={socket,udp:true,bound:false};flows.set(id,flow);
                    socket.on("error",error=>{
                        if(!flow.bound)return;
                        if(error.code==="ECONNREFUSED"&&flow.quote){
                            const q=flow.quote;flow.quote=null;socket.disconnect();stats.icmpErrors=(stats.icmpErrors??0)+1;
                            ext(4,u64(id),address(q.to.v,q.to.port),Buffer.from([1]),address(q.to.v,0),blob(q.data));
                            for(let i=icmpWaiters.length-1;i>=0;i--)if(icmpWaiters[i][0]<=stats.icmpErrors){icmpWaiters[i][1].end("ok");icmpWaiters.splice(i,1);}
                        }else {failure??=error;ws.terminate();}
                    });
                    socket.on("message",(bytes,peer)=>{stats.udp++;if(!bytes.length)stats.emptyUdp++;send(u32(2),u64(id),blob(bytes),address(peer.family==="IPv6"?"2001:db8::1":"192.0.2.1",peer.port));});
                    await new Promise(resolve=>{const failed=()=>{complete(request,4);resolve();};socket.once("error",failed);socket.bind(addr.port,host(addr),()=>{socket.removeListener("error",failed);flow.bound=true;if(profile==="ephemeral")env.PUBLISH_UDP=String(socket.address().port);complete(request);resolve();});});
                }else if(op===15){
                    r.bool();r.bool();r.bool();const request=r.request();r.end();
                    const f={credits:[],children:[]},listener=net.createServer({allowHalfOpen:true,pauseOnConnect:true},socket=>{sockets.add(socket);socket.on("close",()=>sockets.delete(socket));socket.on("error",()=>{});f.children.push(socket);accepted(id,f);});
                    flows.set(id,{...f,listener});const entry=flows.get(id);
                    // Share the same queues with the callback.
                    entry.credits=f.credits;entry.children=f.children;
                    servers.push(listener);await new Promise(resolve=>{listener.once("error",()=>{complete(request,4);resolve();});listener.listen(addr.port,host(addr),()=>{if(profile==="ephemeral")env.PUBLISH_TCP=String(listener.address().port);complete(request);resolve();});});
                }else throw new Error(`unsupported interface ${op}`);
            }else if(tag===2||tag===4){
                let id,data,request,urgent=false;
                if(tag===4){assert.equal(r.u32(),1);const op=r.u32();request=r.u64();
                    if(op===1){r.ip();r.end();stats.reverse++;ext(2,u64(request),u32(30),u32(0),blob(Buffer.from("relay.test")));return;}
                    if(op===2){const query=Buffer.from(r.blob());r.end();stats.forward++;query[2]=0x81;query[3]=0x80;query.writeUInt16BE(1,6);const txt=Buffer.from([5,...Buffer.from("relay")]);const rr=Buffer.from([0xc0,0x0c,0,16,0,1,0,0,0,30,0,txt.length,...txt]);ext(2,u64(request),u32(30),u32(1),blob(Buffer.concat([query,rr])));return;}
                    assert.equal(op,0);id=r.u64();data=r.blob();urgent=true;
                }else {id=r.u64();data=r.blob();request=r.request();}r.end();
                const f=flows.get(id);assert(f);
                if(f.urgent){f.urgent.stdin.write(JSON.stringify({request:String(request),data:data.toString("base64"),urgent})+"\n");return;}
                assert(!urgent,"host OOB forwarding requires the documented native provider");
                stats.maxBuffered=Math.max(stats.maxBuffered,f.socket.writableLength+data.length);
                const ack=()=>send(u32(3),u64(id),u64(request),u64(data.length));
                if(f.socket.write(data))ack();else f.socket.once("drain",ack);
            }else if(tag===3){
                const id=r.u64(),data=r.blob(),to=r.addr();r.request();r.end();const f=flows.get(id);assert(f?.udp);
                if(to.port===Number(to.v.includes(".")?env.UDP_REFUSED:env.UDP6_REFUSED)){
                    // Real Linux connected UDP socket, not a fabricated error endpoint.
                    // udp_err/__udp4_lib_err supplies ECONNREFUSED after the accepted send.
                    f.quote={to,data:Buffer.from(data)};
                    await new Promise((resolve,reject)=>{f.socket.connect(to.port,host(to),()=>{f.socket.send(data,error=>error?reject(error):resolve());});});return;
                }
                const destination=to.v==="198.51.100.1"?"255.255.255.255":host(to);
                f.socket.send(data,to.port,destination,error=>{if(error){stats.sendErrors++;if(profile!=="classic")ext(1,u64(id),u32(error.code==="EACCES"?13:5));}});
            }else if(tag===1){
                const id=r.u64(),op=r.u32(),f=flows.get(id);
                if(op===24){const child=r.u64();r.request();r.end();assert(f?.listener);f.credits.push(child);accepted(id,f);}
                else if(op===25){const request=r.request();r.end();assert(f);const a=f.listener?f.listener.address():f.socket.address();send(u32(0),u64(request),u32(8),address(a.address,a.port));stats.boundAddresses=(stats.boundAddresses??0)+1;}
                else if(op===44){const how=r.u32(),request=r.request();r.end();if(f?.socket&&!f.udp&&how!==0)f.socket.end();complete(request);}
                else if(op===30){if(r.bool())r.take(12);r.request();r.end();if(f?.socket&&!f.udp)f.socket.resetAndDestroy();}
                else if(op===21){r.request();r.end();if(f?.listener){for(const c of f.children)c.destroy();f.listener.close();}else if(f?.udp){try{f.socket.close();}catch{}}else if(f?.urgent)f.urgent.stdin.end();else f?.socket?.end();flows.delete(id);}
                else throw new Error(`unsupported socket ${op}`);
            }else throw new Error(`unsupported record ${tag}`);
        }).catch(error=>{failure??=error;ws.terminate();});});
    });
    // Refusal is a real closed local websocket endpoint, distinct from peer ECONNREFUSED.
    let endpoint=url;if(profile==="refused"){const s=net.createServer();const port=await listen(s);await new Promise(r=>s.close(r));endpoint=`ws://127.0.0.1:${port}`;}
    return {url:endpoint,env:["ingress","peer","ephemeral"].includes(profile)?env:Object.fromEntries(Object.entries(env).filter(([key])=>!key.startsWith("PUBLISH_"))),stats,
        async close(){
            for(const ws of server.clients)ws.terminate();
            const retired=[...helpers].map(child=>new Promise(resolve=>{child.once("exit",resolve);child.stdin.end();}));
            for(const socket of sockets)socket.destroy();
            for(const socket of resources)try{socket.close();}catch{}
            await Promise.all(servers.map(s=>s.listening?new Promise(r=>s.close(r)):Promise.resolve()));
            await new Promise(r=>server.close(r));
            await Promise.all(retired);
            assert(stats.maxBuffered<=256*1024,"relay send queue exceeded bound");
            if(failure)throw failure;
        }
    };
}
