# Public patch queue

Patches applied on top of the pinned Servo tag (`../SERVO_TAG`) by
`../build-libservo.sh`, in the order listed in `series`.

Policy (spec 12.4):

- Anything carried for more than two rebases is proposed upstream or dropped.
- Every patch names the upstream issue or PR it corresponds to in its header.
- Behaviour Tuuli needs from the engine is added to `servo_capi` upstream,
  never by reaching into `libservo` from Rust glue (spec 3.3).

Open upstream items that would otherwise become patches are tracked in
`docs/UPSTREAM.md`.

The queue is currently empty.
