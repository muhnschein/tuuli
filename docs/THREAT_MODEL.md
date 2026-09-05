# Threat model, stated plainly

This is the text the About page and the store descriptions carry.  Keep
them in sync.

## What Tuuli does not have

Tuuli, through M3, offers **no meaningful sandbox between web content and
the app's own privileges.**

- The engine runs in the browser's process (spec 4.1).  Servo's
  multi-process content isolation is not the well-trodden path and its
  sandboxing is incomplete upstream.
- Web content that achieves code execution in the engine therefore gets
  everything the sailjail profile grants the app: network, audio, and the
  user's Downloads, Pictures, Videos and Documents folders (spec 9.1).
- An engine crash takes the whole app down.  Session restore (spec 8.4)
  turns that into a one-second interruption, not a security boundary.

Users who need a hardened browser should use Sailfish Browser.

## What Tuuli does

- Keeps the sailjail profile as tight as the feature set allows.
  `Location`, `Camera` and `Microphone` are added only with the milestone
  that uses them.
- Denies every web permission by default; prompts are Silica dialogs;
  decisions persist per origin and never from private tabs.
- Blocks third-party cookies, sends DNT and GPC, uses
  `strict-origin-when-cross-origin`, defaults to a non-tracking search
  engine with no revenue arrangement (spec 9.4).
- Uses the system CA bundle; ships no roots of its own.
- Never mixes private and non-private documents in one engine webview;
  private tabs write no history, session, permission or Transfer Engine
  records.
- Does not intercept network requests and does not call cosmetic
  filtering "ad blocking" (spec 9.3).

## Removing the caveat

Requires out-of-process content plus a functioning seccomp policy.  That is
an M4-or-later question and depends on upstream.  The
`tuuli_core::engine::Engine` seam exists so that the day it is possible,
the change stays below the models and the QML.
