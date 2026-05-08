"use strict";

const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const path = require("node:path");

const ROOT = path.resolve(__dirname, "..", "..");
const DEFAULT_TIMEOUT_MS = 300000;

function getTestConfig() {
    const binary = process.env.XMRIG_HASH_TEST_BINARY || path.join(ROOT, "build", "hash-tests", process.platform === "win32" ? "hash-tests.exe" : "hash-tests");
    const timeoutMs = Number(process.env.XMRIG_TEST_TIMEOUT_MS || DEFAULT_TIMEOUT_MS);

    assert.ok(Number.isFinite(timeoutMs) && timeoutMs > 0, "XMRIG_TEST_TIMEOUT_MS must be a positive number");

    return {
        binary,
        timeoutMs,
        verbose: process.env.XMRIG_TEST_VERBOSE === "1"
    };
}

function outputTail(result) {
    return [result.stdout, result.stderr]
        .filter(Boolean)
        .join("")
        .split(/\r?\n/)
        .slice(-80)
        .join("\n")
        .trim();
}

function runHashBinary(args, options = {}) {
    const config = getTestConfig();
    const result = spawnSync(config.binary, args, {
        cwd: ROOT,
        encoding: "utf8",
        timeout: options.timeoutMs || config.timeoutMs,
        windowsHide: true
    });

    if (config.verbose) {
        if (result.stdout) {
            process.stdout.write(result.stdout);
        }
        if (result.stderr) {
            process.stderr.write(result.stderr);
        }
    }

    if (result.error) {
        throw result.error;
    }

    if (result.signal) {
        throw new Error(`${path.basename(config.binary)} was terminated by ${result.signal}`);
    }

    if (result.status !== 0) {
        const tail = outputTail(result);
        throw new Error(`${path.basename(config.binary)} exited with status ${result.status}${tail ? `\n\n${tail}` : ""}`);
    }

    return result.stdout || "";
}

function listHashTests(suite) {
    return runHashBinary(["--suite", suite, "--list"])
        .split(/\r?\n/)
        .map(line => line.trim())
        .filter(Boolean);
}

function runHashCase(suite, name, timeoutMs) {
    runHashBinary(["--suite", suite, "--case", name], { timeoutMs });
}

module.exports = {
    DEFAULT_TIMEOUT_MS,
    getTestConfig,
    listHashTests,
    runHashCase
};
