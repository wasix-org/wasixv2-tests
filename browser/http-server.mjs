// Deterministic cross-origin guest HTTP peer; SDK/module acquisition uses the other server.
import { createServer } from "node:http";
export async function serveHttp() {
    const seen = new Map();
    const server = createServer(async (request, response) => {
        seen.set(request.url, (seen.get(request.url) ?? 0) + 1);
        if (request.url === "/reset") { request.socket.destroy(); return; }
        response.setHeader("Cross-Origin-Resource-Policy", "cross-origin");
        if (request.url !== "/cors-denied") response.setHeader("Access-Control-Allow-Origin", "*");
        if (request.url === "/slow") return; // Client timeout must abort, not synthesize a reply.
        if (request.url === "/binary") { response.writeHead(201).end(Buffer.from([0,128,255])); return; }
        if (request.url === "/status") { response.writeHead(500).end("server error"); return; }
        if (request.url === "/echo") {
            const chunks = []; for await (const chunk of request) chunks.push(chunk);
            response.end(Buffer.concat(chunks)); return;
        }
        if (request.url === "/cors-denied") { response.end("unreadable"); return; }
        response.writeHead(404).end();
    });
    await new Promise((resolve,reject)=>{server.once("error",reject);server.listen(0,"127.0.0.1",resolve);});
    return { url:`http://127.0.0.1:${server.address().port}`, seen,
        close:async()=>{server.closeAllConnections();await new Promise((resolve,reject)=>server.close(error=>error?reject(error):resolve()));} };
}
