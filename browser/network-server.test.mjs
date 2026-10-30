import test from "node:test";
import assert from "node:assert/strict";
import { once } from "node:events";
import utils from "./node_modules/playwright-core/lib/utilsBundle.js";
import { serveNetwork } from "./network-server.mjs";
const u32=v=>{const b=Buffer.alloc(4);b.writeUInt32LE(v);return b;};
const u64=v=>{const b=Buffer.alloc(8);b.writeBigUInt64LE(BigInt(v));return b;};
const addr=(v,p)=>{const port=Buffer.alloc(2);port.writeUInt16LE(p);return Buffer.concat([u32(0),Buffer.from(v),port]);};
const option=Buffer.concat([Buffer.from([1]),u64(7)]);
const wait=(ws)=>once(ws,"message").then(([b])=>b);

test("local fidelity relay preserves empty UDP and actual send errors",async()=>{
    const relay=await serveNetwork();const ws=new utils.ws(relay.url);await once(ws,"open");
    try{
        let next=wait(ws);ws.send(Buffer.concat([u32(0),u32(17),u64(9),addr([0,0,0,0],0),Buffer.from([0,0]),option]));assert.equal((await next).readUInt32LE(12),0);
        next=wait(ws);ws.send(Buffer.concat([u32(3),u64(9),u64(0),addr([192,0,2,1],Number(relay.env.UDP_ECHO)),option]));const packet=await next;assert.equal(packet.readUInt32LE(0),2);assert.equal(packet.readBigUInt64LE(12),0n);
        next=wait(ws);ws.send(Buffer.concat([u32(3),u64(9),u64(1),Buffer.from("x"),addr([198,51,100,1],9),option]));const error=await next;assert.equal(error.readUInt32LE(8),1);assert.equal(error.readUInt32LE(20),13);
        assert.equal(relay.stats.emptyUdp,1);assert.equal(relay.stats.sendErrors,1);
    }finally{ws.terminate();await relay.close();}
});

test("local relay differentiates peer FIN and reset",async()=>{
    const relay=await serveNetwork();const ws=new utils.ws(relay.url);await once(ws,"open");
    const events=[];const waiters=[];ws.on("message",b=>{events.push(b);for(const w of waiters.splice(0))w();});
    const event=async(predicate)=>{for(;;){const i=events.findIndex(predicate);if(i>=0)return events.splice(i,1)[0];await new Promise(r=>waiters.push(r));}};
    const connect=async(id,key)=>{ws.send(Buffer.concat([u32(0),u32(19),u64(id),addr([0,0,0,0],0),addr([192,0,2,1],Number(relay.env[key])),option]));await event(b=>b.readUInt32LE(0)===0);};
    try{
        await connect(9,"TCP_FIN");await event(b=>b.readUInt32LE(0)===6);
        ws.send(Buffer.concat([u32(2),u64(9),u64(9),Buffer.from("after-fin"),option]));await event(b=>b.readUInt32LE(0)===3);
        ws.send(Buffer.concat([u32(1),u64(9),u32(44),u32(1),option]));await event(b=>b.readUInt32LE(0)===0);
        await connect(10,"TCP_RESET");ws.send(Buffer.concat([u32(2),u64(10),u64(5),Buffer.from("reset"),option]));const reset=await event(b=>b.readUInt32LE(0)===7);assert.equal(reset.readUInt32LE(8),0);assert.equal(reset.readUInt32LE(20),104);
    }finally{ws.terminate();await relay.close();}
});

test("fixed TCP publication obeys accept credit and carries both directions",async()=>{
    const relay=await serveNetwork("ingress");const ws=new utils.ws(relay.url);await once(ws,"open");
    const events=[],waiters=[];ws.on("message",b=>{events.push(b);for(const w of waiters.splice(0))w();});
    const event=async(predicate)=>{for(;;){const i=events.findIndex(predicate);if(i>=0)return events.splice(i,1)[0];await new Promise(r=>waiters.push(r));}};
    const {default:net}=await import("node:net");let client;
    try{
        ws.send(Buffer.concat([u32(0),u32(15),u64(9),addr([127,0,0,1],Number(relay.env.PUBLISH_TCP)),Buffer.from([0,0,0]),option]));await event(b=>b.readUInt32LE(0)===0);
        ws.send(Buffer.concat([u32(1),u64(9),u32(24),u64(10),Buffer.from([0])]));
        client=net.connect(Number(relay.env.PUBLISH_TCP),"127.0.0.1");await once(client,"connect");await event(b=>b.readUInt32LE(0)===5);
        client.write("hi");const packet=await event(b=>b.readUInt32LE(0)===1);assert.equal(packet.subarray(20).toString(),"hi");
        const reply=once(client,"data");ws.send(Buffer.concat([u32(2),u64(10),u64(2),Buffer.from("ok"),option]));assert.equal((await reply)[0].toString(),"ok");
    }finally{client?.destroy();ws.terminate();await relay.close();}
});

test("urgent relay traverses native MSG_OOB sockets and preserves the mark",async()=>{
    const relay=await serveNetwork();const ws=new utils.ws(relay.url);await once(ws,"open");
    const events=[],waiters=[];ws.on("message",b=>{events.push(b);for(const w of waiters.splice(0))w();});
    const event=async(predicate)=>{for(;;){const i=events.findIndex(predicate);if(i>=0)return events.splice(i,1)[0];await new Promise(r=>waiters.push(r));}};
    try{
        ws.send(Buffer.concat([u32(0),u32(19),u64(9),addr([0,0,0,0],0),addr([192,0,2,1],9),option]));await event(b=>b.readUInt32LE(0)===0);
        ws.send(Buffer.concat([u32(2),u64(9),u64(2),Buffer.from("ab"),option]));await event(b=>b.readUInt32LE(0)===3);
        ws.send(Buffer.concat([u32(4),u32(1),u32(0),u64(8),u64(9),u64(1),Buffer.from("!")]));await event(b=>b.readUInt32LE(0)===3);
        ws.send(Buffer.concat([u32(2),u64(9),u64(2),Buffer.from("cd"),option]));await event(b=>b.readUInt32LE(0)===3);
        let text="";const marks=[];
        while(text.length<5){const data=await event(b=>[1,7].includes(b.readUInt32LE(0)));const urgent=data.readUInt32LE(0)===7;const bytes=data.subarray(urgent?28:20);if(urgent)marks.push(text.length+bytes.length-1);text+=bytes;}
        assert.equal(text,"ab!cd");assert.deepEqual(marks,[2]);assert.equal(relay.stats.urgent,1);
    }finally{ws.terminate();await relay.close();}
});

test("port-zero TCP and UDP bindings return their actual classic local address",{timeout:5000},async()=>{
    const relay=await serveNetwork("ephemeral"),ws=new utils.ws(relay.url);await once(ws,"open");
    try{
        for(const [id,op,flags] of [[9,15,[0,0,0]],[10,17,[0,0]]]){
            let next=wait(ws);ws.send(Buffer.concat([u32(0),u32(op),u64(id),addr([127,0,0,1],0),Buffer.from(flags),option]));assert.equal((await next).readUInt32LE(12),0);
            next=wait(ws);ws.send(Buffer.concat([u32(1),u64(id),u32(25),option]));const response=await next;
            assert.equal(response.readUInt32LE(12),8);assert.equal(response.readUInt32LE(16),0);
            assert.equal(response.readUInt16LE(24),Number(relay.env[id===9?"PUBLISH_TCP":"PUBLISH_UDP"]));assert.notEqual(response.readUInt16LE(24),0);
        }
        assert.equal(relay.stats.boundAddresses,2);
    }finally{ws.terminate();await relay.close();}
});

test("native connected UDP reports actual ICMP refusal with destination and quote",{timeout:5000},async()=>{
    const relay=await serveNetwork(),ws=new utils.ws(relay.url);await once(ws,"open");
    try{
        let next=wait(ws);ws.send(Buffer.concat([u32(0),u32(17),u64(9),addr([0,0,0,0],0),Buffer.from([0,0]),option]));await next;
        next=wait(ws);ws.send(Buffer.concat([u32(3),u64(9),u64(5),Buffer.from("quote"),addr([192,0,2,1],Number(relay.env.UDP_REFUSED)),option]));const error=await next;
        assert.equal(error.readUInt32LE(0),7);assert.equal(error.readUInt32LE(8),4);assert.equal(error.readBigUInt64LE(12),9n);
        assert.equal(error.readUInt16LE(28),Number(relay.env.UDP_REFUSED));assert.equal(error.subarray(-5).toString(),"quote");assert.equal(relay.stats.icmpErrors,1);
    }finally{ws.terminate();await relay.close();}
});
