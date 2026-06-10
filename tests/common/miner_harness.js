"use strict";

const assert = require("node:assert/strict");
const { spawn, spawnSync } = require("node:child_process");
const { EventEmitter } = require("node:events");
const fs = require("node:fs");
const net = require("node:net");
const os = require("node:os");
const path = require("node:path");

const ROOT = path.resolve(__dirname, "..", "..");
const DEFAULT_TIMEOUT_MS = 120000;
const BASE_BLOB = "070780e6b9d60586ba419a0c224e3c6c3e134cc45c4fa04d8ee2d91c2595463c57eef0a4f0796c000000002fcc4d62fa6c77e76c30017c768be5c61d83ec9d3a085d524ba8053ecc3224660d";
const ZERO_HASH = "00".repeat(32);

const STANDARD_ALGOS = [
    "cn/0",
    "cn/1",
    "cn/2",
    "cn/fast",
    "cn/xao",
    "cn/rto",
    "cn/half",
    "cn/r",
    "cn/rwz",
    "cn/zls",
    "cn/ccx",
    "cn/double",
    "cn/gpu",
    "cn-lite/0",
    "cn-lite/1",
    "cn-heavy/0",
    "cn-heavy/xhv",
    "cn-heavy/tube",
    "cn-pico",
    "cn-pico/tlo",
    "cn/upx2",
    "argon2/chukwa",
    "argon2/chukwav2",
    "argon2/ninja",
    "rx/0",
    "rx/2",
    "rx/wow",
    "rx/arq",
    "rx/graft",
    "rx/sfx",
    "rx/yada",
    "panthera"
];

const ETH_STYLE_ALGOS = [
    "ghostrider",
    "flex"
];

const MINER_ALGOS = STANDARD_ALGOS.concat(ETH_STYLE_ALGOS);

const ALGO_PERF = {
    "cn/r": 1,
    "cn-lite/1": 1,
    "cn-pico": 1,
    "cn/ccx": 1,
    "cn/gpu": 1,
    "argon2/chukwav2": 1,
    "kawpow1": 1,
    "ghostrider": 1,
    "flex": 1,
    "cn-heavy/xhv": 1,
    "rx/0": 1,
    "rx/graft": 1,
    "rx/arq": 1,
    "panthera": 1
};

function getMinerTestConfig() {
    const binary = process.env.XMRIG_TEST_BINARY || path.join(ROOT, "build", process.platform === "win32" ? "xmrig.exe" : "xmrig");
    const timeoutMs = Number(process.env.XMRIG_TEST_TIMEOUT_MS || DEFAULT_TIMEOUT_MS);

    assert.ok(Number.isFinite(timeoutMs) && timeoutMs > 0, "XMRIG_TEST_TIMEOUT_MS must be a positive number");

    return {
        binary,
        timeoutMs,
        verbose: process.env.XMRIG_TEST_VERBOSE === "1"
    };
}

function delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function waitFor(predicate, timeoutMs, description) {
    const started = Date.now();

    while (Date.now() - started < timeoutMs) {
        if (predicate()) {
            return;
        }

        await delay(25);
    }

    throw new Error(`timed out waiting for ${description}`);
}

class JsonPeer extends EventEmitter {
    constructor(socket, name) {
        super();
        this.socket = socket;
        this.name = name;
        this.buffer = "";
        this.messages = [];
        this.closed = false;

        socket.setEncoding("utf8");
        socket.on("data", data => this.onData(data));
        socket.on("error", error => this.emit("peer-error", error));
        socket.on("close", () => {
            this.closed = true;
            this.emit("closed");
        });
    }

    onData(data) {
        this.buffer += data;

        while (true) {
            const index = this.buffer.indexOf("\n");
            if (index < 0) {
                return;
            }

            const line = this.buffer.slice(0, index).trim();
            this.buffer = this.buffer.slice(index + 1);
            if (!line) {
                continue;
            }

            let message;
            try {
                message = JSON.parse(line);
            }
            catch (error) {
                error.message = `${this.name}: failed to parse JSON line ${line}: ${error.message}`;
                this.emit("peer-error", error);
                continue;
            }

            this.messages.push(message);
            this.emit("message", message);
        }
    }

    send(message) {
        this.socket.write(`${JSON.stringify(message)}\n`);
    }

    close() {
        this.socket.destroy();
    }
}

class FakePool extends EventEmitter {
    constructor(algo, timeoutMs) {
        super();
        this.algo = algo;
        this.timeoutMs = timeoutMs;
        this.server = net.createServer(socket => this.onConnection(socket));
        this.connections = [];
        this.logins = [];
        this.subscribes = [];
        this.authorizes = [];
        this.submits = [];
        this.jobSeq = 0;
    }

    async start() {
        await new Promise((resolve, reject) => {
            this.server.once("error", reject);
            this.server.listen(0, "127.0.0.1", resolve);
        });

        this.port = this.server.address().port;
    }

    onConnection(socket) {
        const connection = {
            id: this.connections.length + 1,
            rpcId: `miner-${this.connections.length + 1}`,
            peer: new JsonPeer(socket, `fake pool ${this.connections.length + 1}`)
        };

        this.connections.push(connection);
        connection.peer.on("message", message => this.onMessage(connection, message));
    }

    onMessage(connection, message) {
        if (message.method === "login") {
            this.logins.push({ connection, message });
            connection.peer.send({
                id: message.id,
                jsonrpc: "2.0",
                error: null,
                result: {
                    id: connection.rpcId,
                    job: this.nextStandardJob(),
                    extensions: ["algo", "keepalive"]
                }
            });
            return;
        }

        if (message.method === "getjob") {
            connection.peer.send({
                id: message.id,
                jsonrpc: "2.0",
                error: null,
                result: Object.assign({
                    id: connection.rpcId,
                    extensions: ["algo", "keepalive"]
                }, this.nextStandardJob())
            });
            return;
        }

        if (message.method === "submit") {
            this.submits.push({ connection, message });
            this.emit("submit");
            connection.peer.send({
                id: message.id,
                jsonrpc: "2.0",
                error: null,
                result: { status: "OK" }
            });
            return;
        }

        if (message.method === "keepalived") {
            connection.peer.send({
                id: message.id,
                jsonrpc: "2.0",
                error: null,
                result: { status: "KEEPALIVED" }
            });
            return;
        }

        if (message.method === "mining.subscribe") {
            this.subscribes.push({ connection, message });
            connection.peer.send({
                id: message.id,
                jsonrpc: "2.0",
                error: null,
                result: [[], "01", 4]
            });
            this.sendEthJob(connection);
            return;
        }

        if (message.method === "mining.authorize") {
            this.authorizes.push({ connection, message });
            connection.peer.send({
                id: message.id,
                jsonrpc: "2.0",
                error: null,
                result: true
            });
            this.sendEthJob(connection);
            return;
        }

        if (message.method === "mining.submit") {
            this.submits.push({ connection, message });
            this.emit("submit");
            connection.peer.send({
                id: message.id,
                jsonrpc: "2.0",
                error: null,
                result: true
            });
        }
    }

    nextStandardJob() {
        const seq = ++this.jobSeq;

        return {
            blob: blobForAlgo(this.algo, seq),
            job_id: `offline-job-${seq}`,
            target: "ffffffffffffffff",
            algo: this.algo,
            height: 1000 + seq,
            seed_hash: ZERO_HASH
        };
    }

    sendEthJob(connection) {
        if (!ETH_STYLE_ALGOS.includes(this.algo) || this.subscribes.length === 0 || this.authorizes.length === 0) {
            return;
        }

        const jobId = `offline-job-${++this.jobSeq}`;
        connection.peer.send({
            id: null,
            method: "mining.set_difficulty",
            params: [1 / 65536]
        });
        connection.peer.send({
            id: null,
            method: "mining.notify",
            params: [
                jobId,
                "00".repeat(32),
                "00".repeat(160),
                "",
                [],
                "01000000",
                "ffff001d",
                "00000000",
                true
            ]
        });
    }

    async waitForSubmits(count) {
        if (this.submits.length >= count) {
            return;
        }

        await new Promise((resolve, reject) => {
            const onSubmit = () => {
                if (this.submits.length < count) {
                    return;
                }

                cleanup();
                resolve();
            };
            const timer = setTimeout(() => {
                cleanup();
                reject(new Error(`timed out waiting for ${count} submit request(s) for ${this.algo}`));
            }, this.timeoutMs);
            const cleanup = () => {
                clearTimeout(timer);
                this.off("submit", onSubmit);
            };

            this.on("submit", onSubmit);
        });
    }

    async close() {
        for (const connection of this.connections) {
            connection.peer.close();
        }

        await new Promise(resolve => this.server.close(resolve));
    }
}

function blobForAlgo(algo, seq) {
    if (algo === "rx/yada") {
        const blob = Buffer.alloc(151);
        blob.writeUInt32LE(seq, 147);
        return blob.toString("hex");
    }

    const suffix = seq.toString(16).padStart(8, "0");
    return BASE_BLOB.slice(0, -8) + suffix;
}

function createMinerConfig(algo, port) {
    const config = {
        autosave: false,
        background: false,
        colors: false,
        title: false,
        watch: false,
        randomx: {
            init: 1,
            mode: "light",
            "1gb-pages": false,
            rdmsr: false,
            wrmsr: false,
            numa: false
        },
        cpu: {
            enabled: true,
            "huge-pages": false,
            "huge-pages-jit": false,
            "hw-aes": null,
            priority: null,
            "memory-pool": false,
            yield: true,
            "max-threads-hint": 1,
            asm: true
        },
        opencl: {
            enabled: false
        },
        cuda: {
            enabled: false
        },
        "donate-level": 0,
        "donate-over-proxy": 0,
        "log-file": null,
        pools: [
            {
                algo,
                coin: null,
                url: `127.0.0.1:${port}`,
                user: "offline-wallet",
                pass: "x",
                "rig-id": null,
                nicehash: false,
                keepalive: false,
                enabled: true,
                tls: false,
                "tls-fingerprint": null,
                daemon: false,
                socks5: null,
                "self-select": null,
                "submit-to-origin": false
            }
        ],
        "print-time": 60,
        "health-print-time": 60,
        retries: 1,
        "retry-pause": 1,
        syslog: false,
        "rebench-algo": false,
        "bench-algo-time": 1,
        "algo-min-time": 0,
        "algo-perf": ALGO_PERF,
        verbose: 0
    };

    config.cpu["*"] = [[1, -1]];
    config.cpu.ghostrider = [[8, -1]];

    return config;
}

function minerLogTail(child) {
    if (!child || !child.output) {
        return "";
    }

    return child.output.join("").split(/\r?\n/).slice(-120).join("\n");
}

function spawnMiner(binary, configFile, config) {
    const child = spawn(binary, ["--config", configFile, "--no-color"], {
        cwd: path.dirname(configFile),
        env: process.env,
        windowsHide: true
    });

    const output = [];
    const capture = stream => {
        stream.on("data", chunk => {
            const text = chunk.toString();
            output.push(text);
            if (config.verbose) {
                process.stdout.write(text);
            }
        });
    };

    capture(child.stdout);
    capture(child.stderr);

    child.output = output;
    child.exited = false;
    child.once("exit", (code, signal) => {
        child.exited = true;
        child.exitCodeValue = code;
        child.exitSignalValue = signal;
    });

    return child;
}

async function stopMiner(child) {
    if (!child || child.exited) {
        return;
    }

    child.kill("SIGTERM");

    for (let i = 0; i < 20; ++i) {
        if (child.exited) {
            return;
        }

        await delay(100);
    }

    if (process.platform === "win32") {
        spawnSync("taskkill", ["/pid", String(child.pid), "/T", "/F"], { stdio: "ignore" });
    }
    else {
        child.kill("SIGKILL");
    }
}

async function withMiner(algo, testFn) {
    const config = getMinerTestConfig();
    const pool = new FakePool(algo, config.timeoutMs);
    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "xmrig-miner-test-"));
    const configFile = path.join(tempDir, "config.json");
    let miner = null;

    await pool.start();
    fs.writeFileSync(configFile, `${JSON.stringify(createMinerConfig(algo, pool.port), null, 2)}\n`);

    try {
        miner = spawnMiner(config.binary, configFile, config);
        await waitFor(() => pool.submits.length > 0 || miner.exited, config.timeoutMs, `${algo} share submit`);

        if (miner.exited && pool.submits.length === 0) {
            throw new Error(`xmrig exited before submitting a ${algo} share`);
        }

        await pool.waitForSubmits(1);
        await testFn({ config, miner, pool });
    }
    catch (error) {
        const tail = minerLogTail(miner);
        if (tail) {
            console.error("\n--- xmrig log tail ---");
            console.error(tail);
            console.error("--- end xmrig log tail ---\n");
        }

        throw error;
    }
    finally {
        await stopMiner(miner);
        await pool.close();
        fs.rmSync(tempDir, { force: true, recursive: true });
    }
}

module.exports = {
    DEFAULT_TIMEOUT_MS,
    ETH_STYLE_ALGOS,
    MINER_ALGOS,
    STANDARD_ALGOS,
    getMinerTestConfig,
    withMiner
};
