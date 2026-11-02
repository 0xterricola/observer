# Observer — Basecamp Chat to Casberi

**Status:** Native Chat integration — developer preview
**Platform:** macOS Apple Silicon
**Access:** Read-only (`chat.read`)

Observer provides access to existing Basecamp Chat conversations from Casberi on the same trusted local network.

## Quick Start

For users with Logos Basecamp and a compatible Casberi iOS build:

1. **Install Observer:** Install the Observer core and UI `.lgx` packages through Basecamp Package Manager.
2. **Initialize Chat:** Open Basecamp Chat and confirm your conversations load.
3. **Start Observer:** Open Observer UI and click **Start Observer** if the service is stopped.
4. **Pair Casberi:** Click **Pair New Device**, scan the QR in Casberi, and authorize `chat.read`.
5. **Verify readiness:** Click **Run Setup Checks**. The six checks should pass when all dependencies and authorization are ready.
6. **Test messaging:** Open Casberi conversations. Send a message from another Chat client and verify it appears without re-pairing.

**Already installed and paired?** Open Basecamp Chat and Observer UI, verify the service is running, and run the readiness checks. No new pairing is needed.

**After replacing the UI package:** Fully quit and reopen Basecamp to reload the Qt Remote Objects replica factory.

No JSON-RPC bridge or standalone Chat daemon is required.

## Architecture

~~~text
Logos Basecamp
├── Basecamp Chat
│       ↓
│   Native Chat IPC
│       ↓
├── Observer core (observer)
│   ├── Observer v2 HTTPS
│   ├── Device authorization
│   └── Pairing
└── Observer UI (observer_ui)
    ├── Service management
    ├── Readiness checks
    └── Pairing interface

        HTTPS + chat.read
               ↓
          Casberi iOS
~~~

Observer reads from the existing Basecamp Chat identity. It does not create a separate Chat identity or RLN membership.

Casberi message sending is not part of this read-only integration.

## Requirements

- Logos Basecamp running on macOS Apple Silicon.
- Basecamp Chat installed and initialized.
- Observer core module (`observer`).
- Observer UI module (`observer_ui`).
- Compatible Casberi iOS build supporting Observer v2 and `chat.read`.
- Mac and iPhone connected to the same trusted LAN.

## Installation

Install Observer core and Observer UI using Basecamp Package Manager.

The local development package directory is:

~~~text
~/Library/Application Support/ObserverNativeChatPOC/packages/
~~~

Use stable filenames for future package builds:

~~~text
observer-core.lgx
observer-ui.lgx
~~~

These are local naming conventions. Previously built core packages may have different filenames.

Install the core module first, then the UI module.

After updating either the UI backend or its replica factory, fully quit and reopen Basecamp before testing. Reloading only the Observer tab may leave an older Qt Remote Objects replica factory loaded in memory.

## Initialize Chat

Open Basecamp Chat.

Confirm existing conversations are visible and messages load.

Observer uses the active Basecamp Chat integration through native IPC. No HTTP Chat bridge is required.

## Start Observer

Open the Observer UI.

Select **Start Observer** if the managed service is stopped.

Observer exposes an authenticated HTTPS endpoint on the configured LAN interface.

Do not run a second standalone Observer server against the same port or persistent state directory.

## Readiness Checks

Click **Run Setup Checks**.

| Check | Expected status |
|---|---|
| Observer Service | Running |
| LAN & HTTPS | Identity verified |
| Authorized Devices | Authorized `chat.read` devices |
| Native Chat SDK | Responding |
| Basecamp Chat | Initialized |
| Conversations | Readable |

An unpaired installation may not achieve 6/6 until device authorization is completed.

## Pair Casberi

In Observer UI, select **Pair New Device**.

Open Casberi's Observer pairing flow and scan the displayed QR code.

Authorize the `chat.read` capability.

Pairing offers expire after five minutes. Generate a new offer if necessary.

The pairing QR contains temporary authorization material. Do not publish it.

Existing paired devices should retain their authorization across normal restarts when the Observer state directory is preserved.

## End-to-End Verification

In Casberi, confirm existing Basecamp Chat conversations and messages load.

Then send a new message from an independent Chat client to the Basecamp Chat identity connected to Observer.

Confirm the message appears in Basecamp Chat, followed by Casberi, without repeating pairing.

~~~text
Independent Chat client
        ↓
Basecamp Chat
        ↓
Native IPC
        ↓
Observer v2 HTTPS
        ↓
Casberi
~~~

An HTTP `/health` response confirms service availability, not end-to-end Chat access.

## Restarting Basecamp

After restarting Basecamp:

1. Open Basecamp Chat.
2. Open Observer UI.
3. Start Observer if necessary.
4. Run Setup Checks.
5. Confirm Casberi can read conversations.

Do not delete the Observer state directory as a routine troubleshooting step. It contains TLS identity and device authorization state.

## Troubleshooting

| Symptom | Check |
|---|---|
| UI buttons disabled | Verify Qt Remote Objects backend readiness |
| Replica signature mismatch | Completely restart Basecamp |
| Observer Service stopped | Select Start Observer |
| LAN & HTTPS unavailable | Verify service, LAN endpoint, firewall, and certificate identity |
| Native Chat SDK unavailable | Verify Chat initialization and native IPC |
| Basecamp Chat unavailable | Open Chat and load conversations |
| Conversations unavailable | Verify Chat and native adapter status |
| Casberi cannot connect | Check LAN reachability and device authorization |
| Pairing offer expired | Generate a fresh QR |
| Casberi cannot send messages | Expected: current integration is read-only |

Basecamp diagnostic logs:

~~~text
~/Library/Application Support/Logos/LogosBasecamp/logs/
~~~

The error:

~~~text
qt.remoteobjects: Signature mismatch for ObserverManagerBackendReplica
~~~

can occur when Basecamp retains an old replica factory after a UI package replacement. A complete Basecamp restart reloads the matching factory.

## Standalone CLI

The standalone `v2_server.py` remains available for independent Observer monitoring.

It does not automatically access Basecamp Chat conversations. Native Chat access requires the managed Basecamp integration and IPC connection.

Do not use obsolete HTTP Chat bridge arguments with the standalone server.

## Security

- Pair only trusted devices.
- `chat.read` exposes decrypted Chat content to the authorized client.
- Observer uses HTTPS, pinned server identity, device credentials, and request authentication.
- Native Chat IPC stays local to the Basecamp integration.
- Keep pairing secrets, private TLS keys, and persistent state private.
- Do not publicly forward the Observer HTTPS port.
- Observer does not require wallet signing authority or outbound Chat permissions.
- This guide covers local-network access only.

## References

- [Observer repository](https://github.com/0xterricola/observer)
- [Basecamp installation](https://docs.logos.co/basecamp/install-logos-basecamp)
- [Basecamp module installation](https://docs.logos.co/basecamp/install-and-load-a-module-in-logos-basecamp)
