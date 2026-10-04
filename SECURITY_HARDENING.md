# btcpuzzle-secure — Puzzle 71 hardening v1

This fork is a security-focused wrapper around the upstream `ilkerccom/btcpuzzle` client.
The search algorithm and CUDA solver are intentionally left unchanged in v1.

## Pinned target

Hardened v1 is intentionally restricted to Bitcoin Puzzle 71:

`1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU`

The pool API is **not trusted to define the target**. Every received range is rejected if:

- `targetAddress` differs from the hardcoded Puzzle 71 address; or
- the Puzzle 71 address appears inside `proofOfWorkAddresses`.

This prevents a compromised or malicious coordinator from downgrading the real target into an ordinary proof address.

## Winner path

The upstream solver still invokes the normal `PoolClient::onKeyFound(address, privateKey)` callback.
The hardened wrapper intercepts that callback before the upstream implementation.

For ordinary proof addresses, processing is delegated to the upstream client.

For the hardcoded Puzzle 71 target:

1. Pool pinging is stopped.
2. The private key is encrypted locally with the configured RSA public key.
3. Encryption is fail-closed: empty output or plaintext fallback is rejected.
4. Only the encrypted ciphertext is written to `WINNER_P71_<timestamp>.enc`.
5. On Linux the winner file is created with mode `0600`.
6. Neither plaintext nor ciphertext is written to `poolclient.log`.
7. The private key is never sent to Telegram, a custom webhook, or btcpuzzle.info.
8. The process terminates with `std::_Exit()` before the original unsafe target branch in `main.cpp` can run.

## Required configuration

Hardened v1 refuses to start unless all of these are true:

```ini
target_puzzle=71
untrusted_computer=true
public_key=<VALID RSA PUBLIC KEY>
telegram_share=false
api_share=false
save_key=false
```

The corresponding RSA **private** key must never be stored on the scanning computer.
The intended design is to keep that private key only in a separate offline environment (for example an offline Tails session).

## OS hardening

On Unix/Linux, secure initialization also:

- sets `umask(0077)` so newly created files default to owner-only access;
- disables core dumps with `RLIMIT_CORE=0`;
- on Linux, calls `prctl(PR_SET_DUMPABLE, 0)`.

## Upstream preservation

`Pool/PoolClient.cpp` is deliberately retained without editing its large implementation.
During compilation, its `init`, `getRange`, and `onKeyFound` definitions are renamed to:

- `upstream_init`
- `upstream_getRange`
- `upstream_onKeyFound`

`Pool/SecurePoolClient.cpp` provides the hardened public wrappers with the original method names.
This makes the security delta small and auditable while preserving upstream proof/range behavior.

## Not yet addressed in v1

The following are intentionally deferred until the security path is validated:

- CUDA/Blackwell (`sm_120`) performance optimization;
- reproducible builds / dependency pinning;
- replacement of legacy OpenSSL RSA APIs with EVP + explicit OAEP digest parameters;
- memory locking/zeroization improvements;
- automated integration test that simulates a winner without touching mainnet.

Do not use this branch for a real Puzzle 71 search until it has been compiled and the winner path has been tested with a deliberately generated test key/address.
