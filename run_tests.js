#!/usr/bin/env node
"use strict";

const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const assert = require("node:assert/strict");

const ROOT = __dirname;
const DEFAULT_TIMEOUT_MS = 300000;

const ALL_SUITES = ["hash-small", "hash-full", "miner-integration"];
const SAFE_SUITES = ["hash-small"];
let colorEnabled = shouldColor();
const COLORS = {
    blue: "\x1b[34m",
    bold: "\x1b[1m",
    dim: "\x1b[90m",
    green: "\x1b[32m",
    red: "\x1b[31m",
    reset: "\x1b[0m"
};

function shouldColor() {
    if (process.env.FORCE_COLOR && process.env.FORCE_COLOR !== "0") {
        return true;
    }

    if (process.env.NO_COLOR) {
        return false;
    }

    return Boolean(process.stdout.isTTY);
}

function color(name, text) {
    if (!colorEnabled) {
        return text;
    }

    return `${COLORS[name]}${text}${COLORS.reset}`;
}

function parseArgs(argv) {
    const options = {
        build: true,
        buildDir: path.join(ROOT, "build"),
        binary: null,
        cmakeArgs: [],
        cmakeGenerator: process.env.CMAKE_GENERATOR || "",
        color: null,
        hashBinary: null,
        skipFullVectors: false,
        suites: [],
        timeoutMs: DEFAULT_TIMEOUT_MS,
        verbose: false
    };

    for (let i = 0; i < argv.length; ++i) {
        const arg = argv[i];
        const value = () => {
            if (i + 1 >= argv.length) {
                throw new Error(`missing value for ${arg}`);
            }
            return argv[++i];
        };

        if (arg === "--suite") {
            addSuite(options, value());
        }
        else if (arg.startsWith("--suite=")) {
            addSuite(options, arg.slice("--suite=".length));
        }
        else if (arg === "--safe" || arg === "--ci") {
            addSuites(options, SAFE_SUITES);
        }
        else if (arg === "--no-full-vectors" || arg === "--skip-full" || arg === "--no-full") {
            options.skipFullVectors = true;
        }
        else if (arg === "--hash-binary") {
            options.hashBinary = path.resolve(value());
        }
        else if (arg.startsWith("--hash-binary=")) {
            options.hashBinary = path.resolve(arg.slice("--hash-binary=".length));
        }
        else if (arg === "--binary") {
            options.binary = path.resolve(value());
        }
        else if (arg.startsWith("--binary=")) {
            options.binary = path.resolve(arg.slice("--binary=".length));
        }
        else if (arg === "--build-dir") {
            options.buildDir = path.resolve(value());
        }
        else if (arg.startsWith("--build-dir=")) {
            options.buildDir = path.resolve(arg.slice("--build-dir=".length));
        }
        else if (arg === "--skip-build") {
            options.build = false;
        }
        else if (arg === "--cmake-generator") {
            options.cmakeGenerator = value();
        }
        else if (arg.startsWith("--cmake-generator=")) {
            options.cmakeGenerator = arg.slice("--cmake-generator=".length);
        }
        else if (arg === "--cmake-arg") {
            options.cmakeArgs.push(value());
        }
        else if (arg.startsWith("--cmake-arg=")) {
            options.cmakeArgs.push(arg.slice("--cmake-arg=".length));
        }
        else if (arg === "--timeout-ms") {
            options.timeoutMs = Number(value());
        }
        else if (arg.startsWith("--timeout-ms=")) {
            options.timeoutMs = Number(arg.slice("--timeout-ms=".length));
        }
        else if (arg === "--verbose") {
            options.verbose = true;
        }
        else if (arg === "--color") {
            options.color = true;
        }
        else if (arg === "--no-color") {
            options.color = false;
        }
        else if (arg === "--help" || arg === "-h") {
            printHelp();
            process.exit(0);
        }
        else {
            throw new Error(`unknown argument: ${arg}`);
        }
    }

    if (!options.suites.length) {
        options.suites.push(...ALL_SUITES);
    }

    if (options.skipFullVectors) {
        options.suites = options.suites.filter(suite => suite !== "hash-full");
    }

    if (!options.suites.length) {
        throw new Error("no suites selected");
    }

    if (!Number.isFinite(options.timeoutMs) || options.timeoutMs <= 0) {
        throw new Error("--timeout-ms must be a positive number");
    }

    return options;
}

function addSuite(options, suite) {
    const normalized = normalizeSuiteName(suite);

    if (normalized === "all") {
        addSuites(options, ALL_SUITES);
        return;
    }

    if (normalized === "safe") {
        addSuites(options, SAFE_SUITES);
        return;
    }

    if (!ALL_SUITES.includes(normalized)) {
        throw new Error(`unknown suite: ${suite}`);
    }

    if (!options.suites.includes(normalized)) {
        options.suites.push(normalized);
    }
}

function addSuites(options, suites) {
    for (const suite of suites) {
        addSuite(options, suite);
    }
}

function normalizeSuiteName(suite) {
    switch (suite) {
    case "small":
    case "hash":
    case "hash-small":
        return "hash-small";
    case "full":
    case "hash-full":
        return "hash-full";
    case "integration":
    case "miner":
    case "miner-integration":
        return "miner-integration";
    case "safe":
    case "ci":
    case "quick":
        return "safe";
    case "all":
        return "all";
    default:
        return suite;
    }
}

function printHelp() {
    console.log(`Usage: node run_tests.js [options]

Builds the required test binaries when needed, then runs selected suites.
Child test output is captured and shown only on failure unless --verbose is used.

Options:
  --suite NAME              suite to run: hash-small, hash-full,
                            miner-integration, safe, or all
                            default: all
  --safe, --ci              run only the GitHub-safe quick subset
  --no-full-vectors         exclude full RandomX dataset vector checks
                            aliases: --skip-full, --no-full
  --binary PATH             use an existing xmrig binary for miner-integration
  --hash-binary PATH        use an existing hash-tests binary
  --build-dir PATH          CMake build directory, default: build
  --skip-build              do not configure or build test binaries
  --cmake-generator NAME    CMake generator used when configuring hash-tests
  --cmake-arg ARG           extra CMake configure argument, repeatable
  --timeout-ms N            per-case timeout, default: ${DEFAULT_TIMEOUT_MS}
  --verbose                 print child test stdout/stderr while tests run
  --color, --no-color       force ANSI color output on or off
`);
}

function run(command, args, options = {}) {
    console.log(`$ ${[command].concat(args).join(" ")}`);
    const result = spawnSync(command, args, {
        cwd: options.cwd || ROOT,
        env: options.env || process.env,
        stdio: "inherit",
        windowsHide: true
    });

    if (result.error) {
        throw result.error;
    }

    if (result.status !== 0) {
        throw new Error(`${command} exited with status ${result.status}`);
    }
}

function hashBuildDir(options) {
    return path.join(options.buildDir, "hash-tests");
}

function configureHashArgs(options) {
    const args = [
        "-S", path.join(ROOT, "tests", "hash"),
        "-B", hashBuildDir(options),
        `-DCMAKE_BUILD_TYPE=${process.env.CMAKE_BUILD_TYPE || "Release"}`
    ];

    if (options.cmakeGenerator) {
        args.push("-G", options.cmakeGenerator);
    }

    return args.concat(options.cmakeArgs);
}

function findHashBinary(buildDir) {
    const exe = process.platform === "win32" ? ".exe" : "";
    const names = [`hash-tests${exe}`, "hash-tests"];
    const dirs = [
        buildDir,
        path.join(buildDir, "Release"),
        path.join(buildDir, "RelWithDebInfo"),
        path.join(buildDir, "Debug")
    ];

    for (const dir of dirs) {
        for (const name of names) {
            const file = path.join(dir, name);
            if (fs.existsSync(file)) {
                return file;
            }
        }
    }

    throw new Error(`hash-tests binary not found under ${buildDir}`);
}

function findXmrigBinary(buildDir) {
    const exe = process.platform === "win32" ? ".exe" : "";
    const names = [`xmrig${exe}`, "xmrig"];
    const dirs = [
        buildDir,
        path.join(buildDir, "Release"),
        path.join(buildDir, "RelWithDebInfo"),
        path.join(buildDir, "Debug")
    ];

    for (const dir of dirs) {
        for (const name of names) {
            const file = path.join(dir, name);
            if (fs.existsSync(file)) {
                return file;
            }
        }
    }

    throw new Error(`xmrig binary not found under ${buildDir}`);
}

function buildHashTests(options) {
    if (options.hashBinary) {
        if (!fs.existsSync(options.hashBinary)) {
            throw new Error(`hash-tests binary not found: ${options.hashBinary}`);
        }
        return options.hashBinary;
    }

    const buildDir = hashBuildDir(options);

    if (options.build) {
        if (!fs.existsSync(path.join(buildDir, "CMakeCache.txt"))) {
            run("cmake", configureHashArgs(options));
        }

        const parallel = process.env.BUILD_PARALLEL || String(Math.max(1, os.cpus().length));
        run("cmake", ["--build", buildDir, "--target", "hash-tests", "--config", "Release", "--parallel", parallel]);
    }

    return findHashBinary(buildDir);
}

function configureRootArgs(options) {
    const args = ["-S", ROOT, "-B", options.buildDir, `-DCMAKE_BUILD_TYPE=${process.env.CMAKE_BUILD_TYPE || "Release"}`];

    if (options.cmakeGenerator) {
        args.push("-G", options.cmakeGenerator);
    }

    if (process.env.XMRIG_DEPS) {
        args.push(`-DXMRIG_DEPS=${process.env.XMRIG_DEPS}`);
    }

    if (process.env.OPENSSL_ROOT_DIR) {
        args.push(`-DOPENSSL_ROOT_DIR=${process.env.OPENSSL_ROOT_DIR}`);
    }

    return args.concat(options.cmakeArgs);
}

function buildXmrig(options) {
    if (options.binary) {
        if (!fs.existsSync(options.binary)) {
            throw new Error(`xmrig binary not found: ${options.binary}`);
        }
        return options.binary;
    }

    if (options.build) {
        if (!fs.existsSync(path.join(options.buildDir, "CMakeCache.txt"))) {
            run("cmake", configureRootArgs(options));
        }

        const parallel = process.env.BUILD_PARALLEL || String(Math.max(1, os.cpus().length));
        run("cmake", ["--build", options.buildDir, "--target", "xmrig", "--config", "Release", "--parallel", parallel]);
    }

    return findXmrigBinary(options.buildDir);
}

function suiteNeedsHash(options) {
    return options.suites.some(suite => suite.startsWith("hash-"));
}

function suiteNeedsMiner(options) {
    return options.suites.includes("miner-integration");
}

function setTestEnvironment(hashBinary, xmrigBinary, options) {
    process.env.XMRIG_TEST_TIMEOUT_MS = String(options.timeoutMs);
    process.env.XMRIG_TEST_VERBOSE = options.verbose ? "1" : "0";

    if (hashBinary) {
        process.env.XMRIG_HASH_TEST_BINARY = hashBinary;
        console.log(`testing ${hashBinary}`);
    }

    if (xmrigBinary) {
        process.env.XMRIG_TEST_BINARY = xmrigBinary;
        console.log(`testing ${xmrigBinary}`);
    }
}

function createSuite(name) {
    if (name === "hash-small" || name === "hash-full") {
        const suite = name === "hash-small" ? "small" : "full";
        const title = name === "hash-small" ? "small hash vectors" : "full RandomX dataset vectors";
        const { getTestConfig, listHashTests, runHashCase } = require("./tests/common/hash_harness.js");
        const config = getTestConfig();

        return {
            title,
            cases: listHashTests(suite),
            runCase: testName => runHashCase(suite, testName, config.timeoutMs)
        };
    }

    if (name === "miner-integration") {
        const { MINER_ALGOS, withMiner } = require("./tests/common/miner_harness.js");

        return {
            title: "xmrig binary fake pool",
            cases: MINER_ALGOS,
            runCase: algo => withMiner(algo, async ({ pool }) => {
                assert.ok(pool.submits.length >= 1, `${algo} should submit at least one share before shutdown`);
            })
        };
    }

    throw new Error(`unknown suite: ${name}`);
}

function durationMs(started) {
    const ms = Number(process.hrtime.bigint() - started) / 1000000;

    return ms.toFixed(3).replace(/\.?0+$/, "");
}

function formatDuration(started) {
    return color("dim", `${durationMs(started)}ms`);
}

function formatError(error) {
    if (!error) {
        return "";
    }

    return error.stack || error.message || String(error);
}

class Runner {
    constructor() {
        this.total = 0;
        this.passed = 0;
        this.failed = 0;
        this.started = process.hrtime.bigint();
        this.hasOutput = false;
    }

    async runSuite(suite) {
        if (!suite.cases.length) {
            throw new Error(`${suite.title} has no test cases`);
        }

        if (this.hasOutput) {
            console.log("");
        }

        this.hasOutput = true;
        console.log(`${color("bold", "▶")} ${color("bold", suite.title)}`);
        const started = process.hrtime.bigint();

        for (const name of suite.cases) {
            await this.runCase(suite, name);
        }

        console.log(`${color("bold", "▶")} ${color("bold", suite.title)} (${formatDuration(started)})`);
    }

    async runCase(suite, name) {
        ++this.total;
        const started = process.hrtime.bigint();

        try {
            await suite.runCase(name);
            ++this.passed;
            console.log(`  ${color("green", "✔")} ${name} (${formatDuration(started)})`);
        }
        catch (error) {
            ++this.failed;
            console.log(`  ${color("red", "✖")} ${name} (${formatDuration(started)})`);
            console.error(formatError(error));
        }
    }

    printSummary() {
        const info = color("blue", "ℹ");

        console.log(`${info} tests ${this.total}`);
        console.log(`${info} suites ${this.suiteCount}`);
        console.log(`${info} pass ${this.passed}`);
        console.log(`${info} fail ${this.failed}`);
        console.log(`${info} cancelled 0`);
        console.log(`${info} skipped 0`);
        console.log(`${info} todo 0`);
        console.log(`${info} duration_ms ${color("dim", durationMs(this.started))}`);

        return this.failed === 0;
    }
}

async function main() {
    const options = parseArgs(process.argv.slice(2));
    if (options.color !== null) {
        colorEnabled = options.color;
    }

    const hashBinary = suiteNeedsHash(options) ? buildHashTests(options) : null;
    const xmrigBinary = suiteNeedsMiner(options) ? buildXmrig(options) : null;

    setTestEnvironment(hashBinary, xmrigBinary, options);

    const runner = new Runner();
    runner.suiteCount = options.suites.length;

    for (const name of options.suites) {
        await runner.runSuite(createSuite(name));
    }

    if (!runner.printSummary()) {
        process.exit(1);
    }
}

main().catch(error => {
    console.error(error.stack || error.message || String(error));
    process.exit(1);
});
