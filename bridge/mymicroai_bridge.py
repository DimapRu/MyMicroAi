#!/usr/bin/env python3
"""MyMicroAI bridge: lets the device talk to Claude through a computer or server.

The device already speaks the OpenAI-compatible chat API, so it only needs its
Base URL pointed here (http://<bridge-ip>:8787/v1) and the bridge token as its
API key. The bridge answers with one of two backends:

  api          Claude API through the official Anthropic SDK (ANTHROPIC_API_KEY,
               or an `ant auth login` profile).
  claude-code  Claude Code in headless mode (`claude -p`), which runs on the
               Claude subscription that Claude Code is logged in with.

Run `python mymicroai_bridge.py --help` for options; see bridge/README.md.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import hmac
import json
import logging
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

DEFAULT_MODEL = "claude-opus-5-5"
MAX_BODY_BYTES = 12 * 1024 * 1024
API_MAX_TOKENS = 16000
FALLBACK_BETA = "server-side-fallback-2026-07-01"
# Models that accept the server-side refusal fallback ("fallbacks": "default").
DEFAULT_FALLBACK_MODELS = {"claude-fable-5-1", "claude-opus-5-5", "claude-opus-5", "claude-sonnet-5-5"}
# output_config.effort is rejected by older models (Sonnet 4.5, Haiku 4.5, Claude 3).
EFFORT_MODEL_PREFIXES = (
    "claude-opus-4-5", "claude-opus-4-6", "claude-opus-4-7", "claude-opus-4-8", "claude-opus-5",
    "claude-sonnet-4-6", "claude-sonnet-5", "claude-haiku-5", "claude-fable", "claude-mythos",
)
CLAUDE_CODE_MODELS = ["opus", "sonnet", "haiku", "fable"]
DATA_URL = re.compile(r"^data:(image/[a-zA-Z0-9.+-]+);base64,(.*)$", re.DOTALL)
PHOTO_LABEL = re.compile(r"^\s*([A-Za-z0-9_-]{1,32})\s*:\s*$")
IMAGE_EXTENSIONS = {"image/jpeg": "jpg", "image/png": "png", "image/webp": "webp", "image/gif": "gif"}

log = logging.getLogger("mymicroai-bridge")


class BridgeError(Exception):
    """An error answered to the device as an OpenAI-style error body."""

    def __init__(self, status: int, message: str):
        super().__init__(message)
        self.status = status
        self.message = message


# ---------------------------------------------------------------------------
# Request conversion (OpenAI chat format -> neutral turns)
# ---------------------------------------------------------------------------

def parse_image_url(url: str) -> dict[str, str]:
    match = DATA_URL.match(url)
    if match:
        media_type, data = match.group(1), match.group(2).strip()
        try:
            base64.b64decode(data, validate=True)
        except binascii.Error as error:
            raise BridgeError(400, f"Image is not valid base64: {error}") from error
        return {"kind": "base64", "media_type": media_type, "data": data}
    if url.startswith(("http://", "https://")):
        return {"kind": "url", "url": url}
    raise BridgeError(400, "Unsupported image_url (expected a data: or http(s) URL)")


def to_turns(openai_messages: list[dict[str, Any]]) -> tuple[str, list[dict[str, Any]]]:
    """Returns (system prompt, turns); each turn is {role, parts}, parts being
    {"type": "text", "text"} or {"type": "image", ...parse_image_url fields, "label"}."""
    system_parts: list[str] = []
    turns: list[dict[str, Any]] = []
    for message in openai_messages:
        role = message.get("role")
        content = message.get("content")
        if role in ("system", "developer"):
            if isinstance(content, str):
                system_parts.append(content)
            elif isinstance(content, list):
                system_parts.extend(p.get("text", "") for p in content if p.get("type") == "text")
            continue
        if role not in ("user", "assistant"):
            continue

        parts: list[dict[str, Any]] = []
        if isinstance(content, str):
            if content.strip():
                parts.append({"type": "text", "text": content})
        elif isinstance(content, list):
            pending_label = None
            for part in content:
                if part.get("type") == "text":
                    text = part.get("text", "")
                    label = PHOTO_LABEL.match(text)
                    if label:
                        # The device sends "photo1:" right before each image.
                        pending_label = label.group(1)
                    if text.strip():
                        parts.append({"type": "text", "text": text})
                elif part.get("type") == "image_url":
                    image_url = part.get("image_url") or {}
                    url = image_url.get("url", "") if isinstance(image_url, dict) else str(image_url)
                    image = parse_image_url(url)
                    image["type"] = "image"
                    image["label"] = pending_label
                    pending_label = None
                    parts.append(image)
        if parts:
            turns.append({"role": role, "parts": parts})

    # Claude needs the conversation to open with a user turn.
    while turns and turns[0]["role"] != "user":
        turns.pop(0)
    if not turns:
        raise BridgeError(400, "No user message to answer")
    return "\n\n".join(p for p in system_parts if p.strip()), turns


# ---------------------------------------------------------------------------
# Backend: Claude API through the Anthropic SDK
# ---------------------------------------------------------------------------

def to_anthropic_messages(turns: list[dict[str, Any]]) -> list[dict[str, Any]]:
    messages = []
    for turn in turns:
        blocks = []
        for part in turn["parts"]:
            if part["type"] == "text":
                blocks.append({"type": "text", "text": part["text"]})
            elif part["kind"] == "base64":
                blocks.append({"type": "image", "source": {"type": "base64", "media_type": part["media_type"], "data": part["data"]}})
            else:
                blocks.append({"type": "image", "source": {"type": "url", "url": part["url"]}})
        if len(blocks) == 1 and blocks[0]["type"] == "text":
            messages.append({"role": turn["role"], "content": blocks[0]["text"]})
        else:
            messages.append({"role": turn["role"], "content": blocks})
    return messages


class ApiBackend:
    def __init__(self, default_model: str, effort: str | None):
        import anthropic  # imported here so the claude-code backend works without it

        self.anthropic = anthropic
        self.client = anthropic.Anthropic()
        self.default_model = default_model
        self.effort = effort

    def models(self) -> list[str]:
        try:
            ids = [model.id for model in self.client.models.list()]
        except self.anthropic.APIError as error:
            log.warning("Could not list models: %s", error)
            return [self.default_model]
        # Put the configured default first so the device's setup page picks it.
        return [self.default_model] + [i for i in ids if i != self.default_model]

    def complete(self, model: str, system: str, turns: list[dict[str, Any]]) -> tuple[str, str]:
        model = model if model.startswith("claude-") else self.default_model
        request: dict[str, Any] = {
            "model": model,
            "max_tokens": API_MAX_TOKENS,
            "messages": to_anthropic_messages(turns),
        }
        if system:
            request["system"] = system
        if self.effort and model.startswith(EFFORT_MODEL_PREFIXES):
            request["output_config"] = {"effort": self.effort}

        anthropic = self.anthropic
        try:
            if model in DEFAULT_FALLBACK_MODELS:
                # On a safety decline, the API retries on Anthropic's recommended
                # fallback model inside the same call.
                response = self.client.beta.messages.create(betas=[FALLBACK_BETA], fallbacks="default", **request)
            else:
                response = self.client.messages.create(**request)
        except anthropic.AuthenticationError as error:
            raise BridgeError(401, "Bridge: the Anthropic API key was rejected") from error
        except anthropic.PermissionDeniedError as error:
            raise BridgeError(403, f"Bridge: {error.message}") from error
        except anthropic.NotFoundError as error:
            raise BridgeError(404, f"Bridge: unknown model or endpoint ({error.message})") from error
        except anthropic.RateLimitError as error:
            raise BridgeError(429, "Bridge: Claude rate limit reached, try again shortly") from error
        except anthropic.BadRequestError as error:
            raise BridgeError(400, f"Bridge: {error.message}") from error
        except anthropic.APIStatusError as error:
            raise BridgeError(502, f"Bridge: Claude API error {error.status_code}: {error.message}") from error
        except anthropic.APIConnectionError as error:
            raise BridgeError(502, "Bridge: cannot reach the Claude API") from error

        if response.stop_reason == "refusal":
            details = getattr(response, "stop_details", None)
            category = getattr(details, "category", None) if details else None
            raise BridgeError(422, "Claude declined this request" + (f" ({category})" if category else ""))

        text = "".join(block.text for block in response.content if block.type == "text").strip()
        if not text:
            raise BridgeError(502, "Claude returned no text")
        finish = "length" if response.stop_reason == "max_tokens" else "stop"
        log.info("api: model=%s in=%s out=%s stop=%s", response.model, response.usage.input_tokens,
                 response.usage.output_tokens, response.stop_reason)
        return text, finish


# ---------------------------------------------------------------------------
# Backend: Claude Code headless (claude -p) on the user's subscription
# ---------------------------------------------------------------------------

class ClaudeCodeBackend:
    def __init__(self, claude_bin: str, default_model: str, timeout: float):
        resolved = shutil.which(claude_bin)
        if not resolved:
            raise SystemExit(f"Cannot find '{claude_bin}'. Install Claude Code and run `claude` once to log in.")
        self.claude_bin = resolved
        self.default_model = default_model
        self.timeout = timeout

    def models(self) -> list[str]:
        return [self.default_model] + [m for m in CLAUDE_CODE_MODELS if m != self.default_model]

    def complete(self, model: str, system: str, turns: list[dict[str, Any]]) -> tuple[str, str]:
        model = model if (model in CLAUDE_CODE_MODELS or model.startswith("claude-")) else self.default_model
        workdir = tempfile.mkdtemp(prefix="mymicroai-")
        try:
            prompt = self._write_prompt(workdir, turns)
            command = [
                self.claude_bin, "-p",
                "--output-format", "json",
                "--model", model,
                # Only file reading, which is how Claude looks at the photos.
                "--tools", "Read",
                "--allowedTools", "Read",
                "--no-session-persistence",
            ]
            if system:
                command += ["--append-system-prompt", system]
            started = time.monotonic()
            try:
                result = subprocess.run(command, input=prompt, capture_output=True, text=True,
                                        cwd=workdir, timeout=self.timeout, check=False)
            except subprocess.TimeoutExpired as error:
                raise BridgeError(504, f"Claude Code took longer than {self.timeout:.0f}s") from error

            try:
                output = json.loads(result.stdout)
            except json.JSONDecodeError as error:
                detail = (result.stderr or result.stdout).strip().splitlines()
                raise BridgeError(502, "Claude Code failed: " + (detail[-1] if detail else f"exit {result.returncode}")) from error

            text = str(output.get("result") or "").strip()
            if output.get("is_error") or not text:
                raise BridgeError(502, "Claude Code error: " + (text or str(output.get("subtype", "no result"))))
            log.info("claude-code: model=%s %.1fs", model, time.monotonic() - started)
            return text, "stop"
        finally:
            shutil.rmtree(workdir, ignore_errors=True)

    @staticmethod
    def _write_prompt(workdir: str, turns: list[dict[str, Any]]) -> str:
        """Saves photos next to the prompt and flattens the chat into one prompt."""
        lines: list[str] = []
        history, current = turns[:-1], turns[-1]
        if history:
            lines.append("Earlier in this conversation:")
            for turn in history:
                text = " ".join(p["text"] for p in turn["parts"] if p["type"] == "text").strip()
                if text:
                    lines.append(f"{'User' if turn['role'] == 'user' else 'Assistant'}: {text}")
            lines.append("")

        photos: list[str] = []
        texts: list[str] = []
        for index, part in enumerate(current["parts"], start=1):
            if part["type"] == "text":
                if not PHOTO_LABEL.match(part["text"]):
                    texts.append(part["text"])
                continue
            if part["kind"] != "base64":
                texts.append(f"(image URL: {part['url']})")
                continue
            name = part.get("label") or f"image{index}"
            filename = f"{name}.{IMAGE_EXTENSIONS.get(part['media_type'], 'jpg')}"
            with open(os.path.join(workdir, filename), "wb") as handle:
                handle.write(base64.b64decode(part["data"]))
            photos.append(f"{name}: ./{filename}")

        if photos:
            lines.append("Photos from the device camera are saved in the current directory. "
                         "Open each one with the Read tool before answering:")
            lines.extend(photos)
            lines.append("")
        lines.append("Current message from the user:")
        lines.append("\n".join(texts).strip() or "Please analyze the attached photo.")
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# HTTP server (OpenAI-compatible surface)
# ---------------------------------------------------------------------------

def make_handler(backend: Any, token: str):
    class Handler(BaseHTTPRequestHandler):
        server_version = "MyMicroAIBridge/1.0"

        def log_message(self, fmt: str, *args: Any) -> None:
            log.debug("%s - %s", self.address_string(), fmt % args)

        def _send_json(self, status: int, body: dict[str, Any]) -> None:
            data = json.dumps(body, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _send_error(self, status: int, message: str) -> None:
            self._send_json(status, {"error": {"message": message, "type": "bridge_error"}})

        def _authorized(self) -> bool:
            header = self.headers.get("Authorization", "")
            supplied = header[7:].strip() if header.lower().startswith("bearer ") else self.headers.get("x-api-key", "")
            if hmac.compare_digest(supplied.encode(), token.encode()):
                return True
            self._send_error(401, "Bridge: wrong token (set it as the API key on the device)")
            return False

        def do_GET(self) -> None:
            path = self.path.split("?", 1)[0].rstrip("/")
            if path == "/health":
                self._send_json(200, {"status": "ok", "backend": type(backend).__name__})
                return
            if path not in ("/v1/models", "/models"):
                self._send_error(404, "Not found")
                return
            if not self._authorized():
                return
            models = backend.models()
            self._send_json(200, {"object": "list", "data": [{"id": m, "object": "model", "owned_by": "anthropic"} for m in models]})

        def do_POST(self) -> None:
            path = self.path.split("?", 1)[0].rstrip("/")
            if path not in ("/v1/chat/completions", "/chat/completions"):
                self._send_error(404, "Not found")
                return
            if not self._authorized():
                return
            length = int(self.headers.get("Content-Length") or 0)
            if length <= 0 or length > MAX_BODY_BYTES:
                self._send_error(413, "Request body is empty or too large")
                return
            try:
                request = json.loads(self.rfile.read(length))
                system, turns = to_turns(request.get("messages") or [])
                photos = sum(1 for t in turns for p in t["parts"] if p["type"] == "image")
                log.info("chat: %d turns, %d photo(s), %d bytes", len(turns), photos, length)
                text, finish = backend.complete(str(request.get("model") or ""), system, turns)
            except BridgeError as error:
                log.warning("chat failed: %s", error.message)
                self._send_error(error.status, error.message)
                return
            except json.JSONDecodeError:
                self._send_error(400, "Request body is not JSON")
                return
            self._send_json(200, {
                "id": f"chatcmpl-{uuid.uuid4().hex[:24]}",
                "object": "chat.completion",
                "created": int(time.time()),
                "model": request.get("model") or "",
                "choices": [{"index": 0, "message": {"role": "assistant", "content": text}, "finish_reason": finish}],
            })

    return Handler


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Bridge between a MyMicroAI device and Claude.")
    parser.add_argument("--backend", choices=["api", "claude-code"], default="api",
                        help="api: Claude API via the Anthropic SDK; claude-code: `claude -p` on your Claude subscription")
    parser.add_argument("--token", default=os.environ.get("MYMICROAI_BRIDGE_TOKEN"),
                        help="shared secret the device sends as its API key (or MYMICROAI_BRIDGE_TOKEN)")
    parser.add_argument("--host", default="0.0.0.0", help="interface to listen on (default: all)")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--model", default=None,
                        help=f"default model (api: {DEFAULT_MODEL}; claude-code: opus)")
    parser.add_argument("--effort", default="low", choices=["low", "medium", "high", "xhigh", "max", "none"],
                        help="api backend: output_config.effort; low keeps replies fast on the device (default)")
    parser.add_argument("--claude-bin", default="claude", help="claude-code backend: path to the Claude Code CLI")
    parser.add_argument("--timeout", type=float, default=55.0,
                        help="claude-code backend: seconds before giving up (the device waits about 60s)")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO, format="%(asctime)s %(message)s")
    if not args.token:
        parser.error("--token is required: pick a secret and enter it as the API key on the device")

    if args.backend == "api":
        backend: Any = ApiBackend(args.model or DEFAULT_MODEL, None if args.effort == "none" else args.effort)
    else:
        backend = ClaudeCodeBackend(args.claude_bin, args.model or "opus", args.timeout)

    server = ThreadingHTTPServer((args.host, args.port), make_handler(backend, args.token))
    log.info("MyMicroAI bridge (%s) on http://%s:%d/v1", args.backend, args.host, args.port)
    log.info("On the device: Base URL http://<this computer's IP>:%d/v1, API key = your token", args.port)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
