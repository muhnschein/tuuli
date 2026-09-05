# Public patch queue

Patches applied on top of the pinned Servo tag (`../SERVO_TAG`) by
`../build.sh`, in the order listed in `series`.  With a non-empty queue
the script clones the tag, applies the patches and points cargo at the
patched checkout through a `[patch]` section in `../app/.cargo/config.toml`;
with an empty queue cargo builds the tag straight from git.

Policy (spec 12.4):

- Anything carried for more than two rebases is proposed upstream or dropped.
- Every patch names the upstream issue or PR it corresponds to in its header.
- Behaviour Tuuli needs from the engine is added to libservo upstream,
  never by reaching around its API from the backend (spec 3.3).

Open upstream items that would otherwise become patches are tracked in
`docs/UPSTREAM.md`.

The queue is currently empty.
