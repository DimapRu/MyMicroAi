# MyMicroAI Bridge

A small server that sits between the device and Claude. The device keeps speaking the
OpenAI-compatible chat API it already has, so nothing changes in the firmware: point the
device's **Base URL** at the bridge and use the bridge **token** as its **API key**.

```text
MyMicroAI ──Wi-Fi──▶ bridge (PC, Raspberry Pi, server) ──▶ Claude API          (--backend api)
                                                       └──▶ claude -p (Claude Code, your subscription)  (--backend claude-code)
```

Photos travel with the request: the bridge passes them to Claude as images (`api`) or saves
them as `photo1.jpg`, `photo2.jpg`, … in a temporary folder that Claude Code opens with its
Read tool (`claude-code`). The folder is deleted after each answer.

## Requirements

- Python 3.10 or newer, on a machine in the same network as the device.
- `--backend api`: `pip install -r requirements.txt` and an Anthropic API key in
  `ANTHROPIC_API_KEY` (or an `ant auth login` profile).
- `--backend claude-code`: [Claude Code](https://code.claude.com/docs) installed and logged in
  with your Claude account (run `claude` once and sign in). Requests then count against that
  account's Claude plan, not API billing. No Python packages needed.

## Run

```bash
# Claude API with an API key
export ANTHROPIC_API_KEY=sk-ant-...
python mymicroai_bridge.py --token my-secret

# Your Claude subscription through Claude Code
python mymicroai_bridge.py --backend claude-code --token my-secret --model sonnet
```

On the device, open **SETUP**, tap the **Мост MyMicroAI** preset and enter:

- Base URL: `http://<bridge computer's IP>:8787/v1` (find the IP with `ipconfig` / `ip addr`)
- API key: the token you passed with `--token`

Then load the model list and save.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--backend` | `api` | `api` (Anthropic SDK) or `claude-code` (`claude -p`) |
| `--token` | `$MYMICROAI_BRIDGE_TOKEN` | shared secret; required |
| `--host` / `--port` | `0.0.0.0` / `8787` | where to listen |
| `--model` | `claude-opus-5-5` (api), `opus` (claude-code) | used when the device does not pick one |
| `--effort` | `low` | api backend: `output_config.effort`; `low` answers fastest, `none` leaves the model default |
| `--claude-bin` | `claude` | path to the Claude Code CLI |
| `--timeout` | `55` | claude-code backend: seconds to wait (the device gives up after about 60 s) |

With the `api` backend, requests to `claude-opus-5-5`, `claude-opus-5`, `claude-sonnet-5-5`
and `claude-fable-5-1` opt into server-side refusal fallback (`fallbacks: "default"`), so a
request a safety classifier declines is retried on Anthropic's recommended fallback model.
A request that is still declined comes back to the device as "Claude declined this request".

## Phones and servers

- **Claude app on a phone** cannot receive prompts from other devices, so the bridge cannot
  "type into" it. Run the bridge on a computer, a Raspberry Pi or a small server instead.
- **Android**: the `api` backend runs in [Termux](https://termux.dev) (`pkg install python`,
  then the steps above). Claude Code under Termux is not officially supported.
- **A server on the internet**: keep the token secret and put the bridge behind HTTPS
  (a reverse proxy) or a private network such as Tailscale; the bridge itself speaks plain HTTP.

## Endpoints

- `GET /v1/models` — model list for the device's setup page
- `POST /v1/chat/completions` — OpenAI-style chat, text and `image_url` data URLs
- `GET /health` — liveness check (no token)
